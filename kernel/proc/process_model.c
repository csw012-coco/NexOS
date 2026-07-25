#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/internal/fs/file_internal.h"

void process_model_reset(struct process *proc,
                         uint32_t slot,
                         enum process_state state) {
    if (proc == 0) {
        return;
    }
    proc->pid = 0u;
    proc->slot = slot;
    proc->state = state;
    proc->exit_code = 0;
    proc->has_saved_frame = 0u;
    proc->wake_tick = 0u;
    proc->parent.pid = 0u;
    proc->wait.pid = 0u;
    proc->wait.info_user = 0u;
    proc->name = 0;
    proc->name_storage[0] = '\0';
    proc->cwd_storage[0] = '/';
    proc->cwd_storage[1] = '\0';
    proc->image_kind = PROCESS_IMAGE_NONE;
    proc->entry = 0u;
    proc->stack_top = 0u;
    proc->console_handle = 0;
    proc->address_space = 0;
    process_context_reset(&proc->context);
    for (uint32_t i = 0u; i < PROCESS_FILE_MAX; i++) {
        proc->files[i].kind = KERNEL_FILE_NONE;
        proc->files[i].flags = 0u;
        proc->files[i].offset = 0u;
        proc->files[i].dir_index = 0u;
        proc->files[i].opened_path[0] = '\0';
        proc->files[i].private_data = 0;
        proc->files[i].ops = 0;
    }
}

void process_set_name(struct process *proc, const char *name) {
    uint32_t i = 0u;

    if (proc == 0) {
        return;
    }
    if (name == 0) {
        proc->name_storage[0] = '\0';
        proc->name = 0;
        return;
    }
    while (i + 1u < sizeof(proc->name_storage) && name[i] != '\0') {
        proc->name_storage[i] = name[i];
        i++;
    }
    proc->name_storage[i] = '\0';
    proc->name = proc->name_storage;
}

void process_refresh_name_ptr(struct process *proc) {
    if (proc != 0) {
        proc->name =
            proc->name_storage[0] != '\0' ? proc->name_storage : 0;
    }
}

void process_snapshot_fill(struct process_snapshot *out,
                           const struct process *proc) {
    uint32_t i;

    if (out == 0) {
        return;
    }
    out->pid = 0u;
    out->slot = 0u;
    out->state = PROCESS_STATE_FREE;
    out->exit_code = 0;
    out->wake_tick = 0u;
    out->image_kind = PROCESS_IMAGE_NONE;
    for (i = 0u; i < sizeof(out->name); i++) {
        out->name[i] = '\0';
    }
    if (proc == 0) {
        return;
    }
    out->pid = proc->pid;
    out->slot = proc->slot;
    out->state = (uint32_t)proc->state;
    out->exit_code = proc->exit_code;
    out->wake_tick = proc->wake_tick;
    out->image_kind = (uint32_t)proc->image_kind;
    for (i = 0u;
         i + 1u < sizeof(out->name) && proc->name_storage[i] != '\0';
         i++) {
        out->name[i] = proc->name_storage[i];
    }
}

int process_lifecycle_child_matches_wait(const struct process *child,
                                         uint32_t child_parent_pid,
                                         uint32_t waiter_pid,
                                         uint32_t requested_pid,
                                         uint32_t wait_last_pid) {
    if (child == 0 || child_parent_pid != waiter_pid) {
        return 0;
    }
    return requested_pid == wait_last_pid || child->pid == requested_pid;
}

void process_lifecycle_prepare_wait(struct process *waiter) {
    if (waiter != 0) {
        waiter->state = PROCESS_STATE_WAITING;
    }
}

void process_lifecycle_clear_wait(struct process *waiter) {
    if (waiter == 0) {
        return;
    }
    waiter->wait.pid = 0u;
    waiter->wait.info_user = 0u;
}

int process_lifecycle_waiter_matches_exit(const struct process *waiter,
                                          uint32_t waiter_wait_pid,
                                          uint32_t exited_pid) {
    if (waiter == 0 || waiter->state != PROCESS_STATE_WAITING) {
        return 0;
    }
    return waiter_wait_pid == 0u || waiter_wait_pid == exited_pid;
}

void process_lifecycle_finish_wait(struct process *waiter,
                                   int32_t exit_code) {
    if (waiter == 0) {
        return;
    }
    process_context_set_return_value(&waiter->context, (uint32_t)exit_code);
    waiter->state = PROCESS_STATE_READY;
}

int process_lifecycle_wake_file_waiter(struct process *proc,
                                       void *private_data,
                                       uint8_t file_kind) {
    if (proc == 0 ||
        proc->state != PROCESS_STATE_WAITING ||
        private_data == 0) {
        return 0;
    }
    for (uint32_t fd = 0u; fd < PROCESS_FILE_MAX; fd++) {
        struct file *file = &proc->files[fd];

        if (file_is_active(file) &&
            file->kind == file_kind &&
            file->private_data == private_data) {
            proc->state = PROCESS_STATE_READY;
            return 1;
        }
    }
    return 0;
}

int process_lifecycle_can_signal_child(const struct process *child,
                                       uint32_t child_parent_pid,
                                       uint32_t caller_pid,
                                       uint32_t target_pid) {
    return child != 0 &&
           child->pid == target_pid &&
           child_parent_pid == caller_pid &&
           child->state != PROCESS_STATE_FREE &&
           child->state != PROCESS_STATE_EXITED;
}

