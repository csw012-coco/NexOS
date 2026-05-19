#pragma once

#include "user/public/sysapi.h"

int gui_event_cursor_init(struct syscall_gui_event_cursor *cursor);
int gui_poll_event_with_cursor(struct syscall_gui_event_cursor *cursor, struct syscall_gui_event *event);
int gui_poll_event(struct syscall_gui_event *event);
