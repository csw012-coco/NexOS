#include "user/libc/include/nlibc.h"

enum {
    WM_WINDOW_MAX = 4,
    WM_TITLEBAR_HEIGHT = 24,
    WM_CLOSE_SIZE = 12,
    WM_GFX_BATCH_CAPACITY = SYS_GFX_BATCH_MAX_COMMANDS
};

static struct syscall_gfx_batch_entry g_wm_gfx_batch[WM_GFX_BATCH_CAPACITY];

struct wm_window {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t accent;
    uint32_t body;
    uint32_t content;
    int open;
};

struct wm_state {
    struct wm_window windows[WM_WINDOW_MAX];
    uint8_t z_order[WM_WINDOW_MAX];
    int focused;
    int dragging;
    int32_t drag_dx;
    int32_t drag_dy;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t last_buttons;
};

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int point_in_rect(int32_t px, int32_t py, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    return px >= x &&
           py >= y &&
           px < x + (int32_t)width &&
           py < y + (int32_t)height;
}

static void wm_init(struct wm_state *wm, uint32_t screen_w, uint32_t screen_h) {
    wm->windows[0].x = 46;
    wm->windows[0].y = 62;
    wm->windows[0].width = 260u;
    wm->windows[0].height = 166u;
    wm->windows[0].accent = 0x2563ebu;
    wm->windows[0].body = 0xe8eef8u;
    wm->windows[0].content = 0x38bdf8u;
    wm->windows[0].open = 1;

    wm->windows[1].x = 212;
    wm->windows[1].y = 132;
    wm->windows[1].width = 230u;
    wm->windows[1].height = 148u;
    wm->windows[1].accent = 0x16a34au;
    wm->windows[1].body = 0xecfdf5u;
    wm->windows[1].content = 0xa7f3d0u;
    wm->windows[1].open = 1;

    wm->windows[2].x = 410;
    wm->windows[2].y = 82;
    wm->windows[2].width = 220u;
    wm->windows[2].height = 190u;
    wm->windows[2].accent = 0xdb2777u;
    wm->windows[2].body = 0xfdf2f8u;
    wm->windows[2].content = 0xf9a8d4u;
    wm->windows[2].open = 1;

    wm->windows[3].x = 126;
    wm->windows[3].y = 326;
    wm->windows[3].width = 310u;
    wm->windows[3].height = 132u;
    wm->windows[3].accent = 0xf59e0bu;
    wm->windows[3].body = 0xfffbebu;
    wm->windows[3].content = 0xfcd34du;
    wm->windows[3].open = 1;

    for (uint32_t i = 0; i < WM_WINDOW_MAX; i++) {
        wm->z_order[i] = (uint8_t)i;
    }
    wm->focused = 3;
    wm->dragging = -1;
    wm->drag_dx = 0;
    wm->drag_dy = 0;
    wm->pointer_x = (int32_t)(screen_w / 2u);
    wm->pointer_y = (int32_t)(screen_h / 2u);
    wm->last_buttons = 0u;
}

static void wm_focus(struct wm_state *wm, int id) {
    uint8_t next_order[WM_WINDOW_MAX];
    uint32_t out = 0;

    if (id < 0 || id >= WM_WINDOW_MAX || !wm->windows[id].open) {
        return;
    }
    for (uint32_t i = 0; i < WM_WINDOW_MAX; i++) {
        if (wm->z_order[i] != (uint8_t)id) {
            next_order[out++] = wm->z_order[i];
        }
    }
    next_order[out++] = (uint8_t)id;
    for (uint32_t i = 0; i < WM_WINDOW_MAX; i++) {
        wm->z_order[i] = next_order[i];
    }
    wm->focused = id;
}

static void wm_focus_next(struct wm_state *wm) {
    int start = wm->focused >= 0 ? wm->focused : 0;

    for (uint32_t i = 1; i <= WM_WINDOW_MAX; i++) {
        int id = (start + (int)i) % WM_WINDOW_MAX;

        if (wm->windows[id].open) {
            wm_focus(wm, id);
            return;
        }
    }
}

static int wm_hit_window(const struct wm_state *wm, int32_t x, int32_t y) {
    for (int i = WM_WINDOW_MAX - 1; i >= 0; i--) {
        int id = wm->z_order[i];
        const struct wm_window *win = &wm->windows[id];

        if (win->open && point_in_rect(x, y, win->x, win->y, win->width, win->height)) {
            return id;
        }
    }
    return -1;
}

