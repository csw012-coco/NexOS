#include "gdt.h"
#include "idt.h"
#include "fs/vfs.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/mem/address_space.h"
#include "paging.h"
#include "scheduler.h"

enum {
    SCHEDULER_TASKS = 2,
    TASK_NAME_SIZE = 16
};

struct scheduler_task {
    struct i386_irq_frame context;
    uint32_t ticks;
    uint32_t root;
    uint32_t result;
    struct address_space address_space;
    struct process process;
};

static struct scheduler_task tasks[SCHEDULER_TASKS];
static volatile uint32_t scheduler_active;
static uint32_t current_task;
static uint32_t scheduler_tick_count;
static uint32_t scheduler_switch_count;
static uint32_t scheduler_completed_count;
static uint32_t scheduler_kernel_root;
static uint32_t scheduler_next_pid = 1u;

extern int i386_usermode_enter_context(struct i386_irq_frame *frame);

static void clear_context(struct i386_irq_frame *frame) {
    uint32_t *words = (uint32_t *)frame;

    for (uint32_t i = 0; i < sizeof(*frame) / sizeof(uint32_t); i++) {
        words[i] = 0;
    }
}

static void task_init(struct scheduler_task *task,
                      uint32_t entry,
                      uint32_t stack,
                      uint32_t id,
                      uint32_t root) {
    static const char prefix[] = "i386-task";

    clear_context(&task->context);
    task->context.ebx = id;
    task->context.eip = entry;
    task->context.cs = I386_GDT_USER_CODE;
    task->context.eflags = 0x202u;
    task->context.user_esp = stack;
    task->context.user_ss = I386_GDT_USER_DATA;
    task->ticks = 0;
    task->root = root;
    task->result = 0;
    task->address_space.kernel_cr3 = i386_paging_kernel_root();
    task->address_space.user_cr3 = root;
    task->address_space.reserved_phys_base = 0;
    task->address_space.reserved_phys_limit = 0;
    task->address_space.reserved_phys_next = 0;

    for (uint32_t i = 0; i < sizeof(task->process); i++) {
        ((unsigned char *)&task->process)[i] = 0;
    }
    task->process.pid = scheduler_next_pid++;
    task->process.slot = id - 1u;
    task->process.state = PROCESS_STATE_READY;
    task->process.name = task->process.name_storage;
    for (uint32_t i = 0; i < sizeof(prefix); i++) {
        task->process.name_storage[i] = prefix[i];
    }
    task->process.name_storage[9] = (char)('0' + id);
    task->process.name_storage[10] = '\0';
    task->process.cwd_storage[0] = '/';
    task->process.cwd_storage[1] = '\0';
    task->process.image_kind = PROCESS_IMAGE_ELF;
    task->process.entry = entry;
    task->process.stack_top = stack;
    task->process.address_space = &task->address_space;
}

static uint32_t next_runnable(uint32_t current) {
    for (uint32_t offset = 1; offset <= SCHEDULER_TASKS; offset++) {
        uint32_t candidate = (current + offset) % SCHEDULER_TASKS;

        if (tasks[candidate].process.state == PROCESS_STATE_READY ||
            tasks[candidate].process.state == PROCESS_STATE_RUNNING) {
            return candidate;
        }
    }
    return current;
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
    task_init(&tasks[0], entry0, stack0, 1u, root0);
    task_init(&tasks[1], entry1, stack1, 2u, root1);
    current_task = 0;
    scheduler_tick_count = 0;
    scheduler_switch_count = 0;
    scheduler_completed_count = 0;
    scheduler_kernel_root = i386_paging_kernel_root();
    scheduler_active = 1;

    tasks[0].process.state = PROCESS_STATE_RUNNING;
    i386_paging_switch(tasks[0].root);
    if (!i386_usermode_enter_context(&tasks[0].context)) {
        i386_paging_switch(scheduler_kernel_root);
        scheduler_active = 0;
        return 0;
    }
    return !scheduler_active && scheduler_completed_count == SCHEDULER_TASKS;
}

struct i386_irq_frame *i386_scheduler_tick(struct i386_irq_frame *frame) {
    uint32_t next;

    if (!scheduler_active || frame == 0 || (frame->cs & 3u) != 3u) {
        return frame;
    }

    tasks[current_task].context = *frame;
    tasks[current_task].ticks++;
    tasks[current_task].process.state = PROCESS_STATE_READY;
    scheduler_tick_count++;
    next = next_runnable(current_task);
    if (next != current_task) {
        current_task = next;
        tasks[current_task].process.state = PROCESS_STATE_RUNNING;
        scheduler_switch_count++;
        i386_paging_switch(tasks[current_task].root);
    }
    return &tasks[current_task].context;
}

