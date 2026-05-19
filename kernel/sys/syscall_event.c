#include "kernel/internal/sys/syscall_internal.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "fs/vfs_internal.h"

static void syscall_capability_event_sanitize(struct syscall_capability_event *event) {
    if (event == 0) {
        return;
    }
    event->source[sizeof(event->source) - 1u] = '\0';
    event->action[sizeof(event->action) - 1u] = '\0';
    event->caps[sizeof(event->caps) - 1u] = '\0';
}

uint64_t syscall_handle_capability_event(uint64_t user_info_addr) {
    struct syscall_capability_event event;

    if (!syscall_user_readable(user_info_addr, sizeof(event))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&event, user_info_addr, sizeof(event))) {
        return syscall_kill_bad_user_pointer();
    }
    syscall_capability_event_sanitize(&event);
    vfs_event_capability_emit(&event);
    return 0;
}

static void syscall_gui_event_from_keyboard(struct syscall_gui_event *event,
                                            const struct keyboard_event_record *record) {
    event->type = SYS_GUI_EVENT_KEY;
    event->seq = record->seq;
    event->tick = record->tick;
    event->dx = 0;
    event->dy = 0;
    event->buttons = 0u;
    event->keycode = (uint32_t)record->event.keycode;
    event->ascii = record->event.ascii;
    event->pressed = record->event.pressed;
    event->released = record->event.released;
    event->shift = record->event.shift;
    event->ctrl = record->event.ctrl;
}

static void syscall_gui_event_from_mouse(struct syscall_gui_event *event,
                                         const struct mouse_event_record *record) {
    event->type = SYS_GUI_EVENT_MOUSE;
    event->seq = record->seq;
    event->tick = record->tick;
    event->dx = record->dx;
    event->dy = record->dy;
    event->buttons = record->buttons;
    event->keycode = 0u;
    event->ascii = 0;
    event->pressed = 0u;
    event->released = 0u;
    event->shift = 0u;
    event->ctrl = 0u;
}

static uint64_t syscall_gui_event_cursor_init(uint64_t user_info_addr) {
    struct syscall_gui_event_cursor cursor;

    if (!syscall_user_writable(user_info_addr, sizeof(cursor))) {
        return syscall_kill_bad_user_pointer();
    }
    cursor.keyboard_seq = keyboard_event_queue_latest_seq();
    cursor.mouse_seq = mouse_event_latest_seq();
    if (!syscall_copy_to_user(user_info_addr, &cursor, sizeof(cursor))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0u;
}

static uint64_t syscall_gui_event_poll(uint64_t user_info_addr) {
    struct syscall_gui_event_poll poll;
    struct keyboard_event_record key_record;
    struct mouse_event_record mouse_record;
    struct syscall_gui_event_cursor key_cursor;
    struct syscall_gui_event_cursor mouse_cursor;
    int have_key;
    int have_mouse;
    int use_key;

    if (!syscall_user_readable(user_info_addr, sizeof(poll)) ||
        !syscall_user_writable(user_info_addr, sizeof(poll))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&poll, user_info_addr, sizeof(poll))) {
        return syscall_kill_bad_user_pointer();
    }

    key_cursor = poll.cursor;
    mouse_cursor = poll.cursor;
    have_key = keyboard_event_queue_get_after(&key_cursor.keyboard_seq, &key_record);
    have_mouse = mouse_event_get_after(&mouse_cursor.mouse_seq, &mouse_record);

    poll.event.type = SYS_GUI_EVENT_NONE;
    poll.keyboard_dropped = keyboard_event_queue_dropped();
    poll.mouse_dropped = mouse_event_dropped();
    if (!have_key && !have_mouse) {
        if (!syscall_copy_to_user(user_info_addr, &poll, sizeof(poll))) {
            return syscall_kill_bad_user_pointer();
        }
        return SYS_GUI_EVENT_EMPTY;
    }

    use_key = have_key && (!have_mouse || key_record.tick <= mouse_record.tick);
    if (use_key) {
        poll.cursor.keyboard_seq = key_cursor.keyboard_seq;
        syscall_gui_event_from_keyboard(&poll.event, &key_record);
    } else {
        poll.cursor.mouse_seq = mouse_cursor.mouse_seq;
        syscall_gui_event_from_mouse(&poll.event, &mouse_record);
    }

    if (!syscall_copy_to_user(user_info_addr, &poll, sizeof(poll))) {
        return syscall_kill_bad_user_pointer();
    }
    return SYS_GUI_EVENT_READY;
}

uint64_t syscall_handle_gui_event(uint32_t op, uint64_t user_info_addr) {
    switch (op) {
        case SYS_GUI_EVENT_CURSOR_INIT:
            return syscall_gui_event_cursor_init(user_info_addr);
        case SYS_GUI_EVENT_POLL:
            return syscall_gui_event_poll(user_info_addr);
        default:
            return (uint64_t)-1;
    }
}
