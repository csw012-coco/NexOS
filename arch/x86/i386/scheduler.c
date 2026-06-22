#include "gdt.h"
#include "idt.h"
#include "fs/vfs.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/mem/address_space.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"

enum {
    SCHEDULER_TASKS = 8,
    TASK_NAME_SIZE = 16,
    USER_HEAP_BASE = 0x50000000u,
    USER_HEAP_LIMIT = 0x70000000u,
    I386_PIPE_MAX = 4,
    I386_PIPE_BUFFER_SIZE = 4096u
};

struct i386_pipe {
    uint8_t used;
    uint32_t readers;
    uint32_t writers;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint8_t buffer[I386_PIPE_BUFFER_SIZE];
};

struct scheduler_task {
    struct i386_irq_frame context;
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
static volatile uint32_t scheduler_active;
static uint32_t current_task;
static uint32_t scheduler_tick_count;
static uint32_t scheduler_switch_count;
static uint32_t scheduler_completed_count;
static uint32_t scheduler_task_count;
static uint32_t scheduler_kernel_root;
static uint32_t scheduler_next_pid = 1u;
static struct i386_pipe pipes[I386_PIPE_MAX];

extern int i386_usermode_enter_context(struct i386_irq_frame *frame);

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

static void file_clear(struct file *file) {
    for (uint32_t i = 0u; i < sizeof(*file); i++) {
        ((uint8_t *)file)[i] = 0u;
    }
}

static void pipe_release(struct file *file) {
    struct i386_pipe *pipe;

    if (file == 0 ||
        (file->kind != KERNEL_FILE_PIPE_READ &&
         file->kind != KERNEL_FILE_PIPE_WRITE)) {
        return;
    }
    pipe = (struct i386_pipe *)file->private_data;
    if (pipe == 0 || !pipe->used) {
        return;
    }
    if (file->kind == KERNEL_FILE_PIPE_READ && pipe->readers != 0u) {
        pipe->readers--;
    } else if (file->kind == KERNEL_FILE_PIPE_WRITE &&
               pipe->writers != 0u) {
        pipe->writers--;
    }
    if (pipe->readers == 0u && pipe->writers == 0u) {
        for (uint32_t i = 0u; i < sizeof(*pipe); i++) {
            ((uint8_t *)pipe)[i] = 0u;
        }
    }
}

static void file_close_local(struct file *file) {
    pipe_release(file);
    file_clear(file);
}

static int file_clone_local(struct file *dst, const struct file *src) {
    struct i386_pipe *pipe;

    if (dst == 0 || src == 0 || src->kind == KERNEL_FILE_NONE) {
        return 0;
    }
    file_close_local(dst);
    *dst = *src;
    pipe = (struct i386_pipe *)src->private_data;
    if (src->kind == KERNEL_FILE_PIPE_READ) {
        if (pipe == 0 || !pipe->used) {
            file_clear(dst);
            return 0;
        }
        pipe->readers++;
    } else if (src->kind == KERNEL_FILE_PIPE_WRITE) {
        if (pipe == 0 || !pipe->used) {
            file_clear(dst);
            return 0;
        }
        pipe->writers++;
    }
    return 1;
}

static int resolve_path(const struct process *process,
                        const char *path,
                        char *out,
                        uint32_t out_size) {
    char combined[NOS_PATH_BUFFER_SIZE];
    uint32_t combined_length = 0u;
    uint32_t component_starts[NOS_PATH_BUFFER_SIZE / 2u];
    uint32_t component_count = 0u;
    uint32_t output_length = 1u;
    uint32_t cursor = 0u;

    if (process == 0 || path == 0 || out == 0 || out_size < 2u) {
        return 0;
    }
    if (path[0] != '/') {
        const char *cwd = process->cwd_storage[0] != '\0'
            ? process->cwd_storage
            : "/";

        while (cwd[combined_length] != '\0' &&
               combined_length + 1u < sizeof(combined)) {
            combined[combined_length] = cwd[combined_length];
            combined_length++;
        }
        if (combined_length > 1u &&
            combined[combined_length - 1u] != '/' &&
            combined_length + 1u < sizeof(combined)) {
            combined[combined_length++] = '/';
        }
    }
    for (uint32_t i = 0u; path[i] != '\0'; i++) {
        if (combined_length + 1u >= sizeof(combined)) {
            return 0;
        }
        combined[combined_length++] = path[i];
    }
    combined[combined_length] = '\0';
    out[0] = '/';
    out[1] = '\0';

    while (cursor < combined_length) {
        uint32_t start;
        uint32_t length;

        while (cursor < combined_length && combined[cursor] == '/') {
            cursor++;
        }
        start = cursor;
        while (cursor < combined_length && combined[cursor] != '/') {
            cursor++;
        }
        length = cursor - start;
        if (length == 0u ||
            (length == 1u && combined[start] == '.')) {
            continue;
        }
        if (length == 2u &&
            combined[start] == '.' &&
            combined[start + 1u] == '.') {
            if (component_count != 0u) {
                output_length = component_starts[--component_count];
                if (output_length > 1u) {
                    output_length--;
                }
                out[output_length] = '\0';
            }
            continue;
        }
        if (component_count >= sizeof(component_starts) /
                                   sizeof(component_starts[0])) {
            return 0;
        }
        if (output_length > 1u) {
            if (output_length + 1u >= out_size) {
                return 0;
            }
            out[output_length++] = '/';
        }
        component_starts[component_count++] = output_length;
        if (output_length + length >= out_size) {
            return 0;
        }
        for (uint32_t i = 0u; i < length; i++) {
            out[output_length++] = combined[start + i];
        }
        out[output_length] = '\0';
    }
    return 1;
}

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
                      uint32_t root,
                      const char *name,
                      uint32_t parent_pid) {
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
    task->heap_next = USER_HEAP_BASE;
    task->parent_pid = parent_pid;
    task->wait_pid = 0u;
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
    if (name == 0 || name[0] == '\0') {
        name = prefix;
    }
    for (uint32_t i = 0; i + 1u < sizeof(task->process.name_storage); i++) {
        task->process.name_storage[i] = name[i];
        if (name[i] == '\0') {
            break;
        }
        task->process.name_storage[i + 1u] = '\0';
    }
    task->process.cwd_storage[0] = '/';
    task->process.cwd_storage[1] = '\0';
    task->process.image_kind = PROCESS_IMAGE_ELF;
    task->process.entry = entry;
    task->process.stack_top = stack;
    task->process.address_space = &task->address_space;
    task->process.files[SYS_FD_STDIN].kind = KERNEL_FILE_TTY_STDIN;
    task->process.files[SYS_FD_STDOUT].kind = KERNEL_FILE_TTY_STDOUT;
    task->process.files[SYS_FD_STDERR].kind = KERNEL_FILE_TTY_STDERR;
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

static void scheduler_reset(void) {
    for (uint32_t slot = 0u; slot < SCHEDULER_TASKS; slot++) {
        for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
            file_close_local(&tasks[slot].process.files[fd]);
        }
    }
    for (uint32_t i = 0; i < SCHEDULER_TASKS; i++) {
        for (uint32_t j = 0; j < sizeof(tasks[i]); j++) {
            ((uint8_t *)&tasks[i])[j] = 0u;
        }
        tasks[i].process.state = PROCESS_STATE_FREE;
    }
    current_task = 0u;
    scheduler_tick_count = 0u;
    scheduler_switch_count = 0u;
    scheduler_completed_count = 0u;
    scheduler_task_count = 0u;
    scheduler_kernel_root = i386_paging_kernel_root();
}

