#include <nlibc.h>

enum {
    PRISM_TITLEBAR_HEIGHT = 22,
    PRISM_CLOSE_SIZE = 10,
    PRISM_RESIZE_GRIP_SIZE = 14,
    PRISM_REGION_MAX = 64,
    PRISM_SMOKE_FRAMES = 12
};

struct prism_window {
    uint32_t id;
    uint32_t owner_pid;
    uint32_t flags;
    struct prism_rect rect;
    uint32_t color;
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t surface_pitch;
    uint32_t surface_format;
    uint32_t surface_size;
    int surface_handle;
    mqd_t event_queue;
    uint32_t *surface_pixels;
    uint8_t open;
    char title[PRISM_WINDOW_TITLE_MAX + 1u];
    char event_queue_name[PRISM_EVENT_QUEUE_MAX + 1u];
    char surface_name[PRISM_SURFACE_NAME_MAX + 1u];
};

struct prism_server {
    prism_kernel_display_info display;
    prism_kernel_input_cursor input_cursor;
    struct prism_window windows[PRISM_WINDOW_MAX];
    uint8_t z_order[PRISM_WINDOW_MAX];
    uint32_t next_window_id;
    int focused;
    int dragging;
    int resizing;
    int pointer_capture;
    int32_t drag_dx;
    int32_t drag_dy;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t buttons;
    uint8_t key_down[PRISM_KEY_MAX];
    uint8_t shift;
    uint8_t ctrl;
    uint8_t alt;
    uint8_t caps_lock;
    uint8_t num_lock;
    uint8_t scroll_lock;
    mqd_t queue;
    char queue_name[PRISM_SERVER_QUEUE_MAX + 1u];
    uint32_t home_tty_kind;
    uint32_t home_tty_index;
    uint8_t has_home_tty;
    uint8_t has_damage;
    struct prism_rect damage;
    uint8_t input_grabbed;
    uint8_t running;
    uint8_t debug_exit;
};

struct prism_region {
    struct prism_rect rects[PRISM_REGION_MAX];
    uint32_t count;
};

static int32_t prism_clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int prism_streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int32_t prism_abs_i32(int32_t value) {
    return value < 0 ? -value : value;
}

static int prism_query_tty(struct syscall_tty_info *info) {
    return tty_query(STDIN_FILENO, info) > 0 &&
           info->kind == SYS_TTY_KIND_VIRTUAL;
}

static int prism_home_tty_active(const struct prism_server *server) {
    struct syscall_tty_info info;

    if (!server->has_home_tty) {
        return 1;
    }
    if (!prism_query_tty(&info)) {
        return 1;
    }
    return info.kind == server->home_tty_kind &&
           info.index == server->home_tty_index &&
           info.active != 0u;
}

static void prism_update_home_tty(struct prism_server *server, int active) {
    if (active) {
        if (!server->input_grabbed && prism_kernel_input_grab() == 0) {
            server->input_grabbed = 1u;
            (void)prism_kernel_input_cursor_init(&server->input_cursor);
        }
        return;
    }
    if (server->input_grabbed) {
        (void)prism_kernel_input_release();
        server->input_grabbed = 0u;
    }
}