static int wm_hit_titlebar(const struct wm_window *win, int32_t x, int32_t y) {
    return point_in_rect(x, y, win->x, win->y, win->width, WM_TITLEBAR_HEIGHT);
}

static int wm_hit_close(const struct wm_window *win, int32_t x, int32_t y) {
    return point_in_rect(x,
                         y,
                         win->x + 8,
                         win->y + (WM_TITLEBAR_HEIGHT - WM_CLOSE_SIZE) / 2,
                         WM_CLOSE_SIZE,
                         WM_CLOSE_SIZE);
}

static void wm_close_window(struct wm_state *wm, int id) {
    if (id < 0 || id >= WM_WINDOW_MAX) {
        return;
    }
    wm->windows[id].open = 0;
    wm->dragging = -1;
    if (wm->focused == id) {
        wm->focused = -1;
        wm_focus_next(wm);
    }
}

static void wm_handle_mouse(struct wm_state *wm,
                            const struct syscall_gui_event *event,
                            uint32_t screen_w,
                            uint32_t screen_h) {
    uint32_t old_buttons = wm->last_buttons;
    int left_down;
    int left_was_down;

    wm->pointer_x = clamp_i32(wm->pointer_x + event->dx, 0, (int32_t)screen_w - 1);
    wm->pointer_y = clamp_i32(wm->pointer_y + event->dy, 0, (int32_t)screen_h - 1);
    wm->last_buttons = event->buttons;

    left_down = (event->buttons & SYS_GUI_MOUSE_LEFT) != 0u;
    left_was_down = (old_buttons & SYS_GUI_MOUSE_LEFT) != 0u;
    if (left_down && !left_was_down) {
        int id = wm_hit_window(wm, wm->pointer_x, wm->pointer_y);

        wm_focus(wm, id);
        if (id >= 0) {
            struct wm_window *win = &wm->windows[id];

            if (wm_hit_close(win, wm->pointer_x, wm->pointer_y)) {
                wm_close_window(wm, id);
                return;
            }
            if (wm_hit_titlebar(win, wm->pointer_x, wm->pointer_y)) {
                wm->dragging = id;
                wm->drag_dx = wm->pointer_x - win->x;
                wm->drag_dy = wm->pointer_y - win->y;
            }
        }
    } else if (!left_down && left_was_down) {
        wm->dragging = -1;
    }

    if (left_down && wm->dragging >= 0) {
        struct wm_window *win = &wm->windows[wm->dragging];
        int32_t max_x = (int32_t)screen_w - (int32_t)win->width;
        int32_t max_y = (int32_t)screen_h - 42;

        win->x = clamp_i32(wm->pointer_x - wm->drag_dx, 0, max_x > 0 ? max_x : 0);
        win->y = clamp_i32(wm->pointer_y - wm->drag_dy, 30, max_y > 30 ? max_y : 30);
    }
}

static void draw_desktop(uint32_t width, uint32_t height) {
    gfx_clear(0x0b1020u);
    for (uint32_t x = 0; x < width; x += 40u) {
        gfx_draw_line((int32_t)x, 30, (int32_t)x, (int32_t)height - 39, 0x17213au);
    }
    for (uint32_t y = 30; y < height - 38u; y += 40u) {
        gfx_draw_line(0, (int32_t)y, (int32_t)width - 1, (int32_t)y, 0x17213au);
    }
    gfx_fill_rect(0, 0, width, 30u, 0x111827u);
    gfx_fill_rect(0, (int32_t)height - 38, width, 38u, 0x111827u);
    gfx_fill_rect(16, 8, 96u, 14u, 0x22d3eeu);
    gfx_fill_rect(128, 8, 70u, 14u, 0xfbbf24u);
    gfx_fill_rect(214, 8, 86u, 14u, 0x34d399u);
}

