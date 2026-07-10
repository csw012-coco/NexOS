#include "context.h"
#include "gdt.h"
#include "abi/syscall_abi.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/path_resolve_internal.h"
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/proc/context.h"
#include "kernel/public/proc/runqueue.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "user.h"
#include "lib/string.h"

enum {
    SCHEDULER_TASKS = 8,
    TASK_NAME_SIZE = 16,
    USER_HEAP_BASE = 0x50000000u,
    USER_HEAP_LIMIT = 0x70000000u,
    I386_FPU_STATE_SIZE = 108
};

struct i386_fpu_state {
    uint8_t bytes[I386_FPU_STATE_SIZE];
};

struct scheduler_task {
    uint32_t ticks;
    uint32_t root;
    uint32_t result;
    uint32_t heap_next;
    uint32_t parent_pid;
    uint32_t wait_pid;
    uint32_t stack_top;
    uint32_t stack_low;
    uint32_t stack_limit;
    uint32_t stack_grow_events;
    struct address_space address_space;
    struct process process;
    uint8_t fpu_valid;
    struct i386_fpu_state fpu_state __attribute__((aligned(16)));
};

static struct scheduler_task tasks[SCHEDULER_TASKS];
static struct process *task_processes[SCHEDULER_TASKS];
static struct sched_runqueue runqueue;
static volatile uint32_t scheduler_active;
static uint32_t scheduler_tick_count;
static uint32_t scheduler_kernel_root;
static uint32_t scheduler_next_pid = 1u;
static uint32_t scheduler_quiet_tty_output;

static uint32_t current_task_slot(void) {
    return sched_runqueue_current_slot(&runqueue);
}

static uint32_t read_cr0(void) {
    uint32_t value;

    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static void write_cr0(uint32_t value) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}

static void scheduler_fpu_enable(void) {
    enum {
        CR0_MP = 1u << 1,
        CR0_EM = 1u << 2,
        CR0_TS = 1u << 3,
        CR0_NE = 1u << 5
    };
    uint32_t cr0 = read_cr0();

    cr0 &= ~(CR0_EM | CR0_TS);
    cr0 |= CR0_MP | CR0_NE;
    write_cr0(cr0);
    __asm__ volatile("fninit" : : : "memory");
}

static void scheduler_fpu_save(uint32_t slot) {
    if (slot >= SCHEDULER_TASKS ||
        tasks[slot].process.state == PROCESS_STATE_FREE) {
        return;
    }
    __asm__ volatile("fnsave %0\n\tfwait"
                     : "=m"(tasks[slot].fpu_state)
                     :
                     : "memory");
    tasks[slot].fpu_valid = 1u;
}

static void scheduler_fpu_restore(uint32_t slot) {
    if (slot >= SCHEDULER_TASKS || tasks[slot].fpu_valid == 0u) {
        __asm__ volatile("fninit" : : : "memory");
        return;
    }
    __asm__ volatile("frstor %0"
                     :
                     : "m"(tasks[slot].fpu_state)
                     : "memory");
}

static void scheduler_switch_fpu(uint32_t previous, uint32_t next) {
    if (previous == next || next >= SCHEDULER_TASKS) {
        return;
    }
    if (previous < SCHEDULER_TASKS) {
        scheduler_fpu_save(previous);
    }
    scheduler_fpu_restore(next);
}

static int tick_before(uint32_t left, uint32_t right) {
    return (int32_t)(left - right) < 0;
}

static void scheduler_wake_sleepers(void) {
    for (uint32_t slot = 0u; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_SLEEPING &&
            !tick_before(scheduler_tick_count,
                         tasks[slot].process.wake_tick)) {
            tasks[slot].process.wake_tick = 0u;
            tasks[slot].process.state = PROCESS_STATE_READY;
        }
    }
}

static void task_reap(uint32_t slot) {
    if (slot >= SCHEDULER_TASKS ||
        tasks[slot].process.state != PROCESS_STATE_EXITED) {
        return;
    }
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        file_discard(&tasks[slot].process.files[fd]);
    }
    tasks[slot].root = 0u;
    tasks[slot].result = 0u;
    tasks[slot].heap_next = USER_HEAP_BASE;
    tasks[slot].parent_pid = 0u;
    tasks[slot].wait_pid = 0u;
    tasks[slot].fpu_valid = 0u;
    tasks[slot].process.state = PROCESS_STATE_FREE;
}

static uint32_t scheduler_vfs_node_size(const struct vfs_node *node) {
    if (node == 0 || node->kind != VFS_NODE_FILE) {
        return 0u;
    }
    if (node->mount_kind == VFS_MOUNT_FAT32) {
        return node->handle.fat32_file.size;
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        return node->handle.nxfs_inode.size;
    }
    return 0u;
}

static void copy_text(char *dst, uint32_t size, const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void scheduler_refresh_console_from_stdio(struct process *process) {
    void *tty_handle;

    if (process == 0) {
        return;
    }
    tty_handle = file_tty_private_handle(&process->files[SYS_FD_STDIN]);
    if (tty_handle == 0) {
        tty_handle = file_tty_private_handle(&process->files[SYS_FD_STDOUT]);
    }
    if (tty_handle == 0) {
        tty_handle = file_tty_private_handle(&process->files[SYS_FD_STDERR]);
    }
    if (tty_handle != 0) {
        process->console_handle = tty_handle;
    }
}

static void scheduler_reset_file_array(struct file files[NOS_PROCESS_FILE_MAX]) {
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        file_reset(&files[fd]);
    }
}

static void scheduler_discard_file_array(struct file files[NOS_PROCESS_FILE_MAX]) {
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        file_discard(&files[fd]);
    }
}

static int scheduler_clone_inheritable_files(
    const struct process *parent,
    struct file out[NOS_PROCESS_FILE_MAX]) {
    scheduler_reset_file_array(out);
    if (parent == 0) {
        return 1;
    }
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        const struct file *src = &parent->files[fd];

        if (!file_is_active(src) ||
            (src->flags & KERNEL_FILE_CLOSE_ON_SPAWN) != 0u) {
            continue;
        }
        if (!file_clone(&out[fd], src)) {
            scheduler_discard_file_array(out);
            return 0;
        }
    }
    return 1;
}