static int scheduler_enter_first(void) {
    scheduler_active = 1u;
    tasks[current_task].process.state = PROCESS_STATE_RUNNING;
    i386_paging_switch(tasks[current_task].root);
    if (!i386_usermode_enter_context(&tasks[current_task].context)) {
        i386_paging_switch(scheduler_kernel_root);
        scheduler_active = 0u;
        return 0;
    }
    return !scheduler_active &&
           scheduler_completed_count == scheduler_task_count;
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
    scheduler_task_count = 2u;
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
    scheduler_task_count = 1u;
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
    parent_pid = tasks[current_task].process.pid;
    for (uint32_t slot = 0; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_FREE) {
            char parent_cwd[NOS_PATH_BUFFER_SIZE];
            struct file inherited[NOS_PROCESS_FILE_MAX];

            copy_text(parent_cwd,
                      sizeof(parent_cwd),
                      tasks[current_task].process.cwd_storage);
            for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
                inherited[fd] = tasks[current_task].process.files[fd];
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
                if (inherited[fd].kind != KERNEL_FILE_NONE) {
                    (void)file_clone_local(&tasks[slot].process.files[fd],
                                           &inherited[fd]);
                }
            }
            scheduler_task_count++;
            return (int32_t)tasks[slot].process.pid;
        }
    }
    return -1;
}

