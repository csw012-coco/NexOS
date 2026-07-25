#include "abi/syscall_abi.h"
#include "context.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/mem/address_space_internal.h"
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/proc/process_mm_ops.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "kernel/public/proc/sched_policy.h"
#include "paging.h"
#include "pmm.h"
#include "process32.h"
#include "scheduler_internal.h"
#include "user.h"
#include "lib/string.h"

int kernel_i386_syscall_page_is_shared(uint32_t pid, uint32_t user_page)
    __attribute__((weak));

struct scheduler_shared_page_query {
    uint32_t pid;
};

static uint32_t scheduler_process_current_slot(void) {
    return sched_runqueue_current_slot(&runqueue);
}

static int scheduler_process_shared_page_query(uint32_t virtual_address,
                                               void *context) {
    struct scheduler_shared_page_query *query =
        (struct scheduler_shared_page_query *)context;
    struct scheduler_task *task;

    if (query == 0 || kernel_i386_syscall_page_is_shared == 0) {
        return 0;
    }
    task = i386_scheduler_task_by_pid(query->pid);
    if (task != 0) {
        (void)process32_bind_current_address_space(&task->process,
                                                   &task->address_space,
                                                   task->mappings);
    }
    return kernel_i386_syscall_page_is_shared(query->pid, virtual_address);
}