static void scheduler_install_cloned_files(
    struct process *process,
    struct file cloned[NOS_PROCESS_FILE_MAX]) {
    if (process == 0) {
        scheduler_discard_file_array(cloned);
        return;
    }
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        file_discard(&process->files[fd]);
        process->files[fd] = cloned[fd];
        file_reset(&cloned[fd]);
    }
    scheduler_refresh_console_from_stdio(process);
}

static uint32_t scheduler_text_len(const char *text) {
    uint32_t len = 0u;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

static int scheduler_path_pop_segment(char *path) {
    uint32_t len = scheduler_text_len(path);

    if (path == 0 || len == 0u) {
        return 0;
    }
    while (len > 1u && path[len - 1u] == '/') {
        path[--len] = '\0';
    }
    while (len > 1u && path[len - 1u] != '/') {
        len--;
    }
    if (len <= 1u) {
        path[0] = '/';
        path[1] = '\0';
        return 1;
    }
    path[len - 1u] = '\0';
    return 1;
}

static int scheduler_path_append_segment(char *path,
                                         uint32_t path_size,
                                         const char *segment,
                                         uint32_t segment_len) {
    uint32_t len = scheduler_text_len(path);

    if (path == 0 || segment == 0 || path_size < 2u) {
        return 0;
    }
    if (len == 0u) {
        path[0] = '/';
        path[1] = '\0';
        len = 1u;
    }
    if (len > 1u && path[len - 1u] != '/') {
        if (len + 1u >= path_size) {
            return 0;
        }
        path[len++] = '/';
    }
    if (len + segment_len >= path_size) {
        return 0;
    }
    for (uint32_t i = 0u; i < segment_len; i++) {
        path[len + i] = segment[i];
    }
    path[len + segment_len] = '\0';
    return 1;
}

static int scheduler_command_name_needs_path(const char *name) {
    uint32_t i = 0u;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/' || name[0] == '.') {
        return 1;
    }
    while (name[i] != '\0') {
        if (name[i] == '/') {
            return 1;
        }
        i++;
    }
    return 0;
}

static int scheduler_resolve_process_path(const struct process *process,
                                          const char *input,
                                          char *out,
                                          uint32_t out_size) {
    uint32_t pos = 0u;

    if (input == 0 || out == 0 || out_size < 2u) {
        return 0;
    }
    if (input[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        pos = 1u;
    } else {
        copy_text(out, out_size, process != 0 ? process_cwd(process) : "/");
    }
    if (input[0] == '\0' || (input[0] == '.' && input[1] == '\0')) {
        return 1;
    }
    while (input[pos] != '\0') {
        char segment[NOS_NAME_BUFFER_SIZE];
        uint32_t seg_len = 0u;

        while (input[pos] == '/') {
            pos++;
        }
        if (input[pos] == '\0') {
            break;
        }
        while (input[pos] != '\0' && input[pos] != '/') {
            if (seg_len + 1u >= sizeof(segment)) {
                return 0;
            }
            segment[seg_len++] = input[pos++];
        }
        segment[seg_len] = '\0';
        if (seg_len == 1u && segment[0] == '.') {
            continue;
        }
        if (seg_len == 2u && segment[0] == '.' && segment[1] == '.') {
            if (!scheduler_path_pop_segment(out)) {
                return 0;
            }
            continue;
        }
        if (!scheduler_path_append_segment(out, out_size, segment, seg_len)) {
            return 0;
        }
    }
    return 1;
}

static void task_init(struct scheduler_task *task,
                      uint32_t entry,
                      uint32_t stack,
                      uint32_t id,
                      uint32_t root,
                      const char *name,
                      uint32_t parent_pid) {
    static const char prefix[] = "i386-task";

    task->ticks = 0;
    task->root = root;
    task->result = 0;
    task->heap_next = USER_HEAP_BASE;
    task->parent_pid = parent_pid;
    task->wait_pid = 0u;
    /* `stack` is the initial ESP, while growth is bounded by the ELF ABI
     * stack region shared by all i386 user images. */
    task->stack_top = I386_USER_STACK_TOP;
    task->stack_low = I386_USER_STACK_PAGE;
    task->stack_limit = I386_USER_STACK_BOTTOM;
    task->stack_grow_events = 0u;
    task->fpu_valid = 0u;
    task->address_space.kernel_cr3 = i386_paging_kernel_root();
    task->address_space.user_cr3 = root;
    task->address_space.reserved_phys_base = 0;
    task->address_space.reserved_phys_limit = 0;
    task->address_space.reserved_phys_next = 0;

    process_model_reset(&task->process, id - 1u, PROCESS_STATE_READY);
    i386_context_init_user(&task->process.context, entry, stack, id);
    task->process.pid = scheduler_next_pid++;
    if (name == 0 || name[0] == '\0') {
        name = prefix;
    }
    process_set_name(&task->process, name);
    task->process.image_kind = PROCESS_IMAGE_ELF;
    task->process.entry = entry;
    task->process.stack_top = stack;
    task->process.address_space = &task->address_space;
    file_init_console_in(&task->process.files[SYS_FD_STDIN], 0);
    file_init_console_out(&task->process.files[SYS_FD_STDOUT], 0);
    file_init_console_err(&task->process.files[SYS_FD_STDERR], 0);
    scheduler_refresh_console_from_stdio(&task->process);
}

int i386_scheduler_handle_page_fault(uint32_t fault_address,
                                     uint32_t error_code) {
    enum {
        PAGE_FAULT_PRESENT = 1u,
        PAGE_FAULT_USER = 4u
    };
    struct scheduler_task *task;
    uint32_t page;
    uint32_t map_page;
    uint32_t old_root;

    if (!scheduler_active ||
        (error_code & PAGE_FAULT_PRESENT) != 0u ||
        (error_code & PAGE_FAULT_USER) == 0u) {
        return 0;
    }
    task = &tasks[current_task_slot()];
    page = fault_address & ~(I386_PAGE_SIZE - 1u);
    if (task->stack_top == 0u ||
        page < task->stack_limit ||
        page >= task->stack_low) {
        return 0;
    }

    old_root = i386_paging_root();
    for (map_page = task->stack_low - I386_PAGE_SIZE;
         map_page >= page;
         map_page -= I386_PAGE_SIZE) {
        uint32_t frame = i386_pmm_alloc_page();
        void *temporary;

        if (frame == I386_PMM_INVALID_PAGE ||
            !i386_paging_map_page_in(task->root, map_page, frame, 1, 1)) {
            if (frame != I386_PMM_INVALID_PAGE) {
                (void)i386_pmm_free_page(frame);
            }
            i386_paging_switch(old_root);
            return 0;
        }

        i386_paging_switch(scheduler_kernel_root);
        if (!i386_paging_temporary_map(frame, 2u, &temporary)) {
            uint32_t ignored;

            (void)i386_paging_unmap_page_in(task->root, map_page, &ignored);
            (void)i386_pmm_free_page(frame);
            i386_paging_switch(old_root);
            return 0;
        }
        for (uint32_t i = 0u; i < I386_PAGE_SIZE; i++) {
            ((uint8_t *)temporary)[i] = 0u;
        }
        i386_paging_temporary_unmap(2u);
        if (map_page == page) {
            break;
        }
    }
    i386_paging_switch(old_root);
    task->stack_low = page;
    task->stack_grow_events++;
    return 1;
}

static void scheduler_reset(void) {
    scheduler_fpu_enable();
    for (uint32_t slot = 0u; slot < SCHEDULER_TASKS; slot++) {
        for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
            file_discard(&tasks[slot].process.files[fd]);
        }
    }
    for (uint32_t i = 0; i < SCHEDULER_TASKS; i++) {
        for (uint32_t j = 0; j < sizeof(tasks[i]); j++) {
            ((uint8_t *)&tasks[i])[j] = 0u;
        }
        tasks[i].process.state = PROCESS_STATE_FREE;
        task_processes[i] = &tasks[i].process;
    }
    scheduler_tick_count = 0u;
    scheduler_kernel_root = i386_paging_kernel_root();
    sched_runqueue_init(&runqueue, task_processes, SCHEDULER_TASKS);
}