uint32_t i386_scheduler_exec(struct i386_irq_frame *frame,
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

    if (!scheduler_active || frame == 0 || root == 0u) {
        return 0u;
    }
    task = &tasks[current_task];
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
    return (uint32_t)&task->context;
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
    for (uint32_t fd = 0u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        file_close_local(&tasks[current_task].process.files[fd]);
    }
    scheduler_completed_count++;
    for (uint32_t slot = 0; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.state == PROCESS_STATE_WAITING &&
            (tasks[slot].wait_pid == 0u ||
             tasks[slot].wait_pid == tasks[current_task].process.pid)) {
            tasks[slot].context.eax = (uint32_t)exit_code;
            tasks[slot].wait_pid = 0u;
            tasks[slot].process.state = PROCESS_STATE_READY;
        }
    }
    if (scheduler_completed_count == scheduler_task_count) {
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

uint32_t i386_scheduler_wait(struct i386_irq_frame *frame,
                             uint32_t pid,
                             int32_t *status,
                             int *blocked) {
    uint32_t next;
    uint32_t current_pid;

    if (!scheduler_active || frame == 0 || status == 0 || blocked == 0) {
        return 0u;
    }
    *blocked = 0;
    current_pid = tasks[current_task].process.pid;
    for (uint32_t slot = 0; slot < SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.pid == pid &&
            tasks[slot].parent_pid == current_pid) {
            if (tasks[slot].process.state == PROCESS_STATE_EXITED) {
                *status = tasks[slot].process.exit_code;
                return 0u;
            }
            tasks[current_task].context = *frame;
            tasks[current_task].process.state = PROCESS_STATE_WAITING;
            tasks[current_task].wait_pid = pid;
            next = next_runnable(current_task);
            if (next == current_task) {
                tasks[current_task].process.state = PROCESS_STATE_RUNNING;
                return 0u;
            }
            current_task = next;
            tasks[current_task].process.state = PROCESS_STATE_RUNNING;
            scheduler_switch_count++;
            i386_paging_switch(tasks[current_task].root);
            *blocked = 1;
            return (uint32_t)&tasks[current_task].context;
        }
    }
    *status = -1;
    return 0u;
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

uint32_t i386_scheduler_page_alloc(void) {
    struct scheduler_task *task;
    uint32_t kernel_root;

    if (!scheduler_active) {
        return 0u;
    }
    task = &tasks[current_task];
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
    task = &tasks[current_task];
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
    char resolved[NOS_PATH_BUFFER_SIZE];
    uint32_t vfs_flags = 0u;
    uint32_t access;

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task].process;
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
        !resolve_path(process, path, resolved, sizeof(resolved))) {
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
    for (uint32_t fd = 3u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        if (process->files[fd].kind == KERNEL_FILE_NONE) {
            process->files[fd].kind = KERNEL_FILE_VFS;
            process->files[fd].offset =
                (flags & SYS_OPEN_APPEND) != 0u
                    ? node.handle.fat32_file.size
                    : 0u;
            process->files[fd].dir_index = 0u;
            process->files[fd].vfs_node = node;
            copy_text(process->files[fd].opened_path,
                      sizeof(process->files[fd].opened_path),
                      resolved);
            process->files[fd].private_data = (void *)access;
            process->files[fd].ops = 0;
            return (int32_t)fd;
        }
    }
    return -1;
}

int32_t i386_scheduler_opendir(struct vfs *vfs, const char *path) {
    struct process *process;
    struct vfs_node node;
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (!scheduler_active || vfs == 0 || path == 0) {
        return -1;
    }
    process = &tasks[current_task].process;
    if (!resolve_path(process, path, resolved, sizeof(resolved)) ||
        vfs_opendir(vfs, resolved, &node) != 0) {
        return -1;
    }
    for (uint32_t fd = 3u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        if (process->files[fd].kind == KERNEL_FILE_NONE) {
            process->files[fd].kind = KERNEL_FILE_VFS;
            process->files[fd].offset = 0u;
            process->files[fd].dir_index = 0u;
            process->files[fd].vfs_node = node;
            copy_text(process->files[fd].opened_path,
                      sizeof(process->files[fd].opened_path),
                      resolved);
            process->files[fd].private_data = 0;
            process->files[fd].ops = 0;
            return (int32_t)fd;
        }
    }
    return -1;
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
    file = &tasks[current_task].process.files[fd];
    if (file->kind != KERNEL_FILE_VFS ||
        file->vfs_node.kind != VFS_NODE_DIR) {
        return -1;
    }
    result = vfs_readdir(vfs,
                         &file->vfs_node,
                         &file->dir_index,
                         &vfs_entry);
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
    process = &tasks[current_task].process;
    if (!resolve_path(process, path, resolved, sizeof(resolved)) ||
        vfs_opendir(vfs, resolved, &directory) != 0) {
        return -1;
    }
    copy_text(process->cwd_storage,
              sizeof(process->cwd_storage),
              resolved);
    return 0;
}