uint32_t i386_scheduler_exit(struct i386_irq_frame *frame, int exit_code) {
    uint32_t next;

    if (!scheduler_active || frame == 0) {
        return 0;
    }

    tasks[current_task].context = *frame;
    tasks[current_task].result = frame->edx;
    tasks[current_task].process.exit_code = exit_code;
    tasks[current_task].process.state = PROCESS_STATE_EXITED;
    scheduler_completed_count++;
    if (scheduler_completed_count == SCHEDULER_TASKS) {
        scheduler_active = 0;
        i386_paging_switch(scheduler_kernel_root);
        return 1;
    }

    next = next_runnable(current_task);
    if (next == current_task) {
        scheduler_active = 0;
        i386_paging_switch(scheduler_kernel_root);
        return 1;
    }
    current_task = next;
    tasks[current_task].process.state = PROCESS_STATE_RUNNING;
    scheduler_switch_count++;
    i386_paging_switch(tasks[current_task].root);
    return (uint32_t)&tasks[current_task].context;
}

uint32_t i386_scheduler_yield(struct i386_irq_frame *frame) {
    uint32_t next;

    if (!scheduler_active || frame == 0) {
        return 0;
    }
    tasks[current_task].context = *frame;
    tasks[current_task].process.state = PROCESS_STATE_READY;
    next = next_runnable(current_task);
    if (next == current_task) {
        tasks[current_task].process.state = PROCESS_STATE_RUNNING;
        return (uint32_t)&tasks[current_task].context;
    }
    current_task = next;
    tasks[current_task].process.state = PROCESS_STATE_RUNNING;
    scheduler_switch_count++;
    i386_paging_switch(tasks[current_task].root);
    return (uint32_t)&tasks[current_task].context;
}

uint32_t i386_scheduler_ticks(void) {
    return scheduler_tick_count;
}

uint32_t i386_scheduler_switches(void) {
    return scheduler_switch_count;
}

uint32_t i386_scheduler_completed(void) {
    return scheduler_completed_count;
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
    return tasks[current_task].process.pid;
}

int32_t i386_scheduler_open(struct vfs *vfs,
                            const char *path,
                            uint32_t flags) {
    struct process *process;
    struct vfs_node node;

    if (!scheduler_active || vfs == 0 || path == 0 ||
        (flags != 0u && flags != SYS_OPEN_READ) ||
        vfs_open(vfs, path, 0u, &node) != 0) {
        return -1;
    }
    process = &tasks[current_task].process;
    for (uint32_t fd = 3u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        if (process->files[fd].kind == KERNEL_FILE_NONE) {
            process->files[fd].kind = KERNEL_FILE_VFS;
            process->files[fd].offset = 0u;
            process->files[fd].dir_index = 0u;
            process->files[fd].vfs_node = node;
            process->files[fd].opened_path[0] = '\0';
            process->files[fd].private_data = 0;
            process->files[fd].ops = 0;
            return (int32_t)fd;
        }
    }
    return -1;
}

int32_t i386_scheduler_read(struct vfs *vfs,
                            uint32_t fd,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags) {
    struct file *file;
    int64_t result;

    if (!scheduler_active || vfs == 0 || buffer == 0 ||
        fd >= NOS_PROCESS_FILE_MAX || size == 0u ||
        (flags & ~(SYS_READ_NONBLOCK | SYS_READ_CHAR)) != 0u) {
        return -1;
    }
    file = &tasks[current_task].process.files[fd];
    if (file->kind != KERNEL_FILE_VFS) {
        return -1;
    }
    result = vfs_read(vfs,
                      &file->vfs_node,
                      &file->offset,
                      buffer,
                      size,
                      flags);
    return result < -1 || result > 0x7fffffffu ? -1 : (int32_t)result;
}

int32_t i386_scheduler_close(uint32_t fd) {
    struct file *file;

    if (!scheduler_active || fd < 3u || fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &tasks[current_task].process.files[fd];
    if (file->kind == KERNEL_FILE_NONE) {
        return -1;
    }
    for (uint32_t i = 0; i < sizeof(*file); i++) {
        ((uint8_t *)file)[i] = 0u;
    }
    return 0;
}

int i386_scheduler_process_snapshot(uint32_t task,
                                    struct process_snapshot *snapshot) {
    const struct process *process;

    if (task >= SCHEDULER_TASKS || snapshot == 0) {
        return 0;
    }
    process = &tasks[task].process;
    snapshot->pid = process->pid;
    snapshot->slot = process->slot;
    snapshot->state = process->state;
    snapshot->exit_code = process->exit_code;
    snapshot->wake_tick = process->wake_tick;
    snapshot->image_kind = process->image_kind;
    for (uint32_t i = 0; i < sizeof(snapshot->name); i++) {
        snapshot->name[i] = process->name_storage[i];
        if (process->name_storage[i] == '\0') {
            break;
        }
    }
    return 1;
}