static void scheduler_process_copy_text(char *dst,
                                        uint32_t size,
                                        const char *src) {
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

static void scheduler_process_destroy_root(uint32_t root) {
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

static void scheduler_process_destroy_address_space(struct scheduler_task *task) {
    uint32_t root;

    if (task == 0) {
        return;
    }
    root = task->root;
    task->root = 0u;
    task->address_space.user_cr3 = 0u;
    task->process.address_space = &task->address_space;
    scheduler_process_destroy_root(root);
}

static void scheduler_process_cleanup_mappings(struct scheduler_task *task) {
    if (task == 0 || task->process.pid == 0u) {
        return;
    }
    (void)process32_bind_current_address_space(&task->process,
                                               &task->address_space,
                                               task->mappings);
    process_mm_cleanup_pid(task->process.pid);
    memset(task->mappings, 0, sizeof(task->mappings));
}

static void scheduler_process_cleanup_files(struct scheduler_task *task) {
    if (task == 0) {
        return;
    }
    process_discard_file_array(task->process.files);
}

static void scheduler_process_cleanup_owned_resources(
    struct scheduler_task *task,
    int cleanup_pid,
    int destroy_address_space) {
    if (task == 0) {
        return;
    }
    if (cleanup_pid) {
        scheduler_process_cleanup_mappings(task);
    }
    scheduler_process_cleanup_files(task);
    if (destroy_address_space) {
        scheduler_process_destroy_address_space(task);
    }
}

void i386_scheduler_task_release(uint32_t slot, int cleanup_pid) {
    if (slot >= I386_SCHEDULER_TASKS) {
        return;
    }
    scheduler_process_cleanup_owned_resources(&tasks[slot], cleanup_pid, 1);
    tasks[slot].result = 0u;
    tasks[slot].heap_next = I386_USER_HEAP_BASE;
    tasks[slot].process.parent.pid = 0u;
    process_lifecycle_clear_wait(&tasks[slot].process);
    tasks[slot].fpu_valid = 0u;
    tasks[slot].process.state = PROCESS_STATE_FREE;
}

void i386_scheduler_task_reap(uint32_t slot) {
    if (slot >= I386_SCHEDULER_TASKS ||
        tasks[slot].process.state != PROCESS_STATE_EXITED) {
        return;
    }
    i386_scheduler_task_release(slot, 1);
}

static void scheduler_process_clone_mapping_table(
    struct scheduler_task *child,
    const struct scheduler_task *parent,
    uint32_t child_root) {
    uint32_t old_root;

    if (child == 0 || parent == 0 || child_root == 0u) {
        return;
    }
    memset(child->mappings, 0, sizeof(child->mappings));
    old_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    for (uint32_t i = 0u; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        const struct user_page_mapping *src = &parent->mappings[i];
        struct user_page_mapping *dst = &child->mappings[i];
        uint32_t physical;

        if (!src->used || src->virt_addr > 0xffffffffu) {
            continue;
        }
        if (!i386_paging_translate_in(child_root,
                                      (uint32_t)src->virt_addr,
                                      &physical)) {
            continue;
        }
        *dst = *src;
        dst->phys_addr = physical & ~(uint64_t)(I386_PAGE_SIZE - 1u);
    }
    i386_paging_switch(old_root);
}

static void scheduler_process_fill_info(struct syscall_process_info *out,
                                        const struct process *process) {
    struct process_snapshot snapshot;

    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (process == 0) {
        return;
    }
    process_snapshot_fill(&snapshot, process);
    out->pid = snapshot.pid;
    out->slot = snapshot.slot;
    out->state = snapshot.state;
    out->exit_code = snapshot.exit_code;
    out->wake_tick = snapshot.wake_tick;
    out->image_kind = snapshot.image_kind;
    for (uint32_t i = 0u;
         i + 1u < sizeof(out->name) && snapshot.name[i] != '\0';
         i++) {
        out->name[i] = snapshot.name[i];
    }
}

static void scheduler_process_copy_wait_info_to_user(
    uint32_t waiter,
    const struct process *exited,
    void *context) {
    struct syscall_process_info info;
    uint32_t current_root;

    (void)context;
    if (waiter >= I386_SCHEDULER_TASKS ||
        tasks[waiter].process.wait.info_user == 0u ||
        tasks[waiter].root == 0u ||
        exited == 0) {
        return;
    }
    scheduler_process_fill_info(&info, exited);
    current_root = i386_paging_root();
    i386_paging_switch(tasks[waiter].root);
    (void)arch_copy_to_user((uint32_t)tasks[waiter].process.wait.info_user,
                            &info,
                            sizeof(info));
    i386_paging_switch(current_root);
}

int32_t i386_scheduler_spawn(uint32_t entry,
                             uint32_t stack,
                             uint32_t root,
                             const char *name) {
    uint32_t parent_pid;

    if (!scheduler_active || root == 0u) {
        return -1;
    }
    parent_pid = tasks[scheduler_process_current_slot()].process.pid;
    for (uint32_t slot = 0; slot < I386_SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_FREE) {
            char parent_cwd[NOS_PATH_BUFFER_SIZE];
            struct file inherited[NOS_PROCESS_FILE_MAX];
            struct process *parent =
                &tasks[scheduler_process_current_slot()].process;

            scheduler_process_copy_text(parent_cwd,
                                        sizeof(parent_cwd),
                                        parent->cwd_storage);
            if (!process_clone_spawn_files(parent, inherited)) {
                scheduler_process_destroy_root(root);
                return -1;
            }
            i386_scheduler_task_init(&tasks[slot],
                                     entry,
                                     stack,
                                     slot + 1u,
                                     root,
                                     name,
                                     parent_pid);
            scheduler_process_copy_text(tasks[slot].process.cwd_storage,
                                        sizeof(tasks[slot].process.cwd_storage),
                                        parent_cwd);
            process_install_cloned_files(&tasks[slot].process, inherited);
            if (!sched_runqueue_activate(&runqueue, slot)) {
                i386_scheduler_task_release(slot, 0);
                return -1;
            }
            return (int32_t)tasks[slot].process.pid;
        }
    }
    scheduler_process_destroy_root(root);
    return -1;
}

static int32_t i386_scheduler_spawn_loaded(
    const struct process_loaded_image *image) {
    if (image == 0 ||
        image->entry > 0xffffffffu ||
        image->stack > 0xffffffffu ||
        image->root > 0xffffffffu) {
        return -1;
    }
    return i386_scheduler_spawn((uint32_t)image->entry,
                                (uint32_t)image->stack,
                                (uint32_t)image->root,
                                image->name);
}

int32_t i386_scheduler_fork(const struct process_context *context,
                            uint32_t *child_pid_out) {
    struct scheduler_task *parent;
    uint32_t parent_slot;
    uint32_t child_root;
    struct file cloned_files[NOS_PROCESS_FILE_MAX];
    struct scheduler_shared_page_query shared_query;

    if (child_pid_out != 0) {
        *child_pid_out = 0u;
    }
    if (!scheduler_active || context == 0) {
        return -1;
    }
    parent_slot = scheduler_process_current_slot();
    parent = &tasks[parent_slot];
    if (parent->root == 0u ||
        parent->process.image_kind == PROCESS_IMAGE_NONE ||
        !process_clone_all_files(&parent->process, cloned_files)) {
        return -1;
    }

    i386_scheduler_backend_save_frame(parent_slot);
    i386_scheduler_backend_restore_frame(parent_slot);
    shared_query.pid = parent->process.pid;
    i386_paging_switch(i386_paging_kernel_root());
    child_root = i386_paging_clone_user_cow_ex(
        parent->root,
        scheduler_process_shared_page_query,
        &shared_query);
    i386_paging_switch(parent->root);
    if (child_root == 0u) {
        process_discard_file_array(cloned_files);
        return -1;
    }

    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_FREE) {
            char parent_name[NOS_NAME_BUFFER_SIZE];
            uint32_t child_pid = scheduler_next_pid++;
            int cloned_files_installed = 0;

            scheduler_process_copy_text(
                parent_name,
                sizeof(parent_name),
                parent->process.name != 0
                    ? parent->process.name
                    : parent->process.name_storage);
            tasks[slot] = *parent;
            process_forget_file_array(tasks[slot].process.files);
            tasks[slot].ticks = 0u;
            tasks[slot].root = child_root;
            tasks[slot].result = 0u;
            tasks[slot].process.parent.pid = parent->process.pid;
            tasks[slot].process.pid = child_pid;
            tasks[slot].process.slot = slot;
            process_lifecycle_clear_wait(&tasks[slot].process);
            tasks[slot].address_space.user_cr3 = child_root;
            scheduler_process_clone_mapping_table(&tasks[slot],
                                                  parent,
                                                  child_root);
            if (!addrspace_fork_retain_shared(child_root,
                                              tasks[slot].mappings)) {
                process_discard_file_array(cloned_files);
                i386_scheduler_task_release(slot, 1);
                return -1;
            }
            tasks[slot].process.state = PROCESS_STATE_READY;
            tasks[slot].process.exit_code = 0;
            tasks[slot].process.wake_tick = 0u;
            tasks[slot].process.address_space = &tasks[slot].address_space;
            tasks[slot].process.context = *context;
            process_context_set_return_value(&tasks[slot].process.context, 0u);
            process_set_name(&tasks[slot].process,
                             parent_name[0] != '\0'
                                 ? parent_name
                                 : "fork-child");
            process_install_cloned_files(&tasks[slot].process, cloned_files);
            cloned_files_installed = 1;
            if (!sched_runqueue_activate(&runqueue, slot)) {
                if (!cloned_files_installed) {
                    process_discard_file_array(cloned_files);
                }
                i386_scheduler_task_release(slot, 1);
                return -1;
            }
            if (child_pid_out != 0) {
                *child_pid_out = tasks[slot].process.pid;
            }
            return (int32_t)tasks[slot].process.pid;
        }
    }

    process_discard_file_array(cloned_files);
    i386_paging_switch(i386_paging_kernel_root());
    i386_paging_destroy_user_space(child_root);
    i386_paging_switch(parent->root);
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
    uint32_t old_root;
    char cwd[NOS_PATH_BUFFER_SIZE];
    struct file inherited[NOS_PROCESS_FILE_MAX];

    if (!scheduler_active || context == 0 || root == 0u) {
        return 0u;
    }
    task = &tasks[scheduler_process_current_slot()];
    pid = task->process.pid;
    slot = task->process.slot;
    parent_pid = task->process.parent.pid;
    old_root = task->root;
    scheduler_process_copy_text(cwd, sizeof(cwd), task->process.cwd_storage);
    if (!process_clone_spawn_files(&task->process, inherited)) {
        return 0u;
    }
    scheduler_process_cleanup_owned_resources(task, 1, 0);
    scheduler_process_destroy_root(old_root);
    i386_scheduler_task_init(task,
                             entry,
                             stack,
                             slot + 1u,
                             root,
                             name,
                             parent_pid);
    task->process.pid = pid;
    scheduler_process_copy_text(task->process.cwd_storage,
                                sizeof(task->process.cwd_storage),
                                cwd);
    process_install_cloned_files(&task->process, inherited);
    task->process.state = PROCESS_STATE_RUNNING;
    i386_scheduler_backend_switch_to_task(slot);
    return (uintptr_t)&task->process.context;
}

