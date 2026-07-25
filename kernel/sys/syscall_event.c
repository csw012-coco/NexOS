#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/process.h"

static int syscall_capability_event_copy_from_user(void *dest,
                                                   uint64_t user_addr,
                                                   uint32_t size) {
    return syscall_user_readable(user_addr, size) &&
           syscall_copy_from_user(dest, user_addr, size);
}

uint64_t syscall_handle_capability_event(uint64_t user_info_addr) {
    struct syscall_common_user_input_ops ops;

    ops.copy_from_user = syscall_capability_event_copy_from_user;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    return syscall_common_request_core_capability_event_transfer(
        user_info_addr, &ops);
}

static uint64_t syscall_gui_event_cursor_init(uint64_t user_info_addr) {
    struct syscall_gui_event_cursor cursor;

    if (!syscall_user_writable(user_info_addr, sizeof(cursor))) {
        return syscall_kill_bad_user_pointer();
    }
    (void)syscall_common_request_core_gui_event_cursor_init(&cursor);
    if (!syscall_copy_to_user(user_info_addr, &cursor, sizeof(cursor))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0u;
}

static uint64_t syscall_gui_event_poll(uint64_t user_info_addr) {
    struct syscall_gui_event_poll poll;
    const struct process *proc;
    uint64_t result;

    if (!syscall_user_readable(user_info_addr, sizeof(poll)) ||
        !syscall_user_writable(user_info_addr, sizeof(poll))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&poll, user_info_addr, sizeof(poll))) {
        return syscall_kill_bad_user_pointer();
    }

    proc = process_current();
    result = syscall_common_request_core_gui_event_poll(
        &poll, proc == 0 ? 0u : proc->pid);
    if (!syscall_copy_to_user(user_info_addr, &poll, sizeof(poll))) {
        return syscall_kill_bad_user_pointer();
    }
    return result;
}

static uint64_t syscall_gui_event_grab(void) {
    const struct process *proc = process_current();

    if (proc == 0 || !job_current_process_foreground_allowed()) {
        return (uint64_t)-1;
    }
    return syscall_common_request_core_gui_event_grab(proc->pid, 1);
}

static uint64_t syscall_gui_event_release(void) {
    const struct process *proc = process_current();

    if (proc == 0) {
        return (uint64_t)-1;
    }
    return syscall_common_request_core_gui_event_release(proc->pid);
}

uint64_t syscall_handle_gui_event(uint32_t op, uint64_t user_info_addr) {
    switch (op) {
        case SYS_GUI_EVENT_CURSOR_INIT:
            return syscall_gui_event_cursor_init(user_info_addr);
        case SYS_GUI_EVENT_POLL:
            return syscall_gui_event_poll(user_info_addr);
        case SYS_GUI_EVENT_GRAB:
            return syscall_gui_event_grab();
        case SYS_GUI_EVENT_RELEASE:
            return syscall_gui_event_release();
        default:
            return (uint64_t)-1;
    }
}