static int scheduler_enter_first(void) {
    uint32_t slot = current_task_slot();

    scheduler_active = 1u;
    if (!sched_runqueue_start(&runqueue, 0u)) {
        scheduler_active = 0u;
        return 0;
    }
    slot = current_task_slot();
    i386_paging_switch(tasks[slot].root);
    scheduler_fpu_restore(slot);
    if (!i386_context_enter(&tasks[slot].process.context)) {
        i386_paging_switch(scheduler_kernel_root);
        scheduler_active = 0u;
        return 0;
    }
    return !scheduler_active &&
           sched_runqueue_completed_count(&runqueue) ==
               sched_runqueue_active_count(&runqueue);
}

int i386_scheduler_run(uint32_t entry0,
                       uint32_t stack0,
                       uint32_t root0,
                       uint32_t entry1,
                       uint32_t stack1,
                       uint32_t root1) {
    if (root0 == 0u || root1 == 0u || root0 == root1) {
        return 0;
    }
    scheduler_reset();
    task_init(&tasks[0], entry0, stack0, 1u, root0, "i386-task1", 0u);
    task_init(&tasks[1], entry1, stack1, 2u, root1, "i386-task2", 0u);
    if (!sched_runqueue_activate(&runqueue, 0u) ||
        !sched_runqueue_activate(&runqueue, 1u)) {
        return 0;
    }
    return scheduler_enter_first();
}

int i386_scheduler_run_one(uint32_t entry,
                           uint32_t stack,
                           uint32_t root,
                           const char *name) {
    int result;

    if (root == 0u) {
        return 0;
    }
    scheduler_reset();
    task_init(&tasks[0], entry, stack, 1u, root, name, 0u);
    if (!sched_runqueue_activate(&runqueue, 0u)) {
        return 0;
    }
    scheduler_quiet_tty_output = 0u;
    result = scheduler_enter_first();
    scheduler_quiet_tty_output = 0u;
    return result;
}

int i386_scheduler_run_one_quiet(uint32_t entry,
                                 uint32_t stack,
                                 uint32_t root,
                                 const char *name) {
    int result;

    if (root == 0u) {
        return 0;
    }
    scheduler_reset();
    task_init(&tasks[0], entry, stack, 1u, root, name, 0u);
    if (!sched_runqueue_activate(&runqueue, 0u)) {
        return 0;
    }
    scheduler_quiet_tty_output = 1u;
    result = scheduler_enter_first();
    scheduler_quiet_tty_output = 0u;
    return result;
}

int32_t i386_scheduler_spawn(uint32_t entry,
                             uint32_t stack,
                             uint32_t root,
                             const char *name) {
    uint32_t parent_pid;

    if (!scheduler_active || root == 0u) {
        return -1;
    }
    parent_pid = tasks[current_task_slot()].process.pid;
    for (uint32_t slot = 0; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_FREE) {
            char parent_cwd[NOS_PATH_BUFFER_SIZE];
            struct file inherited[NOS_PROCESS_FILE_MAX];
            struct process *parent = &tasks[current_task_slot()].process;

            copy_text(parent_cwd,
                      sizeof(parent_cwd),
                      parent->cwd_storage);
            if (!scheduler_clone_inheritable_files(parent, inherited)) {
                return -1;
            }
            task_init(&tasks[slot],
                      entry,
                      stack,
                      slot + 1u,
                      root,
                      name,
                      parent_pid);
            copy_text(tasks[slot].process.cwd_storage,
                      sizeof(tasks[slot].process.cwd_storage),
                      parent_cwd);
            scheduler_install_cloned_files(&tasks[slot].process, inherited);
            if (!sched_runqueue_activate(&runqueue, slot)) {
                task_reap(slot);
                return -1;
            }
            return (int32_t)tasks[slot].process.pid;
        }
    }
    return -1;
}