static uintptr_t i386_scheduler_exec_loaded(
    const struct process_context *context,
    const struct process_loaded_image *image) {
    if (image == 0 ||
        image->entry > 0xffffffffu ||
        image->stack > 0xffffffffu ||
        image->root > 0xffffffffu) {
        return 0u;
    }
    return i386_scheduler_exec(context,
                               (uint32_t)image->entry,
                               (uint32_t)image->stack,
                               (uint32_t)image->root,
                               image->name);
}

const struct process_context *i386_scheduler_tick(
    const struct process_context *context) {
    uint32_t previous;
    uint32_t next;

    if (!scheduler_active || context == 0 || !context->user_mode) {
        return context;
    }

    previous = scheduler_process_current_slot();
    tasks[previous].process.context = *context;
    tasks[previous].ticks++;
    scheduler_tick_count++;
    next = sched_runqueue_step(&runqueue,
                               SCHED_RUNQUEUE_PREEMPT,
                               scheduler_tick_count,
                               0u);
    if (next == SCHED_RUNQUEUE_NONE) {
        return context;
    }
    if (next != previous) {
        i386_scheduler_backend_switch_task(previous, next);
    }
    return &tasks[next].process.context;
}

uintptr_t i386_scheduler_exit(const struct process_context *context,
                              int exit_code) {
    uint32_t exiting;
    uint32_t next;
    uint32_t woke;

    if (!scheduler_active || context == 0) {
        return 0;
    }

    exiting = scheduler_process_current_slot();
    tasks[exiting].process.context = *context;
    tasks[exiting].result = i386_scheduler_backend_task_result(context);
    process_lifecycle_mark_exited_for_scheduler(&tasks[exiting].process,
                                                exit_code);
    scheduler_process_cleanup_files(&tasks[exiting]);
    woke = process_lifecycle_wake_exit_waiters(
        task_processes,
        I386_SCHEDULER_TASKS,
        tasks[exiting].process.pid,
        exit_code,
        scheduler_process_copy_wait_info_to_user,
        0);
    next = sched_runqueue_exit(&runqueue);
    if (woke != 0u) {
        i386_scheduler_task_reap(exiting);
    }
    if (sched_runqueue_completed_count(&runqueue) ==
        sched_runqueue_active_count(&runqueue)) {
        scheduler_active = 0;
        i386_scheduler_backend_switch_to_kernel();
        return 1;
    }

    if (next == SCHED_RUNQUEUE_NONE) {
        scheduler_active = 0;
        i386_scheduler_backend_switch_to_kernel();
        return 1;
    }
    i386_scheduler_backend_switch_to_task(next);
    return (uintptr_t)&tasks[next].process.context;
}