int process_lifecycle_find_wait_child(struct process *const *slots,
                                      uint32_t capacity,
                                      uint32_t waiter_pid,
                                      uint32_t requested_pid,
                                      uint32_t wait_last_pid,
                                      uint32_t *slot_out,
                                      int *exited_out) {
    if (slot_out != 0) {
        *slot_out = 0u;
    }
    if (exited_out != 0) {
        *exited_out = 0;
    }
    if (slots == 0 || waiter_pid == 0u) {
        return 0;
    }
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        struct process *child = slots[slot];

        if (!process_lifecycle_child_matches_wait(child,
                                                  child != 0
                                                      ? child->parent.pid
                                                      : 0u,
                                                  waiter_pid,
                                                  requested_pid,
                                                  wait_last_pid)) {
            continue;
        }
        if (requested_pid == wait_last_pid &&
            child->state != PROCESS_STATE_EXITED) {
            continue;
        }
        if (slot_out != 0) {
            *slot_out = slot;
        }
        if (exited_out != 0) {
            *exited_out = child->state == PROCESS_STATE_EXITED;
        }
        return 1;
    }
    return 0;
}

uint32_t process_lifecycle_wake_exit_waiters(
    struct process **slots,
    uint32_t capacity,
    uint32_t exited_pid,
    int32_t exit_code,
    void (*copy_wait_info)(uint32_t slot,
                           const struct process *exited,
                           void *context),
    void *copy_context) {
    struct process *exited = 0;
    uint32_t woke = 0u;

    if (slots == 0 || exited_pid == 0u) {
        return 0u;
    }
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        if (slots[slot] != 0 && slots[slot]->pid == exited_pid) {
            exited = slots[slot];
            break;
        }
    }
    if (exited == 0) {
        return 0u;
    }
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        struct process *waiter = slots[slot];

        if (!process_lifecycle_waiter_matches_exit(
                waiter,
                waiter != 0 ? waiter->wait.pid : 0u,
                exited_pid)) {
            continue;
        }
        process_lifecycle_finish_wait(waiter, exit_code);
        if (copy_wait_info != 0) {
            copy_wait_info(slot, exited, copy_context);
        }
        process_lifecycle_clear_wait(waiter);
        woke++;
    }
    return woke;
}

void process_lifecycle_mark_exited_for_scheduler(struct process *proc,
                                                 int32_t exit_code) {
    if (proc == 0) {
        return;
    }
    proc->exit_code = exit_code;
    proc->state = PROCESS_STATE_EXITED;
    proc->wake_tick = 0u;
}

void process_forget_file_array(struct file files[PROCESS_FILE_MAX]) {
    for (uint32_t fd = 0u; fd < PROCESS_FILE_MAX; fd++) {
        file_reset(&files[fd]);
    }
}

void process_discard_file_array(struct file files[PROCESS_FILE_MAX]) {
    for (uint32_t fd = 0u; fd < PROCESS_FILE_MAX; fd++) {
        file_discard(&files[fd]);
    }
}

int process_clone_spawn_files(const struct process *parent,
                              struct file out[PROCESS_FILE_MAX]) {
    process_forget_file_array(out);
    if (parent == 0) {
        return 1;
    }
    for (uint32_t fd = 0u; fd < PROCESS_FILE_MAX; fd++) {
        const struct file *src = &parent->files[fd];

        if (!file_is_active(src) ||
            (src->flags & KERNEL_FILE_CLOSE_ON_SPAWN) != 0u) {
            continue;
        }
        if (!file_clone(&out[fd], src)) {
            process_discard_file_array(out);
            return 0;
        }
    }
    return 1;
}

int process_clone_all_files(const struct process *parent,
                            struct file out[PROCESS_FILE_MAX]) {
    process_forget_file_array(out);
    if (parent == 0) {
        return 1;
    }
    for (uint32_t fd = 0u; fd < PROCESS_FILE_MAX; fd++) {
        const struct file *src = &parent->files[fd];

        if (!file_is_active(src)) {
            continue;
        }
        if (!file_clone(&out[fd], src)) {
            process_discard_file_array(out);
            return 0;
        }
    }
    return 1;
}

static void process_refresh_console_from_files(struct process *proc) {
    void *tty_handle;

    if (proc == 0) {
        return;
    }
    tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDIN]);
    if (tty_handle == 0) {
        tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDOUT]);
    }
    if (tty_handle == 0) {
        tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDERR]);
    }
    if (tty_handle != 0) {
        proc->console_handle = tty_handle;
    }
}

void process_install_cloned_files(struct process *proc,
                                  struct file cloned[PROCESS_FILE_MAX]) {
    if (proc == 0) {
        process_discard_file_array(cloned);
        return;
    }
    for (uint32_t fd = 0u; fd < PROCESS_FILE_MAX; fd++) {
        file_discard(&proc->files[fd]);
        proc->files[fd] = cloned[fd];
        file_reset(&cloned[fd]);
    }
    process_refresh_console_from_files(proc);
}

const char *process_cwd(const struct process *proc) {
    return proc == 0 || proc->cwd_storage[0] == '\0'
        ? "/"
        : proc->cwd_storage;
}

void process_set_cwd(struct process *proc, const char *path) {
    uint32_t i = 0u;

    if (proc == 0) {
        return;
    }
    if (path == 0 || path[0] == '\0') {
        proc->cwd_storage[0] = '/';
        proc->cwd_storage[1] = '\0';
        return;
    }
    while (path[i] != '\0' && i + 1u < sizeof(proc->cwd_storage)) {
        proc->cwd_storage[i] = path[i];
        i++;
    }
    proc->cwd_storage[i] = '\0';
}