uintptr_t i386_scheduler_exec(const struct process_context *context,
                             uint32_t entry,
                             uint32_t stack,
                             uint32_t root,
                             const char *name) {
    struct scheduler_task *task;
    uint32_t pid;
    uint32_t slot;
    uint32_t parent_pid;
    char cwd[NOS_PATH_BUFFER_SIZE];
    struct file inherited[NOS_PROCESS_FILE_MAX];

    if (!scheduler_active || context == 0 || root == 0u) {
        return 0u;
    }
    task = &tasks[current_task_slot()];
    pid = task->process.pid;
    slot = task->process.slot;
    parent_pid = task->parent_pid;
    copy_text(cwd, sizeof(cwd), task->process.cwd_storage);
    if (!scheduler_clone_inheritable_files(&task->process, inherited)) {
        return 0u;
    }
    scheduler_discard_file_array(task->process.files);
    task_init(task,
              entry,
              stack,
              slot + 1u,
              root,
              name,
              parent_pid);
    task->process.pid = pid;
    copy_text(task->process.cwd_storage,
              sizeof(task->process.cwd_storage),
              cwd);
    scheduler_install_cloned_files(&task->process, inherited);
    task->process.state = PROCESS_STATE_RUNNING;
    scheduler_fpu_restore(slot);
    i386_paging_switch(root);
    return (uintptr_t)&task->process.context;
}

const struct process_context *i386_scheduler_tick(
    const struct process_context *context) {
    uint32_t previous;
    uint32_t next;

    if (!scheduler_active || context == 0 || !context->user_mode) {
        return context;
    }

    previous = current_task_slot();
    tasks[previous].process.context = *context;
    tasks[previous].ticks++;
    scheduler_tick_count++;
    scheduler_wake_sleepers();
    next = sched_runqueue_reschedule(&runqueue, SCHED_RUNQUEUE_PREEMPT);
    if (next == SCHED_RUNQUEUE_NONE) {
        return context;
    }
    if (next != previous) {
        scheduler_switch_fpu(previous, next);
        i386_paging_switch(tasks[next].root);
    }
    return &tasks[next].process.context;
}

uintptr_t i386_scheduler_exit(const struct process_context *context,
                              int exit_code) {
    uint32_t exiting;
    uint32_t next;
    int reap_after_reschedule = 0;

    if (!scheduler_active || context == 0) {
        return 0;
    }

    exiting = current_task_slot();
    tasks[exiting].process.context = *context;
    tasks[exiting].result = i386_context_task_result(context);
    tasks[exiting].process.exit_code = exit_code;
    tasks[exiting].process.state = PROCESS_STATE_EXITED;
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        file_discard(&tasks[exiting].process.files[fd]);
    }
    for (uint32_t slot = 0; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_WAITING &&
            (tasks[slot].wait_pid == 0u ||
             tasks[slot].wait_pid == tasks[exiting].process.pid)) {
            process_context_set_return_value(&tasks[slot].process.context,
                                             (uint32_t)exit_code);
            tasks[slot].wait_pid = 0u;
            tasks[slot].process.state = PROCESS_STATE_READY;
            reap_after_reschedule = 1;
        }
    }
    next = sched_runqueue_reschedule(&runqueue, SCHED_RUNQUEUE_EXIT);
    if (reap_after_reschedule) {
        task_reap(exiting);
    }
    if (sched_runqueue_completed_count(&runqueue) ==
        sched_runqueue_active_count(&runqueue)) {
        scheduler_active = 0;
        i386_paging_switch(scheduler_kernel_root);
        return 1;
    }

    if (next == SCHED_RUNQUEUE_NONE) {
        scheduler_active = 0;
        i386_paging_switch(scheduler_kernel_root);
        return 1;
    }
    scheduler_fpu_restore(next);
    i386_paging_switch(tasks[next].root);
    return (uintptr_t)&tasks[next].process.context;
}

uintptr_t i386_scheduler_fault_exit(const struct process_context *context,
                                    int exit_code) {
    return i386_scheduler_exit(context, exit_code);
}

uintptr_t i386_scheduler_wait(const struct process_context *context,
                             uint32_t pid,
                             int32_t *status,
                             int *blocked) {
    uint32_t next;
    uint32_t current_pid;
    uint32_t waiting;

    if (!scheduler_active || context == 0 || status == 0 || blocked == 0) {
        return 0u;
    }
    *blocked = 0;
    waiting = current_task_slot();
    current_pid = tasks[waiting].process.pid;
    for (uint32_t slot = 0; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.pid == pid &&
            tasks[slot].parent_pid == current_pid) {
            if (tasks[slot].process.state == PROCESS_STATE_EXITED) {
                *status = tasks[slot].process.exit_code;
                task_reap(slot);
                return 0u;
            }
            tasks[waiting].process.context = *context;
            tasks[waiting].wait_pid = pid;
            next = sched_runqueue_reschedule(&runqueue,
                                             SCHED_RUNQUEUE_BLOCK);
            if (next == waiting) {
                return 0u;
            }
            if (next == SCHED_RUNQUEUE_NONE) {
                return 0u;
            }
            scheduler_switch_fpu(waiting, next);
            i386_paging_switch(tasks[next].root);
            *blocked = 1;
            return (uintptr_t)&tasks[next].process.context;
        }
    }
    *status = -1;
    return 0u;
}

int32_t i386_scheduler_kill(uint32_t pid) {
    uint32_t current_pid;

    if (!scheduler_active || pid == 0u) {
        return -1;
    }
    current_pid = tasks[current_task_slot()].process.pid;
    for (uint32_t slot = 0u; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.pid == pid &&
            tasks[slot].parent_pid == current_pid &&
            tasks[slot].process.state != PROCESS_STATE_FREE &&
            tasks[slot].process.state != PROCESS_STATE_EXITED) {
            tasks[slot].process.exit_code = -9;
            tasks[slot].process.state = PROCESS_STATE_EXITED;
            tasks[slot].wait_pid = 0u;
            tasks[slot].process.wake_tick = 0u;
            runqueue.completed++;
            for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
                file_discard(&tasks[slot].process.files[fd]);
            }
            for (uint32_t waiter = 0u; waiter < SCHEDULER_TASKS; waiter++) {
                if (tasks[waiter].process.state == PROCESS_STATE_WAITING &&
                    tasks[waiter].wait_pid == pid) {
                    process_context_set_return_value(
                        &tasks[waiter].process.context,
                        (uint32_t)-9);
                    tasks[waiter].wait_pid = 0u;
                    tasks[waiter].process.state = PROCESS_STATE_READY;
                    task_reap(slot);
                }
            }
            return 1;
        }
    }
    return 0;
}