uintptr_t i386_scheduler_fault_exit(const struct process_context *context,
                                    int exit_code) {
    return i386_scheduler_exit(context, exit_code);
}

uintptr_t i386_scheduler_wait(const struct process_context *context,
                              uint32_t pid,
                              int32_t *status,
                              int *blocked,
                              uint32_t user_info,
                              struct process_snapshot *snapshot) {
    uint32_t next;
    uint32_t current_pid;
    uint32_t waiting;
    uint32_t child_slot;
    int child_exited;

    if (!scheduler_active || context == 0 || status == 0 || blocked == 0) {
        return 0u;
    }
    *blocked = 0;
    waiting = scheduler_process_current_slot();
    current_pid = tasks[waiting].process.pid;
    if (!process_lifecycle_find_wait_child(task_processes,
                                           I386_SCHEDULER_TASKS,
                                           current_pid,
                                           pid,
                                           SYS_WAIT_LAST_PID,
                                           &child_slot,
                                           &child_exited)) {
        *status = -1;
        return 0u;
    }
    if (child_exited) {
        *status = tasks[child_slot].process.exit_code;
        if (snapshot != 0) {
            process_snapshot_fill(snapshot, &tasks[child_slot].process);
        }
        i386_scheduler_task_reap(child_slot);
        return 0u;
    }
    tasks[waiting].process.context = *context;
    tasks[waiting].process.wait.pid = pid;
    tasks[waiting].process.wait.info_user = user_info;
    process_lifecycle_prepare_wait(&tasks[waiting].process);
    next = sched_runqueue_block(&runqueue);
    if (next == waiting) {
        return 0u;
    }
    if (next == SCHED_RUNQUEUE_NONE) {
        return 0u;
    }
    i386_scheduler_backend_switch_task(waiting, next);
    *blocked = 1;
    return (uintptr_t)&tasks[next].process.context;
}