static void prism_copy_text(char *dst, uint32_t dst_size, const char *src) {
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

static int prism_point_in_rect(int32_t px, int32_t py, const struct prism_rect *rect) {
    return rect != 0 &&
           px >= rect->x &&
           py >= rect->y &&
           px < rect->x + (int32_t)rect->width &&
           py < rect->y + (int32_t)rect->height;
}

static uint32_t prism_window_content_width(const struct prism_window *window) {
    return window->rect.width > PRISM_WINDOW_CONTENT_X + PRISM_WINDOW_CONTENT_RIGHT ?
        window->rect.width - PRISM_WINDOW_CONTENT_X - PRISM_WINDOW_CONTENT_RIGHT : 1u;
}

static uint32_t prism_window_content_height(const struct prism_window *window) {
    return window->rect.height > PRISM_WINDOW_CONTENT_Y + PRISM_WINDOW_CONTENT_BOTTOM ?
        window->rect.height - PRISM_WINDOW_CONTENT_Y - PRISM_WINDOW_CONTENT_BOTTOM : 1u;
}

static int prism_rect_empty(const struct prism_rect *rect) {
    return rect == 0 || rect->width == 0u || rect->height == 0u;
}

static int prism_rect_intersect(const struct prism_rect *a,
                                const struct prism_rect *b,
                                struct prism_rect *out) {
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t a_right;
    int64_t a_bottom;
    int64_t b_right;
    int64_t b_bottom;

    if (prism_rect_empty(a) || prism_rect_empty(b) || out == 0) {
        return 0;
    }
    a_right = (int64_t)a->x + a->width;
    a_bottom = (int64_t)a->y + a->height;
    b_right = (int64_t)b->x + b->width;
    b_bottom = (int64_t)b->y + b->height;
    left = a->x > b->x ? a->x : b->x;
    top = a->y > b->y ? a->y : b->y;
    right = a_right < b_right ? a_right : b_right;
    bottom = a_bottom < b_bottom ? a_bottom : b_bottom;
    if (right <= left || bottom <= top) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    out->x = (int32_t)left;
    out->y = (int32_t)top;
    out->width = (uint32_t)(right - left);
    out->height = (uint32_t)(bottom - top);
    return 1;
}

static int prism_window_intersects_clip(const struct prism_window *window,
                                        const struct prism_rect *clip) {
    struct prism_rect shadow;
    struct prism_rect hit;

    if (clip == 0) {
        return 1;
    }
    shadow = window->rect;
    if ((window->flags & PRISM_WINDOW_DESKTOP) == 0u) {
        shadow.x += 6;
        shadow.y += 8;
    }
    return prism_rect_intersect(&window->rect, clip, &hit) ||
           prism_rect_intersect(&shadow, clip, &hit);
}

static struct prism_rect prism_window_visual_rect(const struct prism_window *window) {
    struct prism_rect rect = window->rect;

    if ((window->flags & PRISM_WINDOW_DESKTOP) == 0u) {
        rect.width += 6u;
        rect.height += 8u;
    }
    return rect;
}

static int prism_region_add(struct prism_region *region,
                            const struct prism_rect *rect) {
    if (region == 0 || prism_rect_empty(rect)) {
        return 1;
    }
    if (region->count >= PRISM_REGION_MAX) {
        return 0;
    }
    region->rects[region->count++] = *rect;
    return 1;
}

static int prism_region_subtract_one(struct prism_region *out,
                                     const struct prism_rect *src,
                                     const struct prism_rect *cut) {
    struct prism_rect hit;
    int64_t src_right;
    int64_t src_bottom;
    int64_t hit_right;
    int64_t hit_bottom;
    struct prism_rect piece;

    if (!prism_rect_intersect(src, cut, &hit)) {
        return prism_region_add(out, src);
    }
    src_right = (int64_t)src->x + src->width;
    src_bottom = (int64_t)src->y + src->height;
    hit_right = (int64_t)hit.x + hit.width;
    hit_bottom = (int64_t)hit.y + hit.height;
    if (hit.y > src->y) {
        piece.x = src->x;
        piece.y = src->y;
        piece.width = src->width;
        piece.height = (uint32_t)(hit.y - src->y);
        if (!prism_region_add(out, &piece)) {
            return 0;
        }
    }
    if (hit_bottom < src_bottom) {
        piece.x = src->x;
        piece.y = (int32_t)hit_bottom;
        piece.width = src->width;
        piece.height = (uint32_t)(src_bottom - hit_bottom);
        if (!prism_region_add(out, &piece)) {
            return 0;
        }
    }
    if (hit.x > src->x) {
        piece.x = src->x;
        piece.y = hit.y;
        piece.width = (uint32_t)(hit.x - src->x);
        piece.height = hit.height;
        if (!prism_region_add(out, &piece)) {
            return 0;
        }
    }
    if (hit_right < src_right) {
        piece.x = (int32_t)hit_right;
        piece.y = hit.y;
        piece.width = (uint32_t)(src_right - hit_right);
        piece.height = hit.height;
        if (!prism_region_add(out, &piece)) {
            return 0;
        }
    }
    return 1;
}

static int prism_region_subtract_rect(struct prism_region *region,
                                      const struct prism_rect *cut) {
    struct prism_region next;

    if (region == 0 || prism_rect_empty(cut)) {
        return 1;
    }
    memset(&next, 0, sizeof(next));
    for (uint32_t i = 0u; i < region->count; i++) {
        if (!prism_region_subtract_one(&next, &region->rects[i], cut)) {
            return 0;
        }
    }
    *region = next;
    return 1;
}

static int prism_window_visible_region(const struct prism_server *server,
                                       uint32_t z_index,
                                       const struct prism_rect *clip,
                                       struct prism_region *region) {
    const struct prism_window *window;
    struct prism_rect visual;
    struct prism_rect base;
    struct prism_rect display;
    int slot;

    if (region == 0 || z_index >= PRISM_WINDOW_MAX) {
        return 0;
    }
    memset(region, 0, sizeof(*region));
    slot = server->z_order[z_index];
    window = &server->windows[slot];
    if (!window->open) {
        return 1;
    }
    visual = prism_window_visual_rect(window);
    if (clip != 0) {
        if (!prism_rect_intersect(&visual, clip, &base)) {
            return 1;
        }
    } else {
        display.x = 0;
        display.y = 0;
        display.width = server->display.width;
        display.height = server->display.height;
        if (!prism_rect_intersect(&visual, &display, &base)) {
            return 1;
        }
    }
    if (!prism_region_add(region, &base)) {
        return 0;
    }
    for (uint32_t i = z_index + 1u; i < PRISM_WINDOW_MAX; i++) {
        int above_slot = server->z_order[i];

        if (server->windows[above_slot].open) {
            struct prism_rect above = prism_window_visual_rect(
                &server->windows[above_slot]);

            if (!prism_region_subtract_rect(region, &above)) {
                return 0;
            }
        }
    }
    return 1;
}

static uint32_t prism_window_damage_width(const struct prism_window *window) {
    uint32_t width = (window->flags & PRISM_WINDOW_DESKTOP) != 0u ?
        window->rect.width : prism_window_content_width(window);

    if (window->surface_pixels != 0 &&
        window->surface_pixels != MAP_FAILED &&
        window->surface_width < width) {
        width = window->surface_width;
    }
    return width;
}

static uint32_t prism_window_damage_height(const struct prism_window *window) {
    uint32_t height = (window->flags & PRISM_WINDOW_DESKTOP) != 0u ?
        window->rect.height : prism_window_content_height(window);

    if (window->surface_pixels != 0 &&
        window->surface_pixels != MAP_FAILED &&
        window->surface_height < height) {
        height = window->surface_height;
    }
    return height;
}

static void prism_window_content_origin(const struct prism_window *window,
                                        int32_t *x,
                                        int32_t *y) {
    *x = window->rect.x;
    *y = window->rect.y;
    if ((window->flags & PRISM_WINDOW_DESKTOP) == 0u) {
        *x += PRISM_WINDOW_CONTENT_X;
        *y += PRISM_WINDOW_CONTENT_Y;
    }
}

static void prism_clip_damage_rect(const struct prism_window *window,
                                   const struct prism_rect *requested,
                                   struct prism_rect *actual) {
    uint32_t bounds_width = prism_window_damage_width(window);
    uint32_t bounds_height = prism_window_damage_height(window);
    int64_t left = 0;
    int64_t top = 0;
    int64_t right = bounds_width;
    int64_t bottom = bounds_height;

    memset(actual, 0, sizeof(*actual));
    if (bounds_width == 0u || bounds_height == 0u) {
        return;
    }
    if (requested != 0) {
        left = requested->x;
        top = requested->y;
        right = (int64_t)requested->x + requested->width;
        bottom = (int64_t)requested->y + requested->height;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (int64_t)bounds_width) {
        right = bounds_width;
    }
    if (bottom > (int64_t)bounds_height) {
        bottom = bounds_height;
    }
    if (right <= left || bottom <= top) {
        return;
    }
    actual->x = (int32_t)left;
    actual->y = (int32_t)top;
    actual->width = (uint32_t)(right - left);
    actual->height = (uint32_t)(bottom - top);
}

static void prism_note_screen_damage(struct prism_server *server,
                                     const struct prism_rect *screen);

static void prism_note_damage(struct prism_server *server,
                              const struct prism_window *window,
                              const struct prism_rect *content_rect) {
    struct prism_rect screen;
    int32_t origin_x;
    int32_t origin_y;

    if (prism_rect_empty(content_rect)) {
        return;
    }
    prism_window_content_origin(window, &origin_x, &origin_y);
    screen.x = origin_x + content_rect->x;
    screen.y = origin_y + content_rect->y;
    screen.width = content_rect->width;
    screen.height = content_rect->height;
    prism_note_screen_damage(server, &screen);
}

static void prism_note_screen_damage(struct prism_server *server,
                                     const struct prism_rect *screen) {
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (prism_rect_empty(screen)) {
        return;
    }
    if (!server->has_damage) {
        server->damage = *screen;
        server->has_damage = 1u;
        return;
    }
    left = server->damage.x < screen->x ? server->damage.x : screen->x;
    top = server->damage.y < screen->y ? server->damage.y : screen->y;
    right = (int64_t)server->damage.x + server->damage.width;
    if ((int64_t)screen->x + screen->width > right) {
        right = (int64_t)screen->x + screen->width;
    }
    bottom = (int64_t)server->damage.y + server->damage.height;
    if ((int64_t)screen->y + screen->height > bottom) {
        bottom = (int64_t)screen->y + screen->height;
    }
    server->damage.x = (int32_t)left;
    server->damage.y = (int32_t)top;
    server->damage.width = (uint32_t)(right - left);
    server->damage.height = (uint32_t)(bottom - top);
}

static void prism_note_display_damage(struct prism_server *server) {
    struct prism_rect rect;

    rect.x = 0;
    rect.y = 0;
    rect.width = server->display.width;
    rect.height = server->display.height;
    prism_note_screen_damage(server, &rect);
}

static void prism_note_window_rect_damage(struct prism_server *server,
                                          const struct prism_rect *old_rect,
                                          const struct prism_rect *new_rect) {
    prism_note_screen_damage(server, old_rect);
    prism_note_screen_damage(server, new_rect);
}

static void prism_note_move_damage(struct prism_server *server,
                                   const struct prism_rect *old_rect,
                                   const struct prism_rect *new_rect) {
    prism_note_window_rect_damage(server, old_rect, new_rect);
}

static void prism_note_resize_damage(struct prism_server *server,
                                     const struct prism_rect *old_rect,
                                     const struct prism_rect *new_rect) {
    prism_note_window_rect_damage(server, old_rect, new_rect);
}

static void prism_note_focus_damage(struct prism_server *server,
                                    const struct prism_rect *old_rect,
                                    const struct prism_rect *new_rect) {
    prism_note_window_rect_damage(server, old_rect, new_rect);
}

static void prism_pointer_rect_at(int32_t x,
                                  int32_t y,
                                  struct prism_rect *rect) {
    rect->x = x - 12;
    rect->y = y - 12;
    rect->width = 25u;
    rect->height = 25u;
}

static void prism_note_pointer_damage(struct prism_server *server,
                                      int32_t old_x,
                                      int32_t old_y,
                                      int32_t new_x,
                                      int32_t new_y) {
    struct prism_rect rect;

    prism_pointer_rect_at(old_x, old_y, &rect);
    prism_note_screen_damage(server, &rect);
    prism_pointer_rect_at(new_x, new_y, &rect);
    prism_note_screen_damage(server, &rect);
}

static void prism_clear_damage(struct prism_server *server) {
    server->has_damage = 0u;
    memset(&server->damage, 0, sizeof(server->damage));
}

static void prism_display_fill_rect_clipped(int32_t x,
                                            int32_t y,
                                            uint32_t width,
                                            uint32_t height,
                                            uint32_t rgb,
                                            const struct prism_rect *clip) {
    struct prism_rect rect;
    struct prism_rect draw;

    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    if (clip != 0) {
        if (!prism_rect_intersect(&rect, clip, &draw)) {
            return;
        }
    } else {
        draw = rect;
    }
    prism_kernel_display_fill_rect(draw.x,
                                   draw.y,
                                   draw.width,
                                   draw.height,
                                   rgb);
}

static void prism_display_draw_rect_clipped(int32_t x,
                                            int32_t y,
                                            uint32_t width,
                                            uint32_t height,
                                            uint32_t rgb,
                                            const struct prism_rect *clip) {
    if (width == 0u || height == 0u) {
        return;
    }
    prism_display_fill_rect_clipped(x, y, width, 1u, rgb, clip);
    if (height > 1u) {
        prism_display_fill_rect_clipped(x,
                                        y + (int32_t)height - 1,
                                        width,
                                        1u,
                                        rgb,
                                        clip);
    }
    if (height > 2u) {
        prism_display_fill_rect_clipped(x,
                                        y + 1,
                                        1u,
                                        height - 2u,
                                        rgb,
                                        clip);
        if (width > 1u) {
            prism_display_fill_rect_clipped(x + (int32_t)width - 1,
                                            y + 1,
                                            1u,
                                            height - 2u,
                                            rgb,
                                            clip);
        }
    }
}

static void prism_display_draw_line_clipped(int32_t x0,
                                            int32_t y0,
                                            int32_t x1,
                                            int32_t y1,
                                            uint32_t rgb,
                                            const struct prism_rect *clip) {
    int32_t dx;
    int32_t dy;
    int32_t sx;
    int32_t sy;
    int32_t err;

    if (clip == 0) {
        prism_kernel_display_draw_line(x0, y0, x1, y1, rgb);
        return;
    }
    dx = prism_abs_i32(x1 - x0);
    dy = -prism_abs_i32(y1 - y0);
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        if (prism_point_in_rect(x0, y0, clip)) {
            prism_kernel_display_fill_rect(x0, y0, 1u, 1u, rgb);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (2 * err >= dy) {
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void prism_display_blit_xrgb8888_clipped(const uint32_t *pixels,
                                                uint32_t pitch,
                                                int32_t dst_x,
                                                int32_t dst_y,
                                                uint32_t width,
                                                uint32_t height,
                                                const struct prism_rect *clip) {
    struct prism_rect rect;
    struct prism_rect draw;
    uint32_t stride;
    uint32_t src_x;
    uint32_t src_y;
    const uint32_t *src;

    if (pixels == 0 || pixels == MAP_FAILED || pitch == 0u) {
        return;
    }
    rect.x = dst_x;
    rect.y = dst_y;
    rect.width = width;
    rect.height = height;
    if (clip != 0) {
        if (!prism_rect_intersect(&rect, clip, &draw)) {
            return;
        }
    } else {
        draw = rect;
    }
    stride = pitch / sizeof(uint32_t);
    src_x = (uint32_t)(draw.x - dst_x);
    src_y = (uint32_t)(draw.y - dst_y);
    src = pixels + src_y * stride + src_x;
    prism_kernel_display_blit_xrgb8888(src,
                                       pitch,
                                       draw.x,
                                       draw.y,
                                       draw.width,
                                       draw.height);
}

static void prism_send_reply(const struct prism_message *message,
                             int32_t status,
                             uint32_t window_id,
                             const struct prism_rect *rect) {
    struct prism_reply reply;
    mqd_t reply_queue;

    if (message == 0 || message->reply_queue[0] == '\0') {
        return;
    }
    reply_queue = prism_kernel_ipc_open(message->reply_queue, 0);
    if (reply_queue < 0) {
        return;
    }
    memset(&reply, 0, sizeof(reply));
    reply.type = PRISM_MSG_REPLY;
    reply.status = status;
    reply.window_id = window_id;
    if (rect != 0) {
        reply.rect = *rect;
    }
    (void)prism_kernel_ipc_send(reply_queue, &reply, sizeof(reply), IPC_NONBLOCK);
}

static int prism_find_free_window(const struct prism_server *server) {
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        if (!server->windows[i].open) {
            return (int)i;
        }
    }
    return -1;
}

static int prism_find_window_by_id(const struct prism_server *server, uint32_t id) {
    if (id == 0u) {
        return -1;
    }
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        if (server->windows[i].open && server->windows[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static int prism_find_desktop_window(const struct prism_server *server) {
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        if (server->windows[i].open &&
            (server->windows[i].flags & PRISM_WINDOW_DESKTOP) != 0u) {
            return (int)i;
        }
    }
    return -1;
}

static void prism_send_window_event(const struct prism_window *window,
                                    const struct prism_event *event) {
    if (window == 0 ||
        event == 0 ||
        window->event_queue < 0 ||
        window->event_queue_name[0] == '\0') {
        return;
    }
    (void)prism_kernel_ipc_send(window->event_queue,
                                event,
                                sizeof(*event),
                                IPC_NONBLOCK);
}

static void prism_send_focus_event(struct prism_window *window, uint8_t focused) {
    struct prism_event event;

    if (window == 0 || !window->open) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = PRISM_EVENT_FOCUS;
    event.window_id = window->id;
    event.tick = ticks();
    event.pressed = focused;
    event.released = !focused;
    prism_send_window_event(window, &event);
}

static void prism_send_close_request(struct prism_window *window) {
    struct prism_event event;

    if (window == 0 || !window->open) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = PRISM_EVENT_CLOSE_REQUEST;
    event.window_id = window->id;
    event.tick = ticks();
    prism_send_window_event(window, &event);
}

static void prism_send_configure_event(struct prism_window *window,
                                       uint32_t type) {
    struct prism_event event;

    if (window == 0 || !window->open) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.window_id = window->id;
    event.tick = ticks();
    event.rect = window->rect;
    event.content_width = prism_window_content_width(window);
    event.content_height = prism_window_content_height(window);
    prism_send_window_event(window, &event);
}

static void prism_focus(struct prism_server *server, int slot) {
    uint8_t next_order[PRISM_WINDOW_MAX];
    uint32_t out = 0u;
    int old_focused;
    struct prism_rect old_visual;
    struct prism_rect new_visual;
    uint8_t has_old_visual = 0u;

    if (slot < 0 || slot >= (int)PRISM_WINDOW_MAX || !server->windows[slot].open) {
        return;
    }
    if ((server->windows[slot].flags & PRISM_WINDOW_DESKTOP) != 0u) {
        return;
    }
    old_focused = server->focused;
    if (old_focused == slot) {
        return;
    }
    if (old_focused >= 0 &&
        old_focused < (int)PRISM_WINDOW_MAX &&
        server->windows[old_focused].open) {
        old_visual = prism_window_visual_rect(&server->windows[old_focused]);
        has_old_visual = 1u;
    }
    new_visual = prism_window_visual_rect(&server->windows[slot]);
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        if (server->z_order[i] != (uint8_t)slot) {
            next_order[out++] = server->z_order[i];
        }
    }
    next_order[out++] = (uint8_t)slot;
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        server->z_order[i] = next_order[i];
    }
    if (old_focused >= 0 && old_focused < (int)PRISM_WINDOW_MAX) {
        prism_send_focus_event(&server->windows[old_focused], 0u);
    }
    server->focused = slot;
    prism_send_focus_event(&server->windows[slot], 1u);
    if (has_old_visual) {
        prism_note_focus_damage(server, &old_visual, &new_visual);
    } else {
        prism_note_screen_damage(server, &new_visual);
    }
}

static void prism_send_to_back(struct prism_server *server, int slot) {
    uint8_t next_order[PRISM_WINDOW_MAX];
    uint32_t out = 0u;

    if (slot < 0 || slot >= (int)PRISM_WINDOW_MAX || !server->windows[slot].open) {
        return;
    }
    next_order[out++] = (uint8_t)slot;
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        if (server->z_order[i] != (uint8_t)slot) {
            next_order[out++] = server->z_order[i];
        }
    }
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        server->z_order[i] = next_order[i];
    }
}

static int prism_hit_window(const struct prism_server *server, int32_t x, int32_t y) {
    for (int i = (int)PRISM_WINDOW_MAX - 1; i >= 0; i--) {
        int slot = server->z_order[i];

        if (server->windows[slot].open &&
            (server->windows[slot].flags & PRISM_WINDOW_DESKTOP) == 0u &&
            prism_point_in_rect(x, y, &server->windows[slot].rect)) {
            return slot;
        }
    }
    return -1;
}

static int prism_hit_titlebar(const struct prism_window *window,
                              int32_t x,
                              int32_t y) {
    struct prism_rect titlebar;

    titlebar = window->rect;
    titlebar.height = PRISM_TITLEBAR_HEIGHT;
    return prism_point_in_rect(x, y, &titlebar);
}

static int prism_hit_content(const struct prism_window *window,
                             int32_t x,
                             int32_t y) {
    struct prism_rect content;

    content.x = window->rect.x + PRISM_WINDOW_CONTENT_X;
    content.y = window->rect.y + PRISM_WINDOW_CONTENT_Y;
    content.width = prism_window_content_width(window);
    content.height = prism_window_content_height(window);
    return prism_point_in_rect(x, y, &content);
}

static int prism_hit_resize_grip(const struct prism_window *window,
                                 int32_t x,
                                 int32_t y) {
    struct prism_rect grip;

    grip.x = window->rect.x + (int32_t)window->rect.width -
        PRISM_RESIZE_GRIP_SIZE;
    grip.y = window->rect.y + (int32_t)window->rect.height -
        PRISM_RESIZE_GRIP_SIZE;
    grip.width = PRISM_RESIZE_GRIP_SIZE;
    grip.height = PRISM_RESIZE_GRIP_SIZE;
    return prism_point_in_rect(x, y, &grip);
}

static int prism_hit_close(const struct prism_window *window,
                           int32_t x,
                           int32_t y) {
    struct prism_rect close_rect;

    close_rect.x = window->rect.x + 7;
    close_rect.y = window->rect.y + (PRISM_TITLEBAR_HEIGHT - PRISM_CLOSE_SIZE) / 2;
    close_rect.width = PRISM_CLOSE_SIZE;
    close_rect.height = PRISM_CLOSE_SIZE;
    return prism_point_in_rect(x, y, &close_rect);
}

static void prism_destroy_window(struct prism_server *server, int slot) {
    struct prism_window *window;

    if (slot < 0 || slot >= (int)PRISM_WINDOW_MAX) {
        return;
    }
    window = &server->windows[slot];
    if (window->surface_pixels != 0 && window->surface_pixels != MAP_FAILED) {
        (void)prism_kernel_shm_unmap(window->surface_pixels,
                                     window->surface_size);
    }
    memset(window, 0, sizeof(*window));
    if (server->focused == slot) {
        server->focused = -1;
    }
    if (server->dragging == slot) {
        server->dragging = -1;
    }
    if (server->resizing == slot) {
        server->resizing = -1;
    }
    if (server->pointer_capture == slot) {
        server->pointer_capture = -1;
    }
}

static void prism_clamp_rect_to_display(const struct prism_server *server,
                                        struct prism_rect *rect) {
    int32_t max_x;
    int32_t max_y;

    if (rect->width < PRISM_WINDOW_MIN_WIDTH) {
        rect->width = PRISM_WINDOW_MIN_WIDTH;
    }
    if (rect->height < PRISM_WINDOW_MIN_HEIGHT) {
        rect->height = PRISM_WINDOW_MIN_HEIGHT;
    }
    if (rect->width > server->display.width) {
        rect->width = server->display.width;
    }
    if (rect->height > server->display.height) {
        rect->height = server->display.height;
    }
    max_x = (int32_t)server->display.width - (int32_t)rect->width;
    max_y = (int32_t)server->display.height - (int32_t)rect->height;
    rect->x = prism_clamp_i32(rect->x, 0, max_x > 0 ? max_x : 0);
    rect->y = prism_clamp_i32(rect->y, 0, max_y > 0 ? max_y : 0);
}

static uint32_t prism_configure_window(struct prism_server *server,
                                       int slot,
                                       const struct prism_rect *requested,
                                       uint32_t configure_flags,
                                       struct prism_rect *actual) {
    struct prism_window *window;
    struct prism_rect next;
    uint32_t moved = 0u;
    uint32_t resized = 0u;

    if (slot < 0 || slot >= (int)PRISM_WINDOW_MAX || requested == 0) {
        return 0u;
    }
    window = &server->windows[slot];
    if (!window->open) {
        return 0u;
    }
    if ((window->flags & PRISM_WINDOW_DESKTOP) != 0u) {
        if (actual != 0) {
            *actual = window->rect;
        }
        return 0u;
    }
    next = window->rect;
    if ((configure_flags & PRISM_CONFIGURE_X) != 0u) {
        next.x = requested->x;
    }
    if ((configure_flags & PRISM_CONFIGURE_Y) != 0u) {
        next.y = requested->y;
    }
    if ((configure_flags & PRISM_CONFIGURE_WIDTH) != 0u) {
        next.width = requested->width;
    }
    if ((configure_flags & PRISM_CONFIGURE_HEIGHT) != 0u) {
        next.height = requested->height;
    }
    prism_clamp_rect_to_display(server, &next);
    if (next.x != window->rect.x || next.y != window->rect.y) {
        moved = 1u;
    }
    if (next.width != window->rect.width ||
        next.height != window->rect.height) {
        resized = 1u;
    }
    window->rect = next;
    if (actual != 0) {
        *actual = window->rect;
    }
    if (moved) {
        prism_send_configure_event(window, PRISM_EVENT_MOVE);
    }
    if (resized) {
        prism_send_configure_event(window, PRISM_EVENT_RESIZE);
    }
    return moved || resized;
}

static uint8_t prism_track_key_repeat(struct prism_server *server,
                                      uint32_t keycode,
                                      uint8_t pressed,
                                      uint8_t released) {
    uint8_t repeat = 0u;

    if (keycode >= PRISM_KEY_MAX) {
        return 0u;
    }
    if (pressed) {
        repeat = server->key_down[keycode] ? 1u : 0u;
        server->key_down[keycode] = 1u;
    }
    if (released) {
        server->key_down[keycode] = 0u;
    }
    return repeat;
}

static void prism_update_modifiers_from_key(struct prism_server *server,
                                            const prism_kernel_input_event *event) {
    server->shift = event->shift;
    server->ctrl = event->ctrl;
    server->alt = event->alt;
    server->caps_lock = event->caps_lock;
    server->num_lock = event->num_lock;
    server->scroll_lock = event->scroll_lock;
}

static void prism_copy_modifiers_to_event(const struct prism_server *server,
                                          struct prism_event *event) {
    event->shift = server->shift;
    event->ctrl = server->ctrl;
    event->alt = server->alt;
    event->caps_lock = server->caps_lock;
    event->num_lock = server->num_lock;
    event->scroll_lock = server->scroll_lock;
}

static uint32_t prism_window_color(uint32_t id) {
    static const uint32_t colors[] = {
        0x2563ebu,
        0x16a34au,
        0xdb2777u,
        0xf59e0bu,
        0x0891b2u,
        0x7c3aedu
    };

    return colors[id % (sizeof(colors) / sizeof(colors[0]))];
}

static uint32_t prism_create_window(struct prism_server *server,
                                    uint32_t owner_pid,
                                    const struct prism_rect *requested,
                                    const char *title,
                                    uint32_t flags,
                                    const char *event_queue_name) {
    struct prism_window *window;
    int slot;
    mqd_t event_queue;
    uint32_t id;

    if ((flags & PRISM_WINDOW_DESKTOP) != 0u) {
        int existing = prism_find_desktop_window(server);

        if (existing >= 0) {
            prism_destroy_window(server, existing);
        }
    }
    slot = prism_find_free_window(server);
    if (slot < 0) {
        return 0u;
    }
    if (event_queue_name == 0 || event_queue_name[0] == '\0') {
        return 0u;
    }
    event_queue = prism_kernel_ipc_open(event_queue_name, 0);
    if (event_queue < 0) {
        return 0u;
    }
    id = server->next_window_id++;
    if (id == 0u) {
        id = server->next_window_id++;
    }
    window = &server->windows[slot];
    memset(window, 0, sizeof(*window));
    window->id = id;
    window->owner_pid = owner_pid;
    window->flags = flags & PRISM_WINDOW_DESKTOP;
    window->event_queue = event_queue;
    if ((window->flags & PRISM_WINDOW_DESKTOP) != 0u) {
        window->rect.x = 0;
        window->rect.y = 0;
        window->rect.width = server->display.width;
        window->rect.height = server->display.height;
    } else {
        window->rect = *requested;
        prism_clamp_rect_to_display(server, &window->rect);
    }
    window->color = prism_window_color(id);
    window->open = 1u;
    prism_copy_text(window->title, sizeof(window->title), title);
    prism_copy_text(window->event_queue_name,
                    sizeof(window->event_queue_name),
                    event_queue_name);
    if ((window->flags & PRISM_WINDOW_DESKTOP) != 0u) {
        prism_send_to_back(server, slot);
    } else {
        prism_focus(server, slot);
    }
    return id;
}

static int prism_attach_surface(struct prism_server *server,
                                const struct prism_message *message) {
    struct prism_window *window;
    uint32_t size;
    int handle;
    uint32_t *pixels;
    int slot = prism_find_window_by_id(server, message->window_id);

    if (slot < 0) {
        return -1;
    }
    window = &server->windows[slot];
    if (window->owner_pid != message->client_pid ||
        message->surface_name[0] == '\0' ||
        message->surface_width == 0u ||
        message->surface_height == 0u ||
        message->surface_width > 0xffffffffu / sizeof(uint32_t) ||
        message->surface_pitch < message->surface_width * sizeof(uint32_t) ||
        message->surface_format != PRISM_KERNEL_SURFACE_XRGB8888) {
        return -1;
    }
    if (message->surface_height > 0xffffffffu / message->surface_pitch) {
        return -1;
    }
    size = message->surface_pitch * message->surface_height;
    handle = prism_kernel_shm_open(message->surface_name, size, 0);
    if (handle < 0) {
        return -1;
    }
    pixels = (uint32_t *)prism_kernel_shm_map(handle, size);
    if (pixels == MAP_FAILED) {
        return -1;
    }
    if (window->surface_pixels != 0 && window->surface_pixels != MAP_FAILED) {
        (void)prism_kernel_shm_unmap(window->surface_pixels,
                                     window->surface_size);
    }
    window->surface_handle = handle;
    window->surface_pixels = pixels;
    window->surface_width = message->surface_width;
    window->surface_height = message->surface_height;
    window->surface_pitch = message->surface_pitch;
    window->surface_format = message->surface_format;
    window->surface_size = size;
    prism_copy_text(window->surface_name,
                    sizeof(window->surface_name),
                    message->surface_name);
    return 0;
}

static int prism_present_window(struct prism_server *server,
                                const struct prism_message *message,
                                struct prism_rect *actual) {
    struct prism_window *window;
    const struct prism_rect *requested = 0;
    int slot = prism_find_window_by_id(server, message->window_id);

    if (slot < 0) {
        return -1;
    }
    window = &server->windows[slot];
    if (window->owner_pid != message->client_pid) {
        return -1;
    }
    if ((message->flags & PRISM_PRESENT_RECT) != 0u) {
        requested = &message->rect;
    }
    prism_clip_damage_rect(window, requested, actual);
    prism_note_damage(server, window, actual);
    return 0;
}

static int prism_handle_message(struct prism_server *server,
                                const struct prism_message *message) {
    uint32_t id;
    int slot;
    struct prism_rect actual;

    if (message == 0) {
        return 0;
    }
    memset(&actual, 0, sizeof(actual));
    switch (message->type) {
        case PRISM_MSG_HELLO:
            prism_send_reply(message, 0, 0u, 0);
            return 0;
        case PRISM_MSG_CREATE_WINDOW:
            id = prism_create_window(server,
                                     message->client_pid,
                                     &message->rect,
                                     message->title,
                                     message->flags,
                                     message->event_queue);
            slot = prism_find_window_by_id(server, id);
            prism_send_reply(message,
                             id != 0u ? 0 : -1,
                             id,
                             slot >= 0 ? &server->windows[slot].rect : 0);
            if (id != 0u) {
                prism_note_display_damage(server);
            }
            return id != 0u;
        case PRISM_MSG_DESTROY_WINDOW:
            slot = prism_find_window_by_id(server, message->window_id);
            if (slot >= 0) {
                prism_destroy_window(server, slot);
                prism_note_display_damage(server);
            }
            prism_send_reply(message, slot >= 0 ? 0 : -1, message->window_id, 0);
            return slot >= 0;
        case PRISM_MSG_PRESENT_WINDOW:
            slot = prism_present_window(server, message, &actual);
            prism_send_reply(message, slot, message->window_id, &actual);
            return slot == 0 && !prism_rect_empty(&actual);
        case PRISM_MSG_CONFIGURE_WINDOW:
            slot = prism_find_window_by_id(server, message->window_id);
            if (slot >= 0 &&
                server->windows[slot].owner_pid == message->client_pid) {
                struct prism_rect old_rect = server->windows[slot].rect;
                struct prism_rect old_visual =
                    prism_window_visual_rect(&server->windows[slot]);
                uint32_t changed = prism_configure_window(server,
                                                          slot,
                                                          &message->rect,
                                                          message->flags,
                                                          &actual);

                prism_send_reply(message, 0, message->window_id, &actual);
                if (changed != 0u) {
                    uint32_t moved = old_rect.x != actual.x ||
                        old_rect.y != actual.y;
                    uint32_t resized = old_rect.width != actual.width ||
                        old_rect.height != actual.height;

                    if (moved || resized) {
                        struct prism_rect new_visual =
                            prism_window_visual_rect(&server->windows[slot]);

                        if (resized) {
                            prism_note_resize_damage(server,
                                                     &old_visual,
                                                     &new_visual);
                        } else {
                            prism_note_move_damage(server,
                                                   &old_visual,
                                                   &new_visual);
                        }
                    } else {
                        prism_note_display_damage(server);
                    }
                }
                return changed != 0u;
            }
            prism_send_reply(message, -1, message->window_id, 0);
            return 0;
        case PRISM_MSG_SHUTDOWN:
            server->running = 0u;
            prism_send_reply(message, 0, 0u, 0);
            return 0;
        case PRISM_MSG_ATTACH_SURFACE:
            slot = prism_attach_surface(server, message);
            prism_send_reply(message, slot, message->window_id, 0);
            if (slot == 0) {
                prism_note_display_damage(server);
            }
            return slot == 0;
        default:
            prism_send_reply(message, -1, 0u, 0);
            return 0;
    }
}

static int prism_drain_messages(struct prism_server *server) {
    struct prism_message message;
    int dirty = 0;

    for (;;) {
        int bytes = prism_kernel_ipc_receive(server->queue,
                                             &message,
                                             sizeof(message),
                                             IPC_NONBLOCK);
        if (bytes <= 0) {
            return dirty;
        }
        if ((uint32_t)bytes == sizeof(message)) {
            dirty |= prism_handle_message(server, &message);
        }
    }
}

static int prism_handle_input(struct prism_server *server,
                              const prism_kernel_input_event *event) {
    uint32_t old_buttons;
    uint32_t button_pressed;
    uint32_t button_released;
    int left_down;
    int left_was_down;
    int was_dragging;
    int was_resizing;
    int32_t old_pointer_x;
    int32_t old_pointer_y;

    if (event->type == SYS_GUI_EVENT_KEY) {
        prism_update_modifiers_from_key(server, event);
        if (server->focused >= 0 && server->focused < (int)PRISM_WINDOW_MAX) {
            struct prism_window *window = &server->windows[server->focused];
            struct prism_event out;

            if (window->open &&
                (window->flags & PRISM_WINDOW_DESKTOP) == 0u) {
                memset(&out, 0, sizeof(out));
                out.type = PRISM_EVENT_KEY;
                out.window_id = window->id;
                out.tick = event->tick;
                out.keycode = event->keycode;
                out.ascii = event->ascii;
                out.pressed = event->pressed;
                out.released = event->released;
                out.repeat = prism_track_key_repeat(server,
                                                    event->keycode,
                                                    event->pressed,
                                                    event->released);
                out.extended = event->extended;
                prism_copy_modifiers_to_event(server, &out);
                prism_send_window_event(window, &out);
            }
        }
        if (server->debug_exit &&
            event->pressed &&
            event->keycode == SYS_KEY_ESC) {
            server->running = 0u;
        }
        return 1;
    }
    if (event->type != SYS_GUI_EVENT_MOUSE) {
        return 0;
    }

    old_buttons = server->buttons;
    was_dragging = server->dragging;
    was_resizing = server->resizing;
    old_pointer_x = server->pointer_x;
    old_pointer_y = server->pointer_y;
    server->pointer_x = prism_clamp_i32(server->pointer_x + event->dx,
                                        0,
                                        (int32_t)server->display.width - 1);
    server->pointer_y = prism_clamp_i32(server->pointer_y + event->dy,
                                        0,
                                        (int32_t)server->display.height - 1);
    server->buttons = event->buttons;
    button_pressed = server->buttons & ~old_buttons;
    button_released = old_buttons & ~server->buttons;
    left_down = (server->buttons & SYS_GUI_MOUSE_LEFT) != 0u;
    left_was_down = (old_buttons & SYS_GUI_MOUSE_LEFT) != 0u;
    if (event->dx != 0 ||
        event->dy != 0 ||
        server->buttons != old_buttons) {
        prism_note_pointer_damage(server,
                                  old_pointer_x,
                                  old_pointer_y,
                                  server->pointer_x,
                                  server->pointer_y);
    }

    if (left_down && !left_was_down) {
        int slot = prism_hit_window(server, server->pointer_x, server->pointer_y);

        if (slot >= 0) {
            struct prism_window *window = &server->windows[slot];

            prism_focus(server, slot);
            if (prism_hit_close(window, server->pointer_x, server->pointer_y)) {
                prism_send_close_request(window);
                return 1;
            }
            if (prism_hit_titlebar(window, server->pointer_x, server->pointer_y)) {
                server->dragging = slot;
                server->drag_dx = server->pointer_x - window->rect.x;
                server->drag_dy = server->pointer_y - window->rect.y;
                return 1;
            }
            if (prism_hit_resize_grip(window, server->pointer_x, server->pointer_y)) {
                server->resizing = slot;
                return 1;
            }
            if (prism_hit_content(window, server->pointer_x, server->pointer_y)) {
                server->pointer_capture = slot;
            }
        }
    } else if (!left_down && left_was_down) {
        server->dragging = -1;
        server->resizing = -1;
    }

    if (left_down && server->dragging >= 0) {
        struct prism_window *window = &server->windows[server->dragging];
        struct prism_rect old_visual = prism_window_visual_rect(window);
        struct prism_rect next = window->rect;
        uint32_t changed;

        next.x = server->pointer_x - server->drag_dx;
        next.y = server->pointer_y - server->drag_dy;
        changed = prism_configure_window(server,
                                         server->dragging,
                                         &next,
                                         PRISM_CONFIGURE_X | PRISM_CONFIGURE_Y,
                                         0);
        if (changed != 0u) {
            struct prism_rect new_visual = prism_window_visual_rect(window);

            prism_note_move_damage(server, &old_visual, &new_visual);
        }
        return 1;
    }
    if (left_down && server->resizing >= 0) {
        struct prism_window *window = &server->windows[server->resizing];
        struct prism_rect old_visual = prism_window_visual_rect(window);
        struct prism_rect next = window->rect;
        int32_t width = server->pointer_x - window->rect.x + 1;
        int32_t height = server->pointer_y - window->rect.y + 1;
        uint32_t changed;

        next.width = width > 0 ? (uint32_t)width : PRISM_WINDOW_MIN_WIDTH;
        next.height = height > 0 ? (uint32_t)height : PRISM_WINDOW_MIN_HEIGHT;
        changed = prism_configure_window(server,
                                         server->resizing,
                                         &next,
                                         PRISM_CONFIGURE_WIDTH |
                                         PRISM_CONFIGURE_HEIGHT,
                                         0);
        if (changed != 0u) {
            struct prism_rect new_visual = prism_window_visual_rect(window);

            prism_note_resize_damage(server, &old_visual, &new_visual);
        }
        return 1;
    }
    if (was_dragging >= 0 && !left_down) {
        return 1;
    }
    if (was_resizing >= 0 && !left_down) {
        return 1;
    }
    {
        int target = server->pointer_capture >= 0 ?
            server->pointer_capture : server->focused;
        struct prism_window *window;
        struct prism_event out;

        if (target >= 0 && target < (int)PRISM_WINDOW_MAX) {
            window = &server->windows[target];
        } else {
            window = 0;
        }
        if (window != 0 &&
            window->open &&
            (window->flags & PRISM_WINDOW_DESKTOP) == 0u) {
            memset(&out, 0, sizeof(out));
            out.type = PRISM_EVENT_MOUSE;
            out.window_id = window->id;
            out.tick = event->tick;
            out.x = server->pointer_x - window->rect.x - PRISM_WINDOW_CONTENT_X;
            out.y = server->pointer_y - window->rect.y - PRISM_WINDOW_CONTENT_Y;
            out.dx = event->dx;
            out.dy = event->dy;
            out.buttons = server->buttons;
            out.button_pressed = button_pressed;
            out.button_released = button_released;
            out.pressed = button_pressed != 0u;
            out.released = button_released != 0u;
            prism_copy_modifiers_to_event(server, &out);
            prism_send_window_event(window, &out);
        }
    }
    if (server->pointer_capture >= 0 && server->buttons == 0u) {
        server->pointer_capture = -1;
    }
    return event->dx != 0 ||
           event->dy != 0 ||
           server->buttons != old_buttons ||
           left_down != left_was_down ||
           server->dragging >= 0;
}

static int prism_drain_input(struct prism_server *server) {
    prism_kernel_input_event event;
    int dirty = 0;

    while (prism_kernel_input_poll(&server->input_cursor, &event) ==
           PRISM_KERNEL_EVENT_READY) {
        dirty |= prism_handle_input(server, &event);
    }
    return dirty;
}

static void prism_draw_window(const struct prism_window *window,
                              int focused,
                              const struct prism_rect *clip) {
    uint32_t frame = focused ? 0xf8fafcu : 0x64748bu;
    uint32_t title = focused ? window->color : 0x334155u;
    int32_t x = window->rect.x;
    int32_t y = window->rect.y;
    uint32_t w = window->rect.width;
    uint32_t h = window->rect.height;

    prism_display_fill_rect_clipped(x + 6, y + 8, w, h, 0x020617u, clip);
    prism_display_fill_rect_clipped(x, y, w, h, 0xe5e7ebu, clip);
    prism_display_fill_rect_clipped(x, y, w, PRISM_TITLEBAR_HEIGHT, title, clip);
    prism_display_draw_rect_clipped(x, y, w, h, frame, clip);
    prism_display_fill_rect_clipped(x + 7,
                                    y + (PRISM_TITLEBAR_HEIGHT - PRISM_CLOSE_SIZE) / 2,
                                    PRISM_CLOSE_SIZE,
                                    PRISM_CLOSE_SIZE,
                                    0xef4444u,
                                    clip);
    prism_display_fill_rect_clipped(x + 28,
                                    y + 7,
                                    w > 72u ? w - 58u : 16u,
                                    8u,
                                    0xdbeafeu,
                                    clip);
    if (window->surface_pixels != 0 && window->surface_pixels != MAP_FAILED) {
        uint32_t content_width = prism_window_content_width(window);
        uint32_t content_height = prism_window_content_height(window);
        uint32_t blit_width = window->surface_width < content_width ?
            window->surface_width : content_width;
        uint32_t blit_height = window->surface_height < content_height ?
            window->surface_height : content_height;

        prism_display_blit_xrgb8888_clipped(window->surface_pixels,
                                            window->surface_pitch,
                                            x + PRISM_WINDOW_CONTENT_X,
                                            y + PRISM_WINDOW_CONTENT_Y,
                                            blit_width,
                                            blit_height,
                                            clip);
    } else {
        prism_display_fill_rect_clipped(x + 16,
                                        y + 38,
                                        w - 32u,
                                        14u,
                                        0xcbd5e1u,
                                        clip);
        prism_display_fill_rect_clipped(x + 16,
                                        y + 64,
                                        w > 52u ? w - 32u : 20u,
                                        h > 92u ? h - 84u : 8u,
                                        0xffffffu,
                                        clip);
    }
    prism_display_draw_line_clipped(x + (int32_t)w - 12,
                                    y + (int32_t)h - 4,
                                    x + (int32_t)w - 4,
                                    y + (int32_t)h - 12,
                                    0x94a3b8u,
                                    clip);
    prism_display_draw_line_clipped(x + (int32_t)w - 8,
                                    y + (int32_t)h - 4,
                                    x + (int32_t)w - 4,
                                    y + (int32_t)h - 8,
                                    0x64748bu,
                                    clip);
}

static void prism_draw_desktop(const struct prism_window *window,
                               const struct prism_rect *clip) {
    uint32_t blit_width;
    uint32_t blit_height;

    if (window->surface_pixels == 0 || window->surface_pixels == MAP_FAILED) {
        return;
    }
    blit_width = window->surface_width < window->rect.width ?
        window->surface_width : window->rect.width;
    blit_height = window->surface_height < window->rect.height ?
        window->surface_height : window->rect.height;
    prism_display_blit_xrgb8888_clipped(window->surface_pixels,
                                        window->surface_pitch,
                                        window->rect.x,
                                        window->rect.y,
                                        blit_width,
                                        blit_height,
                                        clip);
}

static void prism_draw_pointer(const struct prism_server *server,
                               const struct prism_rect *clip) {
    uint32_t fill = (server->buttons & SYS_GUI_MOUSE_LEFT) ? 0xf97316u : 0xe0f2feu;
    uint32_t outline = (server->buttons & SYS_GUI_MOUSE_LEFT) ? 0xffedd5u : 0x0284c7u;
    struct prism_rect bounds;
    struct prism_rect hit;

    bounds.x = server->pointer_x - 12;
    bounds.y = server->pointer_y - 12;
    bounds.width = 25u;
    bounds.height = 25u;
    if (clip != 0 && !prism_rect_intersect(&bounds, clip, &hit)) {
        return;
    }

    prism_kernel_display_fill_circle(server->pointer_x, server->pointer_y, 5u, fill);
    prism_kernel_display_draw_circle(server->pointer_x, server->pointer_y, 8u, outline);
    prism_display_draw_line_clipped(server->pointer_x - 12,
                                    server->pointer_y,
                                    server->pointer_x + 12,
                                    server->pointer_y,
                                    outline,
                                    clip);
    prism_display_draw_line_clipped(server->pointer_x,
                                    server->pointer_y - 12,
                                    server->pointer_x,
                                    server->pointer_y + 12,
                                    outline,
                                    clip);
}

static void prism_draw(struct prism_server *server) {
    const struct prism_rect *clip = server->has_damage ? &server->damage : 0;
    int occlusion_ok = 1;

    if (clip != 0) {
        prism_display_fill_rect_clipped(clip->x,
                                        clip->y,
                                        clip->width,
                                        clip->height,
                                        0x0b1020u,
                                        0);
    } else {
        prism_kernel_display_clear(0x0b1020u);
    }
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        int slot = server->z_order[i];

        if (server->windows[slot].open) {
            struct prism_region visible;

            if (occlusion_ok &&
                prism_window_visible_region(server, i, clip, &visible)) {
                for (uint32_t j = 0u; j < visible.count; j++) {
                    if ((server->windows[slot].flags & PRISM_WINDOW_DESKTOP) != 0u) {
                        prism_draw_desktop(&server->windows[slot],
                                           &visible.rects[j]);
                    } else {
                        prism_draw_window(&server->windows[slot],
                                          server->focused == slot,
                                          &visible.rects[j]);
                    }
                }
            } else {
                occlusion_ok = 0;
                if (prism_window_intersects_clip(&server->windows[slot], clip)) {
                    if ((server->windows[slot].flags & PRISM_WINDOW_DESKTOP) != 0u) {
                        prism_draw_desktop(&server->windows[slot], clip);
                    } else {
                        prism_draw_window(&server->windows[slot],
                                          server->focused == slot,
                                          clip);
                    }
                }
            }
        }
    }
    prism_draw_pointer(server, clip);
}