uintptr_t i386_scheduler_yield(const struct process_context *context) {
    uint32_t yielding;
    uint32_t next;

    if (!scheduler_active || context == 0) {
        return 0;
    }
    yielding = current_task_slot();
    tasks[yielding].process.context = *context;
    next = sched_runqueue_reschedule(&runqueue, SCHED_RUNQUEUE_YIELD);
    if (next == SCHED_RUNQUEUE_NONE) {
        return 0u;
    }
    if (next != yielding) {
        scheduler_switch_fpu(yielding, next);
        i386_paging_switch(tasks[next].root);
    }
    return (uintptr_t)&tasks[next].process.context;
}

uintptr_t i386_scheduler_sleep(const struct process_context *context,
                               uint32_t ticks) {
    uint32_t sleeping;
    uint32_t next;

    if (!scheduler_active || context == 0) {
        return 0u;
    }
    if (ticks == 0u) {
        return i386_scheduler_yield(context);
    }
    sleeping = current_task_slot();
    tasks[sleeping].process.context = *context;
    tasks[sleeping].process.wake_tick = scheduler_tick_count + ticks;
    if (tasks[sleeping].process.wake_tick == 0u) {
        tasks[sleeping].process.wake_tick = 1u;
    }
    next = sched_runqueue_reschedule(&runqueue, SCHED_RUNQUEUE_SLEEP);
    if (next == SCHED_RUNQUEUE_NONE || next == sleeping) {
        tasks[sleeping].process.wake_tick = 0u;
        tasks[sleeping].process.state = PROCESS_STATE_RUNNING;
        return 0u;
    }
    scheduler_switch_fpu(sleeping, next);
    i386_paging_switch(tasks[next].root);
    return (uintptr_t)&tasks[next].process.context;
}

uint32_t i386_scheduler_ticks(void) {
    return scheduler_tick_count;
}

uint32_t i386_scheduler_switches(void) {
    return sched_runqueue_switch_count(&runqueue);
}

uint32_t i386_scheduler_completed(void) {
    return sched_runqueue_completed_count(&runqueue);
}

uint32_t i386_scheduler_task_ticks(uint32_t task) {
    if (task >= SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].ticks;
}

uint32_t i386_scheduler_task_root(uint32_t task) {
    if (task >= SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].root;
}

uint32_t i386_scheduler_task_result(uint32_t task) {
    if (task >= SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].result;
}

uint32_t i386_scheduler_current_pid(void) {
    if (!scheduler_active) {
        return 0;
    }
    return tasks[current_task_slot()].process.pid;
}

int i386_scheduler_resolve_exec_command_line(char *line, uint32_t line_size) {
    char token[NOS_PATH_BUFFER_SIZE];
    char resolved[NOS_PATH_BUFFER_SIZE];
    char original[NOS_TTY_LINE_BUFFER_SIZE];
    uint32_t rest_pos = 0u;
    uint32_t token_len = 0u;
    uint32_t resolved_len;
    uint32_t rest_len;
    const struct process *process = 0;

    if (line == 0 || line_size == 0u) {
        return 0;
    }
    if (scheduler_active) {
        process = &tasks[current_task_slot()].process;
    }
    while (line[rest_pos] == ' ' || line[rest_pos] == '\t') {
        rest_pos++;
    }
    copy_text(original, sizeof(original), line);
    while (line[rest_pos + token_len] != '\0' &&
           line[rest_pos + token_len] != ' ' &&
           line[rest_pos + token_len] != '\t') {
        if (token_len + 1u >= sizeof(token)) {
            return 0;
        }
        token[token_len] = line[rest_pos + token_len];
        token_len++;
    }
    token[token_len] = '\0';
    if (token_len == 0u || !scheduler_command_name_needs_path(token)) {
        return 1;
    }
    if (!scheduler_resolve_process_path(process, token, resolved, sizeof(resolved))) {
        return 0;
    }
    resolved_len = scheduler_text_len(resolved);
    rest_len = scheduler_text_len(line + rest_pos + token_len);
    if (rest_pos + resolved_len + rest_len + 1u > line_size) {
        return 0;
    }
    for (uint32_t i = 0u; i < resolved_len; i++) {
        line[rest_pos + i] = resolved[i];
    }
    for (uint32_t i = 0u; i <= rest_len; i++) {
        line[rest_pos + resolved_len + i] =
            original[rest_pos + token_len + i];
    }
    return 1;
}

uint32_t i386_scheduler_page_alloc_with_prot(int writable) {
    struct scheduler_task *task;
    uint32_t kernel_root;

    if (!scheduler_active) {
        return 0u;
    }
    task = &tasks[current_task_slot()];
    kernel_root = i386_paging_kernel_root();
    i386_paging_switch(kernel_root);
    while (task->heap_next < USER_HEAP_LIMIT) {
        uint32_t address = task->heap_next;
        uint32_t existing;
        uint32_t frame;

        task->heap_next += I386_PAGE_SIZE;
        if (i386_paging_translate_in(task->root, address, &existing)) {
            continue;
        }
        frame = i386_pmm_alloc_page();
        if (frame == I386_PMM_INVALID_PAGE ||
            !i386_paging_map_page_in(task->root,
                                     address,
                                     frame,
                                     writable ? 1 : 0,
                                     1)) {
            if (frame != I386_PMM_INVALID_PAGE) {
                (void)i386_pmm_free_page(frame);
            }
            i386_paging_switch(task->root);
            return 0u;
        }
        {
            void *temporary;

            if (!i386_paging_temporary_map(frame, 2u, &temporary)) {
                uint32_t ignored;

                (void)i386_paging_unmap_page_in(task->root, address, &ignored);
                (void)i386_pmm_free_page(frame);
                i386_paging_switch(task->root);
                return 0u;
            }
            for (uint32_t i = 0; i < I386_PAGE_SIZE; i++) {
                ((uint8_t *)temporary)[i] = 0u;
            }
            i386_paging_temporary_unmap(2u);
        }
        i386_paging_switch(task->root);
        return address;
    }
    i386_paging_switch(task->root);
    return 0u;
}

uint32_t i386_scheduler_page_alloc(void) {
    return i386_scheduler_page_alloc_with_prot(1);
}

