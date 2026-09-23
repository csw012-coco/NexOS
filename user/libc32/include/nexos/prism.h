#pragma once

#include <stddef.h>
#include <stdint.h>
#include "nexos/gfx.h"
#include "nexos/gui.h"
#include "sys/ipc.h"
#include "sys/mman.h"

/*
 * Kernel-facing substrate for the userspace Prism window system.
 *
 * Prism itself is not a kernel feature. These wrappers name the small set of
 * kernel primitives a Prism server needs: display output, raw input focus,
 * shared memory, and message queues.
 */

typedef struct syscall_gfx_info prism_kernel_display_info;
typedef struct syscall_gui_event_cursor prism_kernel_input_cursor;
typedef struct syscall_gui_event prism_kernel_input_event;
typedef struct syscall_gfx_batch_entry prism_kernel_gfx_batch_entry;

#ifndef EAGAIN
#define EAGAIN NEX_ERR_AGAIN
#endif
#ifndef EINVAL
#define EINVAL NEX_ERR_INVAL
#endif
#ifndef EIO
#define EIO NEX_ERR_IO
#endif
#ifndef ENOMEM
#define ENOMEM NEX_ERR_NOMEM
#endif

int snprintf(char *dst, uint32_t size, const char *fmt, ...);
void *memset(void *dst, int value, size_t count);
int getpid(void);
int tty_query(uint32_t fd, struct syscall_tty_info *info);
uint32_t ticks(void);
void sleep(uint32_t ms);

#define PRISM_SERVER_QUEUE "prism.server"
#define PRISM_SERVER_QUEUE_MAX 31u
#define PRISM_REPLY_NAME_MAX 31u
#define PRISM_EVENT_QUEUE_MAX 31u
#define PRISM_SURFACE_NAME_MAX 31u
#define PRISM_WINDOW_TITLE_MAX 31u
#define PRISM_WINDOW_MAX 16u
#define PRISM_WINDOW_CONTENT_X 8
#define PRISM_WINDOW_CONTENT_Y 26
#define PRISM_WINDOW_CONTENT_RIGHT 8
#define PRISM_WINDOW_CONTENT_BOTTOM 4
#define PRISM_WINDOW_MIN_WIDTH 96u
#define PRISM_WINDOW_MIN_HEIGHT 72u
#define PRISM_KEY_MAX 128u
#define PRISM_MOUSE_LEFT SYS_GUI_MOUSE_LEFT
#define PRISM_MOUSE_RIGHT SYS_GUI_MOUSE_RIGHT
#define PRISM_MOUSE_MIDDLE SYS_GUI_MOUSE_MIDDLE

enum prism_kernel_event_result {
    PRISM_KERNEL_EVENT_EMPTY = SYS_GUI_EVENT_EMPTY,
    PRISM_KERNEL_EVENT_READY = SYS_GUI_EVENT_READY
};

enum prism_kernel_surface_format {
    PRISM_KERNEL_SURFACE_XRGB8888 = SYS_GFX_FORMAT_XRGB8888
};

enum prism_window_flag {
    PRISM_WINDOW_NORMAL = 0u,
    PRISM_WINDOW_DESKTOP = 1u << 0
};

enum prism_configure_flag {
    PRISM_CONFIGURE_X = 1u << 0,
    PRISM_CONFIGURE_Y = 1u << 1,
    PRISM_CONFIGURE_WIDTH = 1u << 2,
    PRISM_CONFIGURE_HEIGHT = 1u << 3,
    PRISM_CONFIGURE_RECT = PRISM_CONFIGURE_X |
                           PRISM_CONFIGURE_Y |
                           PRISM_CONFIGURE_WIDTH |
                           PRISM_CONFIGURE_HEIGHT
};

enum prism_present_flag {
    PRISM_PRESENT_FULL = 0u,
    PRISM_PRESENT_RECT = 1u << 0
};

enum prism_message_type {
    PRISM_MSG_NONE = 0,
    PRISM_MSG_HELLO = 1,
    PRISM_MSG_CREATE_WINDOW = 2,
    PRISM_MSG_DESTROY_WINDOW = 3,
    PRISM_MSG_PRESENT_WINDOW = 4,
    PRISM_MSG_SHUTDOWN = 5,
    PRISM_MSG_ATTACH_SURFACE = 6,
    PRISM_MSG_CONFIGURE_WINDOW = 7,
    PRISM_MSG_REPLY = 0x80000000u
};