static int prism_server_init(struct prism_server *server) {
    struct syscall_tty_info tty;

    memset(server, 0, sizeof(*server));
    server->next_window_id = 1u;
    server->focused = -1;
    server->dragging = -1;
    server->resizing = -1;
    server->pointer_capture = -1;
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        server->z_order[i] = (uint8_t)i;
    }
    if (prism_kernel_display_info_query(&server->display) != 0 ||
        server->display.width == 0u ||
        server->display.height == 0u) {
        eprintf("prism: display unavailable\n");
        return 0;
    }
    if (prism_kernel_input_cursor_init(&server->input_cursor) != 0) {
        eprintf("prism: input cursor init failed\n");
        return 0;
    }
    if (prism_query_tty(&tty)) {
        server->home_tty_kind = tty.kind;
        server->home_tty_index = tty.index;
        server->has_home_tty = 1u;
    }
    if (prism_server_queue_for_current_tty(server->queue_name,
                                           sizeof(server->queue_name)) != 0) {
        eprintf("prism: server queue name failed\n");
        return 0;
    }
    (void)prism_kernel_ipc_unlink(server->queue_name);
    server->queue = prism_kernel_ipc_open(server->queue_name, IPC_CREATE | IPC_EXCL);
    if (server->queue < 0) {
        eprintf("prism: server queue unavailable\n");
        return 0;
    }
    if (prism_kernel_input_grab() != 0) {
        eprintf("prism: input grab failed\n");
        (void)prism_kernel_ipc_unlink(server->queue_name);
        return 0;
    }
    server->input_grabbed = 1u;
    server->pointer_x = (int32_t)(server->display.width / 2u);
    server->pointer_y = (int32_t)(server->display.height / 2u);
    server->running = 1u;
    return 1;
}