uint32_t i386_scheduler_page_alloc_at(uint32_t user_page, int writable) {
    struct scheduler_task *task;
    uint32_t frame;
    uint32_t existing;
    void *temporary;

    if (!scheduler_active ||
        user_page < USER_HEAP_BASE || user_page >= USER_HEAP_LIMIT ||
        (user_page & (I386_PAGE_SIZE - 1u)) != 0u) {
        return 0u;
    }
    task = &tasks[current_task_slot()];
    if (i386_paging_translate_in(task->root, user_page, &existing)) {
        return 0u;
    }
    frame = i386_pmm_alloc_page();
    if (frame == I386_PMM_INVALID_PAGE) {
        return 0u;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_map_page_in(task->root,
                                 user_page,
                                 frame,
                                 writable ? 1 : 0,
                                 1) ||
        !i386_paging_temporary_map(frame, 2u, &temporary)) {
        uint32_t ignored;

        (void)i386_paging_unmap_page_in(task->root, user_page, &ignored);
        (void)i386_pmm_free_page(frame);
        i386_paging_switch(task->root);
        return 0u;
    }
    memset(temporary, 0, I386_PAGE_SIZE);
    i386_paging_temporary_unmap(2u);
    i386_paging_switch(task->root);
    return user_page;
}

int32_t i386_scheduler_page_protect(uint32_t user_page, int writable) {
    struct scheduler_task *task;
    int ok;

    if (!scheduler_active ||
        user_page < USER_HEAP_BASE ||
        user_page >= USER_HEAP_LIMIT ||
        (user_page & (I386_PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }
    task = &tasks[current_task_slot()];
    i386_paging_switch(i386_paging_kernel_root());
    ok = i386_paging_protect_page_in(task->root,
                                     user_page,
                                     writable ? 1 : 0,
                                     1);
    i386_paging_switch(task->root);
    return ok ? 0 : -1;
}

uint32_t i386_scheduler_shared_page_alloc(void) {
    uint32_t frame;
    uint32_t current_root;
    void *temporary;

    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    frame = i386_pmm_alloc_page();
    if (frame == I386_PMM_INVALID_PAGE) {
        i386_paging_switch(current_root);
        return 0u;
    }
    if (!i386_paging_temporary_map(frame, 2u, &temporary)) {
        (void)i386_pmm_free_page(frame);
        i386_paging_switch(current_root);
        return 0u;
    }
    for (uint32_t i = 0; i < I386_PAGE_SIZE; i++) {
        ((uint8_t *)temporary)[i] = 0u;
    }
    i386_paging_temporary_unmap(2u);
    i386_paging_switch(current_root);
    return frame;
}

int32_t i386_scheduler_shared_page_free(uint32_t frame) {
    uint32_t current_root;
    int ok;

    if (frame == 0u || (frame & (I386_PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    ok = i386_pmm_free_page(frame);
    i386_paging_switch(current_root);
    return ok ? 0 : -1;
}

uint32_t i386_scheduler_shared_page_map(uint32_t frame) {
    struct scheduler_task *task;
    uint32_t kernel_root;

    if (!scheduler_active ||
        frame == 0u ||
        (frame & (I386_PAGE_SIZE - 1u)) != 0u) {
        return 0u;
    }
    task = &tasks[current_task_slot()];
    kernel_root = i386_paging_kernel_root();
    i386_paging_switch(kernel_root);
    while (task->heap_next < USER_HEAP_LIMIT) {
        uint32_t address = task->heap_next;
        uint32_t existing;

        task->heap_next += I386_PAGE_SIZE;
        if (i386_paging_translate_in(task->root, address, &existing)) {
            continue;
        }
        if (!i386_paging_map_page_in(task->root, address, frame, 1, 1)) {
            i386_paging_switch(task->root);
            return 0u;
        }
        i386_paging_switch(task->root);
        return address;
    }
    i386_paging_switch(task->root);
    return 0u;
}

int32_t i386_scheduler_shared_page_unmap(uint32_t user_page) {
    struct scheduler_task *task;
    uint32_t frame;

    if (!scheduler_active ||
        user_page < USER_HEAP_BASE ||
        user_page >= USER_HEAP_LIMIT ||
        (user_page & (I386_PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }
    task = &tasks[current_task_slot()];
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_unmap_page_in(task->root, user_page, &frame)) {
        i386_paging_switch(task->root);
        return -1;
    }
    if (user_page < task->heap_next) {
        task->heap_next = user_page;
    }
    i386_paging_switch(task->root);
    return 0;
}

int32_t i386_scheduler_page_free(uint32_t user_page) {
    struct scheduler_task *task;
    uint32_t frame;

    if (!scheduler_active ||
        user_page < USER_HEAP_BASE ||
        user_page >= USER_HEAP_LIMIT ||
        (user_page & (I386_PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }
    task = &tasks[current_task_slot()];
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_unmap_page_in(task->root, user_page, &frame) ||
        !i386_pmm_free_page(frame)) {
        i386_paging_switch(task->root);
        return -1;
    }
    if (user_page < task->heap_next) {
        task->heap_next = user_page;
    }
    i386_paging_switch(task->root);
    return 0;
}

int32_t i386_scheduler_open(struct vfs *vfs,
                            const char *path,
                            uint32_t flags) {
    struct process *process;
    struct vfs_node node;
    struct file *opened_file;
    char resolved[NOS_PATH_BUFFER_SIZE];
    uint32_t vfs_flags = 0u;
    uint32_t access;
    uint32_t fd;

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    access = flags & (SYS_OPEN_READ | SYS_OPEN_WRITE);
    if (flags == 0u) {
        access = SYS_OPEN_READ;
    } else if (access == 0u &&
               (flags & (SYS_OPEN_CREAT | SYS_OPEN_TRUNC | SYS_OPEN_APPEND)) != 0u) {
        access = SYS_OPEN_WRITE;
    }
    if (access == 0u ||
        (flags & ~(SYS_OPEN_CREAT |
                   SYS_OPEN_TRUNC |
                   SYS_OPEN_APPEND |
                   SYS_OPEN_READ |
                   SYS_OPEN_WRITE)) != 0u ||
        !fs_resolve_process_path(process, path, resolved, sizeof(resolved))) {
        return -1;
    }
    if ((flags & SYS_OPEN_CREAT) != 0u) {
        vfs_flags |= VFS_OPEN_CREATE;
    }
    if ((flags & SYS_OPEN_TRUNC) != 0u) {
        vfs_flags |= VFS_OPEN_TRUNCATE;
    }
    if ((flags & SYS_OPEN_APPEND) != 0u) {
        vfs_flags |= VFS_OPEN_APPEND;
    }
    if (vfs_open(vfs, resolved, vfs_flags, &node) != 0) {
        return -1;
    }
    if (node.mount_kind == VFS_MOUNT_DEVFS) {
        if (node.aux_index == VFS_DEV_TTY ||
            node.aux_index == VFS_DEV_TTY2 ||
            node.aux_index == VFS_DEV_TTY3) {
            access = SYS_OPEN_READ | SYS_OPEN_WRITE;
        } else if (node.aux_index == VFS_DEV_STDIN) {
            access = SYS_OPEN_READ;
        } else if (node.aux_index == VFS_DEV_STDOUT ||
                   node.aux_index == VFS_DEV_STDERR) {
            access = SYS_OPEN_WRITE;
        }
    }
    if (!file_table_open_vfs(process->files,
                             NOS_PROCESS_FILE_MAX,
                             3u,
                             &node,
                             resolved,
                             0,
                             &fd,
                             &opened_file)) {
        return -1;
    }
    opened_file->flags =
        (access & SYS_OPEN_READ ? KERNEL_FILE_ACCESS_READ : 0u) |
        (access & SYS_OPEN_WRITE ? KERNEL_FILE_ACCESS_WRITE : 0u);
    if ((flags & SYS_OPEN_APPEND) != 0u) {
        file_set_offset(opened_file, node.handle.fat32_file.size);
    }
    return (int32_t)fd;
}

int32_t i386_scheduler_opendir(struct vfs *vfs, const char *path) {
    struct process *process;
    struct vfs_node node;
    char resolved[NOS_PATH_BUFFER_SIZE];
    uint32_t fd;

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    if (!fs_resolve_process_path(process, path, resolved, sizeof(resolved)) ||
        vfs_opendir(vfs, resolved, &node) != 0) {
        return -1;
    }
    return file_table_open_vfs(process->files,
                               NOS_PROCESS_FILE_MAX,
                               3u,
                               &node,
                               resolved,
                               0,
                               &fd,
                               0)
        ? (int32_t)fd
        : -1;
}

int32_t i386_scheduler_readdir(struct vfs *vfs,
                               uint32_t fd,
                               struct syscall_dirent *entry) {
    struct file *file;
    struct vfs_dirent vfs_entry;
    int64_t result;

    if (!scheduler_active || vfs == 0 || entry == 0 ||
        fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &tasks[current_task_slot()].process.files[fd];
    if (file->kind != KERNEL_FILE_VFS ||
        file->vfs_node.kind != VFS_NODE_DIR) {
        return -1;
    }
    result = file_readdir(file, vfs, &vfs_entry);
    if (result <= 0) {
        return (int32_t)result;
    }
    copy_text(entry->name, sizeof(entry->name), vfs_entry.name);
    entry->size = vfs_entry.size;
    entry->attributes = vfs_entry.attributes;
    return 1;
}

int32_t i386_scheduler_chdir(struct vfs *vfs, const char *path) {
    struct process *process;
    struct vfs_node directory;
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    if (!fs_resolve_process_path(process, path, resolved, sizeof(resolved)) ||
        vfs_opendir(vfs, resolved, &directory) != 0) {
        return -1;
    }
    process_set_cwd(process, resolved);
    return 0;
}

int32_t i386_scheduler_getcwd(char *buffer, uint32_t size) {
    const char *cwd;
    uint32_t length = 0u;

    if (!scheduler_active || buffer == 0 || size == 0u) {
        return -1;
    }
    cwd = process_cwd(&tasks[current_task_slot()].process);
    while (cwd[length] != '\0') {
        length++;
    }
    if (length + 1u > size) {
        return -1;
    }
    copy_text(buffer, size, cwd);
    return 0;
}

int32_t i386_scheduler_mkdir(struct vfs *vfs, const char *path) {
    struct process *process;
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    if (!fs_resolve_process_path(process, path, resolved, sizeof(resolved))) {
        return -1;
    }
    return vfs_mkdir(vfs, resolved) == 0 ? 0 : -1;
}

int32_t i386_scheduler_rmdir(struct vfs *vfs, const char *path) {
    struct process *process;
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    if (!fs_resolve_process_path(process, path, resolved, sizeof(resolved))) {
        return -1;
    }
    return vfs_rmdir(vfs, resolved) == 0 ? 0 : -1;
}

int32_t i386_scheduler_remove(struct vfs *vfs, const char *path) {
    struct process *process;
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    if (!fs_resolve_process_path(process, path, resolved, sizeof(resolved))) {
        return -1;
    }
    return vfs_unlink(vfs, resolved) == 0 ? 0 : -1;
}

int32_t i386_scheduler_read(struct vfs *vfs,
                            uint32_t fd,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags) {
    struct file *file;
    int64_t result;

    if (!scheduler_active || buffer == 0 ||
        fd >= NOS_PROCESS_FILE_MAX || size == 0u ||
        (flags & ~(SYS_READ_NONBLOCK | SYS_READ_CHAR)) != 0u) {
        return -1;
    }
    file = &tasks[current_task_slot()].process.files[fd];
    if (file->kind == KERNEL_FILE_TTY_STDIN) {
        return -2;
    }
    if (file->kind == KERNEL_FILE_VFS &&
        (file->flags & KERNEL_FILE_ACCESS_READ) == 0u) {
        return -1;
    }
    result = file_read(file,
                       vfs,
                       buffer,
                       size,
                       (flags & SYS_READ_NONBLOCK
                            ? KERNEL_FILE_READ_NONBLOCK
                            : KERNEL_FILE_READ_BLOCKING) |
                           (flags & SYS_READ_CHAR
                                ? KERNEL_FILE_READ_CHAR
                                : 0u));
    if (result == KERNEL_FILE_IO_WOULD_BLOCK) {
        return -2;
    }
    return result < 0 || result > 0x7fffffffu ? -1 : (int32_t)result;
}

int32_t i386_scheduler_write(struct vfs *vfs,
                             uint32_t fd,
                             const void *buffer,
                             uint32_t size) {
    struct file *file;
    int64_t result;

    if (!scheduler_active || buffer == 0 ||
        fd >= NOS_PROCESS_FILE_MAX || size == 0u) {
        return -1;
    }
    file = &tasks[current_task_slot()].process.files[fd];
    if (file->kind == KERNEL_FILE_TTY_STDOUT ||
        file->kind == KERNEL_FILE_TTY_STDERR) {
        if (scheduler_quiet_tty_output != 0u) {
            return (int32_t)size;
        }
        return -2;
    }
    if (file->kind == KERNEL_FILE_VFS &&
        (file->flags & KERNEL_FILE_ACCESS_WRITE) == 0u) {
        return -1;
    }
    result = file_write(file, vfs, buffer, size);
    if (result == KERNEL_FILE_IO_WOULD_BLOCK) {
        return -2;
    }
    return result < 0 || result > 0x7fffffffu ? -1 : (int32_t)result;
}

int32_t i386_scheduler_seek(uint32_t fd,
                            int32_t offset,
                            uint32_t whence) {
    struct file *file;
    int64_t base;
    int64_t next;

    if (!scheduler_active || fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &tasks[current_task_slot()].process.files[fd];
    if (file->kind != KERNEL_FILE_VFS ||
        file->vfs_node.kind != VFS_NODE_FILE) {
        return -1;
    }
    if (whence == SYS_SEEK_SET) {
        base = 0;
    } else if (whence == SYS_SEEK_CUR) {
        base = file->offset;
    } else if (whence == SYS_SEEK_END) {
        base = scheduler_vfs_node_size(&file->vfs_node);
    } else {
        return -1;
    }
    next = base + offset;
    if (next < 0 || next > 0x7fffffffll) {
        return -1;
    }
    file_set_offset(file, (uint32_t)next);
    return (int32_t)next;
}

int32_t i386_scheduler_pipe(uint32_t pair[2]) {
    if (!scheduler_active || pair == 0) {
        return -1;
    }
    return file_table_open_pipe_pair(
                                     tasks[current_task_slot()].process.files,
                                     NOS_PROCESS_FILE_MAX,
                                     3u,
                                     pair)
        ? 0
        : -1;
}

int32_t i386_scheduler_dup2(uint32_t src_fd, uint32_t dst_fd) {
    struct process *process;

    if (!scheduler_active ||
        src_fd >= NOS_PROCESS_FILE_MAX ||
        dst_fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    process = &tasks[current_task_slot()].process;
    if (process->files[src_fd].kind == KERNEL_FILE_NONE) {
        return -1;
    }
    if (src_fd == dst_fd) {
        return (int32_t)dst_fd;
    }
    if (!file_clone(&process->files[dst_fd],
                    &process->files[src_fd])) {
        return -1;
    }
    process->files[dst_fd].flags &= (uint8_t)~KERNEL_FILE_CLOSE_ON_SPAWN;
    return (int32_t)dst_fd;
}

uint32_t i386_scheduler_fd_kind(uint32_t fd) {
    if (!scheduler_active || fd >= NOS_PROCESS_FILE_MAX) {
        return KERNEL_FILE_NONE;
    }
    return tasks[current_task_slot()].process.files[fd].kind;
}

int32_t i386_scheduler_fd_query(uint32_t fd, struct syscall_fd_info *info) {
    const struct file *file;

    if (!scheduler_active || info == 0 || fd >= NOS_PROCESS_FILE_MAX) {
        return 0;
    }
    file = &tasks[current_task_slot()].process.files[fd];
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    info->fd = fd;
    info->kind = file->kind;
    info->flags = file->flags;
    info->offset = file->offset;
    info->node_kind = file->vfs_node.kind;
    info->mount_kind = file->vfs_node.mount_kind;
    info->readable =
        file->kind == KERNEL_FILE_TTY_STDIN ||
        file->kind == KERNEL_FILE_PIPE_READ ||
        ((file->flags & KERNEL_FILE_ACCESS_READ) != 0u);
    info->writable =
        file->kind == KERNEL_FILE_TTY_STDOUT ||
        file->kind == KERNEL_FILE_TTY_STDERR ||
        file->kind == KERNEL_FILE_PIPE_WRITE ||
        ((file->flags & KERNEL_FILE_ACCESS_WRITE) != 0u);
    copy_text(info->path, sizeof(info->path), file->opened_path);
    return file->kind != KERNEL_FILE_NONE ? 1 : 0;
}

int32_t i386_scheduler_close(uint32_t fd) {
    struct file *file;

    if (!scheduler_active || fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &tasks[current_task_slot()].process.files[fd];
    if (file->kind == KERNEL_FILE_NONE) {
        return -1;
    }
    return (int32_t)file_close(file);
}

int i386_scheduler_process_snapshot(uint32_t task,
                                    struct process_snapshot *snapshot) {
    const struct process *process;

    if (task >= SCHEDULER_TASKS || snapshot == 0) {
        return 0;
    }
    process = &tasks[task].process;
    process_snapshot_fill(snapshot, process);
    return 1;
}

const struct process *process_current(void) {
    uint32_t slot;

    if (!scheduler_active) {
        return 0;
    }
    slot = current_task_slot();
    if (slot >= SCHEDULER_TASKS ||
        tasks[slot].process.state == PROCESS_STATE_FREE ||
        tasks[slot].process.image_kind == PROCESS_IMAGE_NONE) {
        return 0;
    }
    return &tasks[slot].process;
}

struct process *process_current_mut(void) {
    uint32_t slot;

    if (!scheduler_active) {
        return 0;
    }
    slot = current_task_slot();
    if (slot >= SCHEDULER_TASKS ||
        tasks[slot].process.state == PROCESS_STATE_FREE ||
        tasks[slot].process.image_kind == PROCESS_IMAGE_NONE) {
        return 0;
    }
    return &tasks[slot].process;
}

uint32_t process_capacity(void) {
    return SCHEDULER_TASKS;
}

int process_get(uint32_t slot, struct process_snapshot *out) {
    struct process_snapshot snapshot;

    if (slot >= SCHEDULER_TASKS || out == 0 ||
        !i386_scheduler_process_snapshot(slot, &snapshot) ||
        snapshot.state == PROCESS_STATE_FREE) {
        return 0;
    }
    *out = snapshot;
    return 1;
}
