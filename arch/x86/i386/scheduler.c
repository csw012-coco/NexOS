#include "context.h"
#include "gdt.h"
#include "fs/vfs.h"
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

enum {
    SCHEDULER_TASKS = 8,
    TASK_NAME_SIZE = 16,
    USER_HEAP_BASE = 0x50000000u,
    USER_HEAP_LIMIT = 0x70000000u
};

struct scheduler_task {
    uint32_t ticks;
    uint32_t root;
    uint32_t result;
    uint32_t heap_next;
    uint32_t parent_pid;
    uint32_t wait_pid;
    struct address_space address_space;
    struct process process;
};

static struct scheduler_task tasks[SCHEDULER_TASKS];
static struct process *task_processes[SCHEDULER_TASKS];
static struct sched_runqueue runqueue;
static volatile uint32_t scheduler_active;
static uint32_t scheduler_tick_count;
static uint32_t scheduler_kernel_root;
static uint32_t scheduler_next_pid = 1u;

static uint32_t current_task_slot(void) {
    return sched_runqueue_current_slot(&runqueue);
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
}

static void scheduler_reset(void) {
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
    if (root == 0u) {
        return 0;
    }
    scheduler_reset();
    task_init(&tasks[0], entry, stack, 1u, root, name, 0u);
    if (!sched_runqueue_activate(&runqueue, 0u)) {
        return 0;
    }
    return scheduler_enter_first();
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

            copy_text(parent_cwd,
                      sizeof(parent_cwd),
                      tasks[current_task_slot()].process.cwd_storage);
            for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
                inherited[fd] =
                    tasks[current_task_slot()].process.files[fd];
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
            for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
                if (inherited[fd].kind != KERNEL_FILE_NONE &&
                    (inherited[fd].flags &
                     KERNEL_FILE_CLOSE_ON_SPAWN) == 0u) {
                    (void)file_clone(&tasks[slot].process.files[fd],
                                     &inherited[fd]);
                }
            }
            if (!sched_runqueue_activate(&runqueue, slot)) {
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
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        inherited[fd] = task->process.files[fd];
    }
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
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        task->process.files[fd] = inherited[fd];
    }
    task->process.state = PROCESS_STATE_RUNNING;
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
    next = sched_runqueue_reschedule(&runqueue, SCHED_RUNQUEUE_PREEMPT);
    if (next == SCHED_RUNQUEUE_NONE) {
        return context;
    }
    if (next != previous) {
        i386_paging_switch(tasks[next].root);
    }
    return &tasks[next].process.context;
}

uintptr_t i386_scheduler_exit(const struct process_context *context,
                              int exit_code) {
    uint32_t exiting;
    uint32_t next;

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
        }
    }
    next = sched_runqueue_reschedule(&runqueue, SCHED_RUNQUEUE_EXIT);
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
    i386_paging_switch(tasks[next].root);
    return (uintptr_t)&tasks[next].process.context;
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
            i386_paging_switch(tasks[next].root);
            *blocked = 1;
            return (uintptr_t)&tasks[next].process.context;
        }
    }
    *status = -1;
    return 0u;
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
        i386_paging_switch(tasks[next].root);
    }
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

uint32_t i386_scheduler_page_alloc(void) {
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
            !i386_paging_map_page_in(task->root, address, frame, 1, 1)) {
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