static void prism_server_shutdown(struct prism_server *server) {
    for (uint32_t i = 0u; i < PRISM_WINDOW_MAX; i++) {
        prism_destroy_window(server, (int)i);
    }
    if (server->input_grabbed) {
        (void)prism_kernel_input_release();
    }
    if (server->queue_name[0] != '\0') {
        (void)prism_kernel_ipc_unlink(server->queue_name);
    }
}

int main(int argc, char **argv) {
    struct prism_server server;
    int smoke = 0;
    int debug_exit = 0;
    int dirty = 1;
    int was_active = 1;
    uint32_t frame = 0u;

    for (int i = 1; i < argc; i++) {
        if (prism_streq(argv[i], "--smoke")) {
            smoke = 1;
        } else if (prism_streq(argv[i], "--debug-exit")) {
            debug_exit = 1;
        }
    }
    if (!prism_server_init(&server)) {
        return 1;
    }
    server.debug_exit = debug_exit ? 1u : 0u;
    printf("prism: server online queue=%s display=%ux%u\n",
           server.queue_name,
           server.display.width,
           server.display.height);
    while (server.running) {
        int active = prism_home_tty_active(&server);

        if (active && !was_active) {
            dirty = 1;
        }
        was_active = active;
        prism_update_home_tty(&server, active);
        dirty |= prism_drain_messages(&server);
        if (!active) {
            sleep(2u);
            continue;
        }
        if (server.input_grabbed) {
            dirty |= prism_drain_input(&server);
        }
        if (smoke) {
            dirty = 1;
        }
        if (!dirty) {
            sleep(2u);
            continue;
        }
        prism_draw(&server);
        if (prism_kernel_display_present() != 0) {
            eprintf("prism: gfx present failed\n");
            break;
        }
        prism_clear_damage(&server);
        dirty = 0;
        if (smoke && ++frame >= PRISM_SMOKE_FRAMES) {
            break;
        }
        sleep(2u);
    }
    prism_server_shutdown(&server);
    printf("prism: server stopped\n");
    return 0;
}
