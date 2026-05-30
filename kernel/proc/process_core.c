#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/vmm.h"
#include "lib/string.h"

struct tty *g_user_tty;
volatile uint32_t *g_user_timer_ticks;
uint64_t g_current_user_raw_entry;
uint8_t g_elf_file_buffer[NOS_ELF_FILE_BUFFER_SIZE];

struct user_page_mapping g_user_page_mappings[USER_DYNAMIC_PAGE_LIMIT];
struct process_session g_user_session;
struct process_session *g_bound_session = &g_user_session;
struct user_page_mapping *g_bound_mappings = g_user_page_mappings;
struct cpu_user_state g_cpu_user_state[USER_CPU_COUNT];
struct process g_process_slots[USER_PROCESS_LIMIT];
uint8_t g_process_slot_used[USER_PROCESS_LIMIT];
uint32_t g_next_pid = 1;
uint64_t g_next_user_alloc = USER_ALLOC_BASE;
uint32_t g_process_exec_last_error;
uint32_t g_process_exec_last_stage;
uint32_t g_process_exec_read_open_rc;
uint32_t g_process_exec_read_node_kind;
uint32_t g_process_exec_read_mount_kind;
uint32_t g_process_exec_read_file_size;
uint32_t g_process_exec_read_bytes;
uint32_t g_process_exec_read_result;
struct process g_last_exited_process;
struct job_runtime g_bg_runtimes[USER_PROCESS_LIMIT];
uint32_t g_scheduler_next_slot;

static int process_session_has_image(const struct process_session *session) {
    return session != NULL && session->process.image_kind != PROCESS_IMAGE_NONE;
}

static void process_set_default_state(struct process *proc,
                                      uint32_t slot,
                                      enum process_state state) {
    if (proc == NULL) {
        return;
    }
    proc->pid = 0;
    proc->slot = slot;
    proc->state = state;
    proc->exit_code = 0;
    proc->has_saved_frame = 0;
    proc->wake_tick = 0;
    proc->name = NULL;
    proc->name_storage[0] = '\0';
    proc->cwd_storage[0] = '/';
    proc->cwd_storage[1] = '\0';
    proc->image_kind = PROCESS_IMAGE_NONE;
    proc->entry = 0;
    proc->stack_top = 0;
    proc->console_handle = NULL;
    proc->address_space = NULL;
    for (uint32_t i = 0; i < PROCESS_FILE_MAX; i++) {
        file_reset(&proc->files[i]);
    }
}

static void job_reset_mapping_table(struct user_page_mapping *mappings) {
    if (mappings == NULL) {
        return;
    }
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        mappings[i].used = 0;
        mappings[i].virt_addr = 0;
        mappings[i].phys_addr = 0;
        mappings[i].reserved_pool = 0;
    }
}

static void job_reset_runtime_slot(struct job_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }
    runtime->used = 0;
    runtime->entry = 0;
    runtime->stack_top = 0;
    addrspace_reset(&runtime->session.address_space);
    process_clear_slot_state(&runtime->session.process);
    job_reset_mapping_table(runtime->mappings);
}

static void process_reset_exit_record(struct process *proc) {
    process_set_default_state(proc, 0, PROCESS_STATE_FREE);
}

static void process_refresh_console_from_stdio(struct process *proc) {
    void *tty_handle;

    if (proc == NULL) {
        return;
    }
    tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDIN]);
    if (tty_handle == NULL) {
        tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDOUT]);
    }
    if (tty_handle == NULL) {
        tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDERR]);
    }
    if (tty_handle != NULL) {
        proc->console_handle = tty_handle;
    }
}

