#include "abi/syscall_abi.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "kernel/public/proc/sched_policy.h"
#include "paging.h"
#include "scheduler_internal.h"
#include "user.h"
#include "lib/string.h"

static void *scheduler_console_handle;

void i386_scheduler_set_console_handle(void *handle) {
    scheduler_console_handle = handle;
}

static void scheduler_run_destroy_root(uint32_t root) {
    uint32_t current_root;

    if (root == 0u || root == i386_paging_kernel_root()) {
        return;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    i386_paging_destroy_user_space(root);
    if (current_root != root) {
        i386_paging_switch(current_root);
    }
}

static void scheduler_run_destroy_address_space(struct scheduler_task *task) {
    uint32_t root;

    if (task == 0) {
        return;
    }
    root = task->root;
    task->root = 0u;
    task->address_space.user_cr3 = 0u;
    task->process.address_space = &task->address_space;
    scheduler_run_destroy_root(root);
}

static void scheduler_run_refresh_console_from_stdio(struct process *process) {
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

void i386_scheduler_task_init(struct scheduler_task *task,
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
    task->heap_next = I386_USER_HEAP_BASE;
    task->stack_top = I386_USER_STACK_TOP;
    task->stack_low = I386_USER_STACK_PAGE;
    task->stack_limit = I386_USER_STACK_BOTTOM;
    task->stack_grow_events = 0u;
    memset(task->mappings, 0, sizeof(task->mappings));
    task->fpu_valid = 0u;
    task->address_space.kernel_cr3 = i386_paging_kernel_root();
    task->address_space.user_cr3 = root;
    task->address_space.reserved_phys_base = 0;
    task->address_space.reserved_phys_limit = 0;
    task->address_space.reserved_phys_next = 0;

    process_model_reset(&task->process, id - 1u, PROCESS_STATE_READY);
    task->process.parent.pid = parent_pid;
    process_lifecycle_clear_wait(&task->process);
    i386_scheduler_backend_init_user_context(task, entry, stack, id);
    task->process.pid = scheduler_next_pid++;
    if (scheduler_console_handle != 0) {
        tty_set_foreground_pid((struct tty *)scheduler_console_handle,
                               task->process.pid);
    }
    if (name == 0 || name[0] == '\0') {
        name = prefix;
    }
    process_set_name(&task->process, name);
    task->process.image_kind = PROCESS_IMAGE_ELF;
    task->process.entry = entry;
    task->process.stack_top = stack;
    task->process.address_space = &task->address_space;
    task->process.console_handle = scheduler_console_handle;
    file_init_console_in(&task->process.files[SYS_FD_STDIN],
                         scheduler_console_handle);
    file_init_console_out(&task->process.files[SYS_FD_STDOUT],
                          scheduler_console_handle);
    file_init_console_err(&task->process.files[SYS_FD_STDERR],
                          scheduler_console_handle);
    scheduler_run_refresh_console_from_stdio(&task->process);
}

static void scheduler_reset(void) {
    i386_scheduler_fpu_enable();
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
            file_discard(&tasks[slot].process.files[fd]);
        }
        scheduler_run_destroy_address_space(&tasks[slot]);
    }
    for (uint32_t i = 0; i < I386_SCHEDULER_TASKS; i++) {
        for (uint32_t j = 0; j < sizeof(tasks[i]); j++) {
            ((uint8_t *)&tasks[i])[j] = 0u;
        }
        tasks[i].process.state = PROCESS_STATE_FREE;
        task_processes[i] = &tasks[i].process;
    }
    scheduler_tick_count = 0u;
    scheduler_kernel_root = i386_paging_kernel_root();
    sched_runqueue_init(&runqueue, task_processes, I386_SCHEDULER_TASKS);
    sched_policy_init();
    sched_policy_bind_slot_view(task_processes,
                                I386_SCHEDULER_TASKS,
                                &runqueue.current);
}