int32_t i386_scheduler_getcwd(char *buffer, uint32_t size) {
    const char *cwd;
    uint32_t length = 0u;

    if (!scheduler_active || buffer == 0 || size == 0u) {
        return -1;
    }
    cwd = tasks[current_task].process.cwd_storage;
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
    file = &tasks[current_task].process.files[fd];
    if (file->kind == KERNEL_FILE_TTY_STDIN) {
        return -2;
    }
    if (file->kind == KERNEL_FILE_PIPE_READ) {
        struct i386_pipe *pipe = file->private_data;
        uint32_t copied = 0u;

        if (pipe == 0 || !pipe->used) {
            return -1;
        }
        while (copied < size && pipe->count != 0u) {
            ((uint8_t *)buffer)[copied++] = pipe->buffer[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1u) % I386_PIPE_BUFFER_SIZE;
            pipe->count--;
        }
        if (copied == 0u && pipe->writers != 0u) {
            return -2;
        }
        return (int32_t)copied;
    }
    if (vfs == 0) {
        return -1;
    }
    if (file->kind != KERNEL_FILE_VFS ||
        (((uint32_t)file->private_data) & SYS_OPEN_READ) == 0u) {
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
    file = &tasks[current_task].process.files[fd];
    if (file->kind == KERNEL_FILE_TTY_STDOUT ||
        file->kind == KERNEL_FILE_TTY_STDERR) {
        return -2;
    }
    if (file->kind == KERNEL_FILE_PIPE_WRITE) {
        struct i386_pipe *pipe = file->private_data;
        uint32_t written = 0u;

        if (pipe == 0 || !pipe->used || pipe->readers == 0u) {
            return -1;
        }
        while (written < size && pipe->count < I386_PIPE_BUFFER_SIZE) {
            pipe->buffer[pipe->write_pos] =
                ((const uint8_t *)buffer)[written++];
            pipe->write_pos =
                (pipe->write_pos + 1u) % I386_PIPE_BUFFER_SIZE;
            pipe->count++;
        }
        if (written == 0u && pipe->readers != 0u) {
            return -2;
        }
        return (int32_t)written;
    }
    if (vfs == 0) {
        return -1;
    }
    if (file->kind != KERNEL_FILE_VFS ||
        (((uint32_t)file->private_data) & SYS_OPEN_WRITE) == 0u) {
        return -1;
    }
    result = vfs_write(vfs,
                       &file->vfs_node,
                       &file->offset,
                       buffer,
                       size,
                       file->opened_path);
    return result < -1 || result > 0x7fffffffu ? -1 : (int32_t)result;
}

int32_t i386_scheduler_pipe(uint32_t pair[2]) {
    struct process *process;
    uint32_t read_fd = NOS_PROCESS_FILE_MAX;
    uint32_t write_fd = NOS_PROCESS_FILE_MAX;
    struct i386_pipe *pipe = 0;

    if (!scheduler_active || pair == 0) {
        return -1;
    }
    process = &tasks[current_task].process;
    for (uint32_t fd = 3u; fd < NOS_PROCESS_FILE_MAX; fd++) {
        if (process->files[fd].kind == KERNEL_FILE_NONE) {
            if (read_fd == NOS_PROCESS_FILE_MAX) {
                read_fd = fd;
            } else {
                write_fd = fd;
                break;
            }
        }
    }
    for (uint32_t i = 0u; i < I386_PIPE_MAX; i++) {
        if (!pipes[i].used) {
            pipe = &pipes[i];
            break;
        }
    }
    if (read_fd == NOS_PROCESS_FILE_MAX ||
        write_fd == NOS_PROCESS_FILE_MAX ||
        pipe == 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < sizeof(*pipe); i++) {
        ((uint8_t *)pipe)[i] = 0u;
    }
    pipe->used = 1u;
    pipe->readers = 1u;
    pipe->writers = 1u;
    process->files[read_fd].kind = KERNEL_FILE_PIPE_READ;
    process->files[read_fd].private_data = pipe;
    process->files[write_fd].kind = KERNEL_FILE_PIPE_WRITE;
    process->files[write_fd].private_data = pipe;
    pair[0] = read_fd;
    pair[1] = write_fd;
    return 0;
}

int32_t i386_scheduler_dup2(uint32_t src_fd, uint32_t dst_fd) {
    struct process *process;

    if (!scheduler_active ||
        src_fd >= NOS_PROCESS_FILE_MAX ||
        dst_fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    process = &tasks[current_task].process;
    if (process->files[src_fd].kind == KERNEL_FILE_NONE) {
        return -1;
    }
    if (src_fd == dst_fd) {
        return (int32_t)dst_fd;
    }
    return file_clone_local(&process->files[dst_fd],
                            &process->files[src_fd])
        ? (int32_t)dst_fd
        : -1;
}

uint32_t i386_scheduler_fd_kind(uint32_t fd) {
    if (!scheduler_active || fd >= NOS_PROCESS_FILE_MAX) {
        return KERNEL_FILE_NONE;
    }
    return tasks[current_task].process.files[fd].kind;
}

int32_t i386_scheduler_close(uint32_t fd) {
    struct file *file;

    if (!scheduler_active || fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &tasks[current_task].process.files[fd];
    if (file->kind == KERNEL_FILE_NONE) {
        return -1;
    }
    file_close_local(file);
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