static void process_prepare_slot(struct process *proc,
                                 uint32_t slot,
                                 struct address_space *address_space,
                                 const struct process *parent_proc) {
    process_set_default_state(proc, slot, PROCESS_STATE_RUNNING);
    proc->pid = g_next_pid++;
    proc->console_handle = parent_proc != NULL ? parent_proc->console_handle : g_user_tty;
    if (parent_proc != NULL && parent_proc->image_kind != PROCESS_IMAGE_NONE) {
        for (uint32_t i = 0; i <= SYS_FD_STDERR; i++) {
            (void)file_clone(&proc->files[i], &parent_proc->files[i]);
        }
        process_refresh_console_from_stdio(proc);
        process_set_cwd(proc, process_cwd(parent_proc));
    } else {
        process_init_stdio(proc);
        process_set_cwd(proc, "/");
    }
    proc->address_space = address_space;
}

static void process_reset_slots(void) {
    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        g_process_slot_used[i] = 0;
        process_set_default_state(&g_process_slots[i], i, PROCESS_STATE_FREE);
        job_reset_runtime_slot(&g_bg_runtimes[i]);
    }
}

static void process_init_reserved_window(struct address_space *address_space) {
    uint64_t reserved_base = 0;
    uint64_t reserved_top = 0;

    if (address_space == NULL) {
        return;
    }
    if (vmm_query((uint64_t)(uintptr_t)__userelf_start, &reserved_base) &&
        vmm_query((uint64_t)(uintptr_t)__userelf_end - USER_PAGE_SIZE, &reserved_top)) {
        address_space->reserved_phys_base = reserved_base;
        address_space->reserved_phys_limit = reserved_top + USER_PAGE_SIZE;
        address_space->reserved_phys_next = reserved_base;
        return;
    }

    address_space->reserved_phys_base = 0;
    address_space->reserved_phys_limit = 0;
    address_space->reserved_phys_next = 0;
}

static void sched_save_process_frame(struct process *proc,
                                     const struct syscall_frame *frame,
                                     uint64_t result) {
    if (proc == NULL || frame == NULL) {
        return;
    }
    proc->saved_frame = *frame;
    proc->saved_frame.rax = result;
    proc->has_saved_frame = 1;
    proc->wake_tick = 0;
}

static void sched_capture_process_frame(struct process *proc, const struct syscall_frame *frame) {
    if (proc == NULL || frame == NULL) {
        return;
    }
    proc->saved_frame = *frame;
    proc->has_saved_frame = 1;
    proc->wake_tick = 0;
}

void process_bind_session(struct process_session *session,
                          struct user_page_mapping *mappings) {
    g_bound_session = session != NULL ? session : &g_user_session;
    g_bound_mappings = mappings != NULL ? mappings : g_user_page_mappings;
}

void addrspace_reset(struct address_space *address_space) {
    if (address_space == NULL) {
        return;
    }
    address_space->kernel_cr3 = 0;
    address_space->user_cr3 = 0;
    address_space->reserved_phys_base = 0;
    address_space->reserved_phys_limit = 0;
    address_space->reserved_phys_next = 0;
}

void process_clear_slot_state(struct process *proc) {
    if (proc == NULL) {
        return;
    }
    process_set_default_state(proc, 0, PROCESS_STATE_FREE);
}

void process_init_stdio(struct process *proc) {
    if (proc == NULL) {
        return;
    }
    file_init_console_in(&proc->files[SYS_FD_STDIN], proc->console_handle);
    file_init_console_out(&proc->files[SYS_FD_STDOUT], proc->console_handle);
    file_init_console_err(&proc->files[SYS_FD_STDERR], proc->console_handle);
}

void job_inherit_stdio(struct process *proc) {
    if (proc == NULL) {
        return;
    }
    if (!process_session_has_image(g_bound_session)) {
        process_init_stdio(proc);
        return;
    }
    for (uint32_t i = 0; i <= SYS_FD_STDERR; i++) {
        (void)file_clone(&proc->files[i], &g_bound_session->process.files[i]);
    }
    process_refresh_console_from_stdio(proc);
}

uint32_t sched_current_ticks(void) {
    return g_user_timer_ticks != NULL ? *g_user_timer_ticks : 0;
}