int32_t i386_scheduler_kill(uint32_t pid) {
    uint32_t current_pid;

    if (!scheduler_active || pid == 0u) {
        return -1;
    }
    current_pid = tasks[scheduler_process_current_slot()].process.pid;
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        if (process_lifecycle_can_signal_child(&tasks[slot].process,
                                               tasks[slot].process.parent.pid,
                                               current_pid,
                                               pid)) {
            process_lifecycle_mark_exited_for_scheduler(&tasks[slot].process,
                                                        -9);
            process_lifecycle_clear_wait(&tasks[slot].process);
            sched_runqueue_mark_completed(&runqueue);
            scheduler_process_cleanup_owned_resources(&tasks[slot], 1, 1);
            if (process_lifecycle_wake_exit_waiters(
                    task_processes,
                    I386_SCHEDULER_TASKS,
                    pid,
                    -9,
                    scheduler_process_copy_wait_info_to_user,
                    0) != 0u) {
                i386_scheduler_task_reap(slot);
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
    yielding = scheduler_process_current_slot();
    tasks[yielding].process.context = *context;
    next = sched_runqueue_step(&runqueue, SCHED_RUNQUEUE_YIELD, 0u, 0u);
    if (next == SCHED_RUNQUEUE_NONE) {
        return 0u;
    }
    if (next != yielding) {
        i386_scheduler_backend_switch_task(yielding, next);
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
    sleeping = scheduler_process_current_slot();
    tasks[sleeping].process.context = *context;
    next = sched_runqueue_step(&runqueue,
                               SCHED_RUNQUEUE_SLEEP,
                               0u,
                               scheduler_tick_count + ticks);
    if (next == SCHED_RUNQUEUE_NONE) {
        return 0u;
    }
    i386_scheduler_backend_switch_task(sleeping, next);
    return (uintptr_t)&tasks[next].process.context;
}

int32_t i386_scheduler_reap_exited_pid(uint32_t pid) {
    if (pid == 0u) {
        return -1;
    }
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.pid == pid &&
            tasks[slot].process.state == PROCESS_STATE_EXITED) {
            i386_scheduler_task_reap(slot);
            return 1;
        }
    }
    return 0;
}

static const struct process_scheduler_ops i386_process_scheduler_ops = {
    i386_scheduler_spawn_loaded,
    i386_scheduler_run_loaded,
    i386_scheduler_run_loaded_quiet,
    i386_scheduler_fork,
    i386_scheduler_exec_loaded,
    i386_scheduler_wait,
    i386_scheduler_exit,
    i386_scheduler_yield,
    i386_scheduler_sleep,
    i386_scheduler_ticks,
    i386_scheduler_current_pid,
    i386_scheduler_kill,
    i386_scheduler_process_snapshot,
    i386_scheduler_reap_exited_pid
};

void i386_scheduler_register_process_ops(void) {
    process_scheduler_ops_register(&i386_process_scheduler_ops);
}
