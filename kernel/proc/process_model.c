#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/internal/proc/process_types_internal.h"

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