static int scheduler_enter_first(void) {
    uint32_t slot;

    scheduler_active = 1u;
    if (!sched_runqueue_start(&runqueue, 0u)) {
        scheduler_active = 0u;
        return 0;
    }
    slot = i386_scheduler_current_slot();
    if (!i386_scheduler_backend_enter_task(slot)) {
        i386_scheduler_backend_switch_to_kernel();
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
    i386_scheduler_task_init(&tasks[0], entry0, stack0, 1u, root0, "i386-task1", 0u);
    i386_scheduler_task_init(&tasks[1], entry1, stack1, 2u, root1, "i386-task2", 0u);
    if (!sched_runqueue_activate(&runqueue, 0u) ||
        !sched_runqueue_activate(&runqueue, 1u)) {
        i386_scheduler_task_release(0u, 0);
        i386_scheduler_task_release(1u, 0);
        return 0;
    }
    return scheduler_enter_first();
}

static int i386_scheduler_run_one(uint32_t entry,
                                  uint32_t stack,
                                  uint32_t root,
                                  const char *name) {
    int result;

    if (root == 0u) {
        return 0;
    }
    scheduler_reset();
    i386_scheduler_task_init(&tasks[0], entry, stack, 1u, root, name, 0u);
    if (!sched_runqueue_activate(&runqueue, 0u)) {
        i386_scheduler_task_release(0u, 0);
        return 0;
    }
    scheduler_quiet_tty_output = 0u;
    result = scheduler_enter_first();
    scheduler_quiet_tty_output = 0u;
    if (!result) {
        i386_scheduler_task_release(0u, 0);
    }
    return result;
}

static int i386_scheduler_run_one_quiet(uint32_t entry,
                                        uint32_t stack,
                                        uint32_t root,
                                        const char *name) {
    int result;

    if (root == 0u) {
        return 0;
    }
    scheduler_reset();
    i386_scheduler_task_init(&tasks[0], entry, stack, 1u, root, name, 0u);
    if (!sched_runqueue_activate(&runqueue, 0u)) {
        i386_scheduler_task_release(0u, 0);
        return 0;
    }
    scheduler_quiet_tty_output = 1u;
    result = scheduler_enter_first();
    scheduler_quiet_tty_output = 0u;
    if (!result) {
        i386_scheduler_task_release(0u, 0);
    }
    return result;
}

int i386_scheduler_run_loaded(const struct process_loaded_image *image,
                              struct process_snapshot *snapshot) {
    if (image == 0 ||
        snapshot == 0 ||
        image->entry > 0xffffffffu ||
        image->stack > 0xffffffffu ||
        image->root > 0xffffffffu) {
        return 0;
    }
    if (!i386_scheduler_run_one((uint32_t)image->entry,
                                (uint32_t)image->stack,
                                (uint32_t)image->root,
                                image->name) ||
        !i386_scheduler_process_snapshot(0u, snapshot)) {
        kprint("scheduler: run_loaded failed name=%s entry=%x stack=%x root=%x\n",
               image->name != 0 ? image->name : "(null)",
               (uint32_t)image->entry,
               (uint32_t)image->stack,
               (uint32_t)image->root);
        return 0;
    }
    if (snapshot->state == PROCESS_STATE_EXITED) {
        (void)process_scheduler_reap_exited_pid(snapshot->pid);
    }
    return snapshot->state == PROCESS_STATE_EXITED;
}

int i386_scheduler_run_loaded_quiet(const struct process_loaded_image *image,
                                    struct process_snapshot *snapshot) {
    if (image == 0 ||
        snapshot == 0 ||
        image->entry > 0xffffffffu ||
        image->stack > 0xffffffffu ||
        image->root > 0xffffffffu) {
        return 0;
    }
    if (!i386_scheduler_run_one_quiet((uint32_t)image->entry,
                                      (uint32_t)image->stack,
                                      (uint32_t)image->root,
                                      image->name) ||
        !i386_scheduler_process_snapshot(0u, snapshot)) {
        return 0;
    }
    if (snapshot->state == PROCESS_STATE_EXITED) {
        (void)process_scheduler_reap_exited_pid(snapshot->pid);
    }
    return snapshot->state == PROCESS_STATE_EXITED;
}