enum prism_event_type {
    PRISM_EVENT_NONE = 0,
    PRISM_EVENT_KEY = 1,
    PRISM_EVENT_MOUSE = 2,
    PRISM_EVENT_CLOSE_REQUEST = 3,
    PRISM_EVENT_CLOSE = PRISM_EVENT_CLOSE_REQUEST,
    PRISM_EVENT_FOCUS = 4,
    PRISM_EVENT_MOVE = 5,
    PRISM_EVENT_RESIZE = 6
};

struct prism_rect {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

struct prism_message {
    uint32_t type;
    uint32_t client_pid;
    uint32_t window_id;
    uint32_t flags;
    struct prism_rect rect;
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t surface_pitch;
    uint32_t surface_format;
    char title[PRISM_WINDOW_TITLE_MAX + 1u];
    char reply_queue[PRISM_REPLY_NAME_MAX + 1u];
    char event_queue[PRISM_EVENT_QUEUE_MAX + 1u];
    char surface_name[PRISM_SURFACE_NAME_MAX + 1u];
};

struct prism_reply {
    uint32_t type;
    int32_t status;
    uint32_t window_id;
    uint32_t reserved;
    struct prism_rect rect;
};

struct prism_event {
    uint32_t type;
    uint32_t window_id;
    uint32_t tick;
    struct prism_rect rect;
    uint32_t content_width;
    uint32_t content_height;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint32_t buttons;
    uint32_t button_pressed;
    uint32_t button_released;
    uint32_t keycode;
    char ascii;
    uint8_t pressed;
    uint8_t released;
    uint8_t repeat;
    uint8_t extended;
    uint8_t shift;
    uint8_t ctrl;
    uint8_t alt;
    uint8_t caps_lock;
    uint8_t num_lock;
    uint8_t scroll_lock;
};

struct prism_client {
    mqd_t server_queue;
    mqd_t reply_queue;
    mqd_t event_queue;
    char server_queue_name[PRISM_SERVER_QUEUE_MAX + 1u];
    char reply_queue_name[PRISM_REPLY_NAME_MAX + 1u];
    char event_queue_name[PRISM_EVENT_QUEUE_MAX + 1u];
};

struct prism_surface {
    int handle;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    uint32_t size;
    uint32_t *pixels;
    char name[PRISM_SURFACE_NAME_MAX + 1u];
};

static inline int prism_event_is_key_press(const struct prism_event *event) {
    return event != 0 &&
           event->type == PRISM_EVENT_KEY &&
           event->pressed != 0u;
}

static inline int prism_event_is_key_release(const struct prism_event *event) {
    return event != 0 &&
           event->type == PRISM_EVENT_KEY &&
           event->released != 0u;
}

static inline int prism_event_is_mouse_press(const struct prism_event *event,
                                             uint32_t button) {
    return event != 0 &&
           event->type == PRISM_EVENT_MOUSE &&
           (event->button_pressed & button) != 0u;
}

static inline int prism_event_is_mouse_release(const struct prism_event *event,
                                               uint32_t button) {
    return event != 0 &&
           event->type == PRISM_EVENT_MOUSE &&
           (event->button_released & button) != 0u;
}

static inline int prism_event_is_configure(const struct prism_event *event) {
    return event != 0 &&
           (event->type == PRISM_EVENT_MOVE ||
            event->type == PRISM_EVENT_RESIZE);
}

static inline int prism_event_needs_surface_resize(
    const struct prism_event *event,
    const struct prism_surface *surface) {
    return event != 0 &&
           surface != 0 &&
           event->type == PRISM_EVENT_RESIZE &&
           (surface->width != event->content_width ||
            surface->height != event->content_height);
}

static inline void prism_client_copy_text(char *dst,
                                          uint32_t dst_size,
                                          const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    if (src == 0) {
        src = "";
    }
    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static inline int prism_server_queue_for_current_tty(char *dst,
                                                     uint32_t dst_size) {
    struct syscall_tty_info tty;

    if (dst == 0 || dst_size == 0u) {
        return -EINVAL;
    }
    if (tty_query(0u, &tty) > 0 && tty.kind == SYS_TTY_KIND_VIRTUAL) {
        if (snprintf(dst,
                     dst_size,
                     "prism.server.tty%u",
                     tty.index + 1u) < 0) {
            return -EINVAL;
        }
        return 0;
    }
    prism_client_copy_text(dst, dst_size, PRISM_SERVER_QUEUE);
    return 0;
}

static inline const char *prism_client_server_queue_name(
    const struct prism_client *client) {
    if (client == 0 || client->server_queue_name[0] == '\0') {
        return PRISM_SERVER_QUEUE;
    }
    return client->server_queue_name;
}

static inline int prism_client_connect(struct prism_client *client) {
    static uint32_t sequence;

    if (client == 0) {
        return -EINVAL;
    }
    memset(client, 0, sizeof(*client));
    if (prism_server_queue_for_current_tty(client->server_queue_name,
                                           sizeof(client->server_queue_name)) != 0) {
        return -EINVAL;
    }
    client->server_queue = mq_open(client->server_queue_name, 0);
    if (client->server_queue < 0) {
        return client->server_queue;
    }
    if (snprintf(client->reply_queue_name,
                 sizeof(client->reply_queue_name),
                 "prism.%u.%u",
                 (uint32_t)getpid(),
                 ++sequence) < 0) {
        return -EINVAL;
    }
    (void)mq_unlink(client->reply_queue_name);
    client->reply_queue = mq_open(client->reply_queue_name,
                                  IPC_CREATE | IPC_EXCL);
    if (client->reply_queue < 0) {
        return client->reply_queue;
    }
    if (snprintf(client->event_queue_name,
                 sizeof(client->event_queue_name),
                 "prisme.%u.%u",
                 (uint32_t)getpid(),
                 sequence) < 0) {
        (void)mq_unlink(client->reply_queue_name);
        return -EINVAL;
    }
    (void)mq_unlink(client->event_queue_name);
    client->event_queue = mq_open(client->event_queue_name,
                                  IPC_CREATE | IPC_EXCL);
    if (client->event_queue < 0) {
        (void)mq_unlink(client->reply_queue_name);
        return client->event_queue;
    }
    return 0;
}

static inline void prism_client_disconnect(struct prism_client *client) {
    if (client != 0 && client->reply_queue_name[0] != '\0') {
        (void)mq_unlink(client->reply_queue_name);
        client->reply_queue_name[0] = '\0';
    }
    if (client != 0 && client->event_queue_name[0] != '\0') {
        (void)mq_unlink(client->event_queue_name);
        client->event_queue_name[0] = '\0';
    }
}

static inline int prism_client_request(struct prism_client *client,
                                       struct prism_message *message,
                                       struct prism_reply *reply,
                                       uint32_t timeout_ticks) {
    uint32_t start;

    if (client == 0 || message == 0 || reply == 0) {
        return -EINVAL;
    }
    message->client_pid = (uint32_t)getpid();
    prism_client_copy_text(message->reply_queue,
                           sizeof(message->reply_queue),
                           client->reply_queue_name);
    prism_client_copy_text(message->event_queue,
                           sizeof(message->event_queue),
                           client->event_queue_name);
    if (mq_send(client->server_queue, message, sizeof(*message), 0) != 0) {
        return -EIO;
    }
    start = ticks();
    for (;;) {
        int rc = mq_receive(client->reply_queue,
                            reply,
                            sizeof(*reply),
                            IPC_NONBLOCK);

        if (rc == (int)sizeof(*reply) && reply->type == PRISM_MSG_REPLY) {
            return reply->status;
        }
        if (rc < 0 && rc != -EAGAIN) {
            return rc;
        }
        if ((uint32_t)(ticks() - start) >= timeout_ticks) {
            return -EAGAIN;
        }
        sleep(1u);
    }
}

static inline int prism_client_poll_event(struct prism_client *client,
                                          struct prism_event *event) {
    int rc;

    if (client == 0 || event == 0) {
        return -EINVAL;
    }
    rc = mq_receive(client->event_queue, event, sizeof(*event), IPC_NONBLOCK);
    if (rc == (int)sizeof(*event)) {
        return 1;
    }
    if (rc == -EAGAIN) {
        memset(event, 0, sizeof(*event));
        return 0;
    }
    return rc;
}

static inline int prism_client_wait_event(struct prism_client *client,
                                          struct prism_event *event,
                                          uint32_t timeout_ticks) {
    uint32_t start;

    if (client == 0 || event == 0) {
        return -EINVAL;
    }
    start = ticks();
    for (;;) {
        int rc = prism_client_poll_event(client, event);

        if (rc != 0) {
            return rc;
        }
        if ((uint32_t)(ticks() - start) >= timeout_ticks) {
            return 0;
        }
        sleep(1u);
    }
}

static inline int prism_client_hello(struct prism_client *client,
                                     uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;

    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_HELLO;
    return prism_client_request(client, &message, &reply, timeout_ticks);
}

static inline int prism_client_create_window_ex(struct prism_client *client,
                                                const struct prism_rect *rect,
                                                const char *title,
                                                uint32_t flags,
                                                uint32_t *window_id,
                                                struct prism_rect *actual,
                                                uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;
    int rc;

    if (rect == 0) {
        return -EINVAL;
    }
    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_CREATE_WINDOW;
    message.flags = flags;
    message.rect = *rect;
    prism_client_copy_text(message.title, sizeof(message.title), title);
    rc = prism_client_request(client, &message, &reply, timeout_ticks);
    if (rc == 0) {
        if (window_id != 0) {
            *window_id = reply.window_id;
        }
        if (actual != 0) {
            *actual = reply.rect;
        }
    }
    return rc;
}

static inline int prism_client_create_window(struct prism_client *client,
                                             const struct prism_rect *rect,
                                             const char *title,
                                             uint32_t *window_id,
                                             struct prism_rect *actual,
                                             uint32_t timeout_ticks) {
    return prism_client_create_window_ex(client,
                                         rect,
                                         title,
                                         PRISM_WINDOW_NORMAL,
                                         window_id,
                                         actual,
                                         timeout_ticks);
}

static inline int prism_client_create_desktop_window(struct prism_client *client,
                                                    uint32_t *window_id,
                                                    struct prism_rect *actual,
                                                    uint32_t timeout_ticks) {
    struct prism_rect rect;

    memset(&rect, 0, sizeof(rect));
    rect.width = 1u;
    rect.height = 1u;
    return prism_client_create_window_ex(client,
                                         &rect,
                                         "Desktop",
                                         PRISM_WINDOW_DESKTOP,
                                         window_id,
                                         actual,
                                         timeout_ticks);
}

static inline int prism_client_destroy_window(struct prism_client *client,
                                              uint32_t window_id,
                                              uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;

    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_DESTROY_WINDOW;
    message.window_id = window_id;
    return prism_client_request(client, &message, &reply, timeout_ticks);
}

static inline int prism_client_configure_window(struct prism_client *client,
                                                uint32_t window_id,
                                                const struct prism_rect *rect,
                                                struct prism_rect *actual,
                                                uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;
    int rc;

    if (rect == 0) {
        return -EINVAL;
    }
    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_CONFIGURE_WINDOW;
    message.window_id = window_id;
    message.flags = PRISM_CONFIGURE_RECT;
    message.rect = *rect;
    rc = prism_client_request(client, &message, &reply, timeout_ticks);
    if (rc == 0 && actual != 0) {
        *actual = reply.rect;
    }
    return rc;
}

static inline int prism_client_move_window(struct prism_client *client,
                                           uint32_t window_id,
                                           int32_t x,
                                           int32_t y,
                                           uint32_t timeout_ticks) {
    struct prism_rect rect;

    memset(&rect, 0, sizeof(rect));
    rect.x = x;
    rect.y = y;
    {
        struct prism_message message;
        struct prism_reply reply;

        memset(&message, 0, sizeof(message));
        memset(&reply, 0, sizeof(reply));
        message.type = PRISM_MSG_CONFIGURE_WINDOW;
        message.window_id = window_id;
        message.flags = PRISM_CONFIGURE_X | PRISM_CONFIGURE_Y;
        message.rect = rect;
        return prism_client_request(client, &message, &reply, timeout_ticks);
    }
}

static inline int prism_client_resize_window(struct prism_client *client,
                                             uint32_t window_id,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t timeout_ticks) {
    struct prism_rect rect;

    memset(&rect, 0, sizeof(rect));
    rect.width = width;
    rect.height = height;
    {
        struct prism_message message;
        struct prism_reply reply;

        memset(&message, 0, sizeof(message));
        memset(&reply, 0, sizeof(reply));
        message.type = PRISM_MSG_CONFIGURE_WINDOW;
        message.window_id = window_id;
        message.flags = PRISM_CONFIGURE_WIDTH | PRISM_CONFIGURE_HEIGHT;
        message.rect = rect;
        return prism_client_request(client, &message, &reply, timeout_ticks);
    }
}

static inline int prism_client_present_window(struct prism_client *client,
                                              uint32_t window_id,
                                              uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;

    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_PRESENT_WINDOW;
    message.window_id = window_id;
    message.flags = PRISM_PRESENT_FULL;
    return prism_client_request(client, &message, &reply, timeout_ticks);
}

static inline int prism_client_present_rect(struct prism_client *client,
                                            uint32_t window_id,
                                            const struct prism_rect *rect,
                                            struct prism_rect *actual,
                                            uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;
    int rc;

    if (rect == 0) {
        return -EINVAL;
    }
    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_PRESENT_WINDOW;
    message.window_id = window_id;
    message.flags = PRISM_PRESENT_RECT;
    message.rect = *rect;
    rc = prism_client_request(client, &message, &reply, timeout_ticks);
    if (rc == 0 && actual != 0) {
        *actual = reply.rect;
    }
    return rc;
}

static inline int prism_client_present_surface(struct prism_client *client,
                                               uint32_t window_id,
                                               const struct prism_surface *surface,
                                               uint32_t timeout_ticks) {
    struct prism_rect rect;

    if (surface == 0) {
        return -EINVAL;
    }
    memset(&rect, 0, sizeof(rect));
    rect.width = surface->width;
    rect.height = surface->height;
    return prism_client_present_rect(client,
                                     window_id,
                                     &rect,
                                     0,
                                     timeout_ticks);
}

static inline int prism_client_shutdown(struct prism_client *client,
                                        uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;

    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_SHUTDOWN;
    return prism_client_request(client, &message, &reply, timeout_ticks);
}

static inline int prism_client_create_surface(struct prism_surface *surface,
                                              const char *name,
                                              uint32_t width,
                                              uint32_t height) {
    uint32_t pitch;
    uint32_t size;

    if (surface == 0 || name == 0 || width == 0u || height == 0u) {
        return -EINVAL;
    }
    pitch = width * sizeof(uint32_t);
    if (pitch / sizeof(uint32_t) != width ||
        height > 0xffffffffu / pitch) {
        return -EINVAL;
    }
    size = pitch * height;
    memset(surface, 0, sizeof(*surface));
    prism_client_copy_text(surface->name, sizeof(surface->name), name);
    (void)shm_unlink(surface->name);
    surface->handle = shm_open(surface->name, size, SHM_CREATE | SHM_EXCL);
    if (surface->handle < 0) {
        return surface->handle;
    }
    surface->pixels = (uint32_t *)mmap(0,
                                       size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED,
                                       surface->handle,
                                       0);
    if (surface->pixels == MAP_FAILED) {
        (void)shm_unlink(surface->name);
        return -ENOMEM;
    }
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->format = PRISM_KERNEL_SURFACE_XRGB8888;
    surface->size = size;
    return 0;
}

static inline void prism_client_destroy_surface(struct prism_surface *surface) {
    if (surface == 0) {
        return;
    }
    if (surface->pixels != 0 && surface->pixels != MAP_FAILED) {
        (void)munmap(surface->pixels, surface->size);
    }
    if (surface->name[0] != '\0') {
        (void)shm_unlink(surface->name);
    }
    memset(surface, 0, sizeof(*surface));
}

static inline int prism_client_resize_surface(struct prism_surface *surface,
                                              const char *name,
                                              uint32_t width,
                                              uint32_t height) {
    struct prism_surface next;
    int rc;

    memset(&next, 0, sizeof(next));
    rc = prism_client_create_surface(&next, name, width, height);
    if (rc != 0) {
        return rc;
    }
    prism_client_destroy_surface(surface);
    *surface = next;
    return 0;
}

static inline int prism_client_attach_surface(struct prism_client *client,
                                              uint32_t window_id,
                                              const struct prism_surface *surface,
                                              uint32_t timeout_ticks) {
    struct prism_message message;
    struct prism_reply reply;

    if (surface == 0 || surface->pixels == 0 || surface->name[0] == '\0') {
        return -EINVAL;
    }
    memset(&message, 0, sizeof(message));
    memset(&reply, 0, sizeof(reply));
    message.type = PRISM_MSG_ATTACH_SURFACE;
    message.window_id = window_id;
    message.surface_width = surface->width;
    message.surface_height = surface->height;
    message.surface_pitch = surface->pitch;
    message.surface_format = surface->format;
    prism_client_copy_text(message.surface_name,
                           sizeof(message.surface_name),
                           surface->name);
    return prism_client_request(client, &message, &reply, timeout_ticks);
}

static inline int prism_client_attach_resized_surface(
    struct prism_client *client,
    uint32_t window_id,
    const struct prism_surface *surface,
    const struct prism_event *resize_event,
    uint32_t timeout_ticks) {
    if (resize_event == 0 || resize_event->type != PRISM_EVENT_RESIZE) {
        return -EINVAL;
    }
    if (surface == 0 ||
        surface->width != resize_event->content_width ||
        surface->height != resize_event->content_height) {
        return -EINVAL;
    }
    return prism_client_attach_surface(client,
                                       window_id,
                                       surface,
                                       timeout_ticks);
}

static inline int prism_kernel_display_info_query(
    prism_kernel_display_info *info) {
    return gfx_info(info);
}

static inline int prism_kernel_gfx_batch_begin(
    prism_kernel_gfx_batch_entry *entries,
    uint32_t capacity) {
    return gfx_batch_begin(entries, capacity);
}

static inline int prism_kernel_gfx_batch_submit(int present) {
    return gfx_batch_submit(present ? SYS_GFX_BATCH_PRESENT : 0u);
}

static inline void prism_kernel_gfx_batch_cancel(void) {
    gfx_batch_cancel();
}

static inline int prism_kernel_display_clear(uint32_t rgb) {
    return gfx_clear(rgb);
}

static inline int prism_kernel_display_fill_rect(int32_t x,
                                                 int32_t y,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t rgb) {
    return gfx_fill_rect(x, y, width, height, rgb);
}

static inline int prism_kernel_display_draw_rect(int32_t x,
                                                 int32_t y,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t rgb) {
    return gfx_draw_rect(x, y, width, height, rgb);
}

static inline int prism_kernel_display_draw_line(int32_t x0,
                                                 int32_t y0,
                                                 int32_t x1,
                                                 int32_t y1,
                                                 uint32_t rgb) {
    return gfx_draw_line(x0, y0, x1, y1, rgb);
}

static inline int prism_kernel_display_fill_circle(int32_t x,
                                                   int32_t y,
                                                   uint32_t radius,
                                                   uint32_t rgb) {
    return gfx_fill_circle(x, y, radius, rgb);
}

static inline int prism_kernel_display_draw_circle(int32_t x,
                                                   int32_t y,
                                                   uint32_t radius,
                                                   uint32_t rgb) {
    return gfx_draw_circle(x, y, radius, rgb);
}

static inline int prism_kernel_display_blit_xrgb8888(
    const uint32_t *pixels,
    uint32_t pitch,
    int32_t dst_x,
    int32_t dst_y,
    uint32_t width,
    uint32_t height) {
    return gfx_blit(pixels, pitch, dst_x, dst_y, width, height);
}

static inline int prism_kernel_display_present(void) {
    return gfx_present();
}

static inline int prism_kernel_input_cursor_init(
    prism_kernel_input_cursor *cursor) {
    return gui_event_cursor_init(cursor);
}

static inline int prism_kernel_input_poll(
    prism_kernel_input_cursor *cursor,
    prism_kernel_input_event *event) {
    return cursor != 0 ? gui_poll_event_with_cursor(cursor, event)
                       : gui_poll_event(event);
}

static inline int prism_kernel_input_grab(void) {
    return gui_input_grab();
}

static inline int prism_kernel_input_release(void) {
    return gui_input_release();
}

static inline int prism_kernel_shm_open(const char *name,
                                        size_t size,
                                        int flags) {
    return shm_open(name, size, flags);
}

static inline int prism_kernel_shm_unlink(const char *name) {
    return shm_unlink(name);
}

static inline void *prism_kernel_shm_map(int handle, size_t size) {
    return mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
}

static inline int prism_kernel_shm_unmap(void *addr, size_t size) {
    return munmap(addr, size);
}

static inline mqd_t prism_kernel_ipc_open(const char *name, int flags) {
    return mq_open(name, flags);
}

static inline int prism_kernel_ipc_unlink(const char *name) {
    return mq_unlink(name);
}

static inline int prism_kernel_ipc_send(mqd_t queue,
                                        const void *data,
                                        size_t size,
                                        int flags) {
    return mq_send(queue, data, size, flags);
}

static inline int prism_kernel_ipc_receive(mqd_t queue,
                                           void *data,
                                           size_t capacity,
                                           int flags) {
    return mq_receive(queue, data, capacity, flags);
}