void process_set_name(struct process *proc, const char *name) {
    uint32_t i = 0;

    if (proc == NULL) {
        return;
    }
    if (name == NULL) {
        proc->name_storage[0] = '\0';
        proc->name = NULL;
        return;
    }
    while (i + 1 < sizeof(proc->name_storage) && name[i] != '\0') {
        proc->name_storage[i] = name[i];
        i++;
    }
    proc->name_storage[i] = '\0';
    proc->name = proc->name_storage;
}

void process_refresh_name_ptr(struct process *proc) {
    if (proc == NULL) {
        return;
    }
    proc->name = proc->name_storage[0] != '\0' ? proc->name_storage : NULL;
}

void process_snapshot_fill(struct process_snapshot *out, const struct process *proc) {
    uint32_t i;

    if (out == NULL) {
        return;
    }
    out->pid = 0;
    out->slot = 0;
    out->state = PROCESS_STATE_FREE;
    out->exit_code = 0;
    out->wake_tick = 0;
    out->image_kind = PROCESS_IMAGE_NONE;
    for (i = 0; i < sizeof(out->name); i++) {
        out->name[i] = '\0';
    }
    if (proc == NULL) {
        return;
    }
    out->pid = proc->pid;
    out->slot = proc->slot;
    out->state = (uint32_t)proc->state;
    out->exit_code = proc->exit_code;
    out->wake_tick = proc->wake_tick;
    out->image_kind = (uint32_t)proc->image_kind;
    for (i = 0; i + 1u < sizeof(out->name) && proc->name_storage[i] != '\0'; i++) {
        out->name[i] = proc->name_storage[i];
    }
}

const char *process_cwd(const struct process *proc) {
    if (proc == NULL || proc->cwd_storage[0] == '\0') {
        return "/";
    }
    return proc->cwd_storage;
}

void process_set_cwd(struct process *proc, const char *path) {
    uint32_t i = 0;

    if (proc == NULL) {
        return;
    }
    if (path == NULL || path[0] == '\0') {
        proc->cwd_storage[0] = '/';
        proc->cwd_storage[1] = '\0';
        return;
    }
    while (path[i] != '\0' && i + 1u < sizeof(proc->cwd_storage)) {
        proc->cwd_storage[i] = path[i];
        i++;
    }
    proc->cwd_storage[i] = '\0';
    if (proc->cwd_storage[0] == '\0') {
        proc->cwd_storage[0] = '/';
        proc->cwd_storage[1] = '\0';
    }
}

int job_process_ignores_sigint(const struct process *proc) {
    const char *name;
    const char *base;
    uint32_t i = 0u;

    if (proc == NULL || proc->name == NULL) {
        return 0;
    }
    name = proc->name;
    base = name;
    while (name[i] != '\0') {
        if (name[i] == '/') {
            base = name + i + 1u;
        }
        i++;
    }
    return streq(name, "ush") ||
           streq(name, "USH.ELF") ||
           streq(base, "ush") ||
           streq(base, "USH") ||
           streq(base, "USH.ELF") ||
           streq(base, "ush.elf");
}

struct process *process_alloc_slot(struct process_session *session, const struct process *parent_proc) {
    struct address_space *address_space;

    address_space = session != NULL ? &session->address_space : NULL;
    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        if (!g_process_slot_used[i]) {
            g_process_slot_used[i] = 1;
            process_prepare_slot(&g_process_slots[i], i, address_space, parent_proc);
            return &g_process_slots[i];
        }
    }
    return NULL;
}

void process_clear_current(struct process_session *session) {
    if (session == NULL) {
        return;
    }
    process_clear_slot_state(&session->process);
}

void process_discard_files(struct process *proc) {
    if (proc == NULL) {
        return;
    }
    for (uint32_t i = 0; i < PROCESS_FILE_MAX; i++) {
        file_discard(&proc->files[i]);
    }
}