static void draw_window(const struct wm_window *win, int focused, uint32_t frame) {
    uint32_t border = focused ? 0xf8fafcu : 0x475569u;
    uint32_t shade = focused ? 0x020617u : 0x111827u;
    int32_t x = win->x;
    int32_t y = win->y;

    gfx_fill_rect(x + 7, y + 9, win->width, win->height, shade);
    gfx_fill_rect(x, y, win->width, win->height, win->body);
    gfx_fill_rect(x, y, win->width, WM_TITLEBAR_HEIGHT, focused ? win->accent : 0x334155u);
    gfx_draw_rect(x, y, win->width, win->height, border);
    gfx_fill_rect(x + 8, y + 6, WM_CLOSE_SIZE, WM_CLOSE_SIZE, 0xef4444u);
    gfx_draw_line(x + 11, y + 9, x + 17, y + 15, 0xfff1f2u);
    gfx_draw_line(x + 17, y + 9, x + 11, y + 15, 0xfff1f2u);
    gfx_fill_rect(x + 30, y + 8, win->width > 118u ? win->width - 94u : 24u, 8u, 0xdbeafeu);

    gfx_fill_rect(x + 16, y + 42, win->width - 32u, 16u, 0xcbd5e1u);
    gfx_fill_rect(x + 16, y + 70, (win->width - 44u) / 2u, 34u, win->content);
    gfx_fill_rect(x + 30 + (int32_t)((win->width - 44u) / 2u), y + 70, (win->width - 48u) / 2u, 34u, 0xffffffu);
    gfx_draw_line(x + 18,
                  y + (int32_t)win->height - 24,
                  x + (int32_t)win->width - 18,
                  y + 44 + (int32_t)(frame % 18u),
                  0x334155u);
    gfx_fill_circle(x + (int32_t)win->width - 34,
                    y + (int32_t)win->height - 34,
                    12u + (frame % 5u),
                    focused ? 0xf97316u : 0x94a3b8u);
}

static void draw_taskbar(const struct wm_state *wm, uint32_t width, uint32_t height) {
    int32_t x = 16;

    (void)width;
    for (uint32_t i = 0; i < WM_WINDOW_MAX; i++) {
        const struct wm_window *win = &wm->windows[i];
        uint32_t fill;

        if (!win->open) {
            continue;
        }
        fill = wm->focused == (int)i ? win->accent : 0x334155u;
        gfx_fill_rect(x, (int32_t)height - 27, 52u, 16u, fill);
        gfx_draw_rect(x, (int32_t)height - 27, 52u, 16u, 0xe5e7ebu);
        x += 62;
    }
}

static void draw_pointer(int32_t x, int32_t y, int pressed) {
    uint32_t fill = pressed ? 0xf97316u : 0xe0f2feu;
    uint32_t outline = pressed ? 0xffedd5u : 0x0284c7u;

    gfx_fill_circle(x, y, 6u, fill);
    gfx_draw_circle(x, y, 9u, outline);
    gfx_draw_line(x - 14, y, x + 14, y, outline);
    gfx_draw_line(x, y - 14, x, y + 14, outline);
}

static void wm_draw(const struct wm_state *wm, uint32_t width, uint32_t height, uint32_t frame) {
    draw_desktop(width, height);
    for (uint32_t i = 0; i < WM_WINDOW_MAX; i++) {
        int id = wm->z_order[i];

        if (wm->windows[id].open) {
            draw_window(&wm->windows[id], wm->focused == id, frame);
        }
    }
    draw_taskbar(wm, width, height);
    draw_pointer(wm->pointer_x, wm->pointer_y, (wm->last_buttons & SYS_GUI_MOUSE_LEFT) != 0u);
}

int main(int argc, char **argv) {
    struct syscall_gfx_info info;
    struct syscall_gui_event event;
    struct wm_state wm;
    uint32_t frames = 600u;
    int running = 1;

    (void)argc;
    (void)argv;

    if (gfx_info(&info) != 0 || info.width == 0u || info.height == 0u) {
        eprintf("guidemo: framebuffer graphics unavailable\n");
        return 1;
    }

    wm_init(&wm, info.width, info.height);
    printf("guidemo: window manager prototype, drag titlebars, click close, Tab cycles focus, Esc exits\n");
    for (uint32_t frame = 0; frame < frames && running; frame++) {
        while (gui_poll_event(&event) == SYS_GUI_EVENT_READY) {
            if (event.type == SYS_GUI_EVENT_MOUSE) {
                wm_handle_mouse(&wm, &event, info.width, info.height);
            } else if (event.type == SYS_GUI_EVENT_KEY && event.pressed) {
                if (event.keycode == SYS_KEY_ESC) {
                    running = 0;
                } else if (event.keycode == SYS_KEY_TAB) {
                    wm_focus_next(&wm);
                } else if (event.keycode == SYS_KEY_BACKSPACE) {
                    wm_close_window(&wm, wm.focused);
                }
            }
        }
        (void)gfx_batch_begin(g_wm_gfx_batch, WM_GFX_BATCH_CAPACITY);
        wm_draw(&wm, info.width, info.height, frame);
        gfx_present();
        sleep(2u);
    }
    printf("guidemo done\n");
    return 0;
}