void process_mark_exit_pending(struct process *proc, int32_t exit_code) {
    if (proc == NULL || proc->image_kind == PROCESS_IMAGE_NONE) {
        return;
    }
    proc->exit_code = exit_code;
    proc->state = PROCESS_STATE_EXITED;
    proc->has_saved_frame = 0;
    proc->wake_tick = 0;
}

void process_mark_exited(struct process *proc, int32_t exit_code) {
    struct process *slot_proc;

    if (proc == NULL) {
        return;
    }
    process_mark_exit_pending(proc, exit_code);
    process_discard_files(proc);
    if (proc->slot >= USER_PROCESS_LIMIT) {
        return;
    }
    slot_proc = &g_process_slots[proc->slot];
    *slot_proc = *proc;
    process_refresh_name_ptr(slot_proc);
    slot_proc->state = PROCESS_STATE_EXITED;
    slot_proc->exit_code = exit_code;
    slot_proc->wake_tick = 0;
    slot_proc->console_handle = NULL;
    slot_proc->address_space = NULL;
    g_process_slot_used[proc->slot] = 1;
    g_last_exited_process = *slot_proc;
    process_refresh_name_ptr(&g_last_exited_process);
}

void process_init(struct tty *tty, volatile uint32_t *timer_ticks) {
    g_user_tty = tty;
    g_user_timer_ticks = timer_ticks;
    process_bind_session(&g_user_session, g_user_page_mappings);
    addrspace_reset(&g_user_session.address_space);
    process_reset_slots();
    process_reset_exit_record(&g_last_exited_process);
    process_clear_slot_state(&g_user_session.process);
    process_init_reserved_window(&g_user_session.address_space);
    g_scheduler_next_slot = 0;
    (void)timer_ticks;
}

struct process_session *process_current_session(void) {
    return g_bound_session;
}

struct user_page_mapping *process_current_mappings(void) {
    return g_bound_mappings;
}

void process_exit_current(struct process_session *session, int32_t exit_code) {
    if (!process_session_has_image(session)) {
        return;
    }
    if (exit_code < 0) {
        if (session->process.name != NULL) {
            kprint("proc: exit pid=%u code=%d name=%s\n",
                   session->process.pid,
                   exit_code,
                   session->process.name);
        } else {
            kprint("proc: exit pid=%u code=%d\n", session->process.pid, exit_code);
        }
    }
    process_mark_exit_pending(&session->process, exit_code);
}

void sched_yield_current(struct process_session *session, const struct syscall_frame *frame) {
    if (frame == NULL || !process_session_has_image(session)) {
        return;
    }
    sched_save_process_frame(&session->process, frame, 0);
    session->process.state = PROCESS_STATE_READY;
}

void sched_sleep_current(struct process_session *session, const struct syscall_frame *frame, uint32_t ticks) {
    uint32_t now;

    if (frame == NULL || !process_session_has_image(session)) {
        return;
    }
    sched_save_process_frame(&session->process, frame, 0);
    now = sched_current_ticks();
    session->process.wake_tick = now + ticks;
    session->process.state = ticks == 0 ? PROCESS_STATE_READY : PROCESS_STATE_SLEEPING;
}

void sched_preempt_current(struct process_session *session, const struct syscall_frame *frame) {
    if (frame == NULL || !process_session_has_image(session)) {
        return;
    }
    if (session->process.state != PROCESS_STATE_RUNNING) {
        return;
    }
    sched_capture_process_frame(&session->process, frame);
    session->process.state = PROCESS_STATE_READY;
}

void sched_resume_current_syscall(struct process_session *session,
                                  const struct syscall_frame *frame,
                                  uint64_t result) {
    if (frame == NULL || !process_session_has_image(session)) {
        return;
    }
    sched_save_process_frame(&session->process, frame, result);
    session->process.state = PROCESS_STATE_READY;
}
