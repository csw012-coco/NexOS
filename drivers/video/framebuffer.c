#include "drivers/video/framebuffer.h"
#include "drivers/video/framebuffer_font.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "hal/hal.h"
#include <stddef.h>
#include "lib/string.h"

enum {
    FRAMEBUFFER_CURSOR_DISABLED = 0,
    FRAMEBUFFER_CURSOR_ENABLED = 1,
    FRAMEBUFFER_FONT_WIDTH = 8,
    FRAMEBUFFER_FONT_HEIGHT = 16,
    FRAMEBUFFER_FONT_HEIGHT_SMALL = 8,
    FRAMEBUFFER_CURSOR_UNDERLINE_ROWS = 2,
    FRAMEBUFFER_CURSOR_BLINK_TICKS = 50,
    FRAMEBUFFER_GLYPH_CACHE_SIZE = 2048,
    FRAMEBUFFER_FONT_INDEX_PAGES = 0x1100,
    FRAMEBUFFER_MOUSE_CURSOR_WIDTH = 12,
    FRAMEBUFFER_MOUSE_CURSOR_HEIGHT = 18,
    FRAMEBUFFER_MOUSE_CURSOR_PIXELS = FRAMEBUFFER_MOUSE_CURSOR_WIDTH * FRAMEBUFFER_MOUSE_CURSOR_HEIGHT
};

struct framebuffer_cached_glyph {
    uint32_t codepoint;
    uint8_t valid;
    uint8_t width;
    uint16_t rows[FRAMEBUFFER_FONT_HEIGHT];
};

struct framebuffer_display_state {
    int active;
    volatile uint8_t *front_base;
    volatile uint8_t *base;
    uint8_t backbuffer_enabled;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint16_t columns;
    uint16_t rows;
    uint16_t origin_x;
    uint16_t origin_y;
    uint8_t cell_height;
    uint8_t cursor_enabled;
    uint8_t cursor_visible;
    uint8_t cursor_start;
    uint8_t cursor_end;
    uint16_t cursor_row;
    uint16_t cursor_col;
    uint32_t cursor_blink_ticks;
    uint8_t dirty_valid;
    uint32_t dirty_x0;
    uint32_t dirty_y0;
    uint32_t dirty_x1;
    uint32_t dirty_y1;
    uint8_t mouse_cursor_enabled;
    uint8_t mouse_cursor_visible;
    uint8_t mouse_cursor_initialized;
    int32_t mouse_cursor_x;
    int32_t mouse_cursor_y;
    uint32_t mouse_cursor_saved_x;
    uint32_t mouse_cursor_saved_y;
    uint32_t mouse_cursor_saved_width;
    uint32_t mouse_cursor_saved_height;
    uint32_t mouse_cursor_saved[FRAMEBUFFER_MOUSE_CURSOR_PIXELS];
    uint32_t cells[FRAMEBUFFER_TEXT_MAX_ROWS][FRAMEBUFFER_TEXT_MAX_COLUMNS];
};

static struct framebuffer_display_state g_framebuffer_display;
static uint8_t g_framebuffer_active_font[256 * FRAMEBUFFER_FONT_HEIGHT];
static struct framebuffer_cached_glyph g_framebuffer_glyph_cache[FRAMEBUFFER_GLYPH_CACHE_SIZE];
static uint32_t g_framebuffer_glyph_cache_next;
static const char *g_framebuffer_font_module_text;
static uint32_t g_framebuffer_font_module_size;
static uint32_t g_framebuffer_font_page_offsets[FRAMEBUFFER_FONT_INDEX_PAGES];
static uint8_t g_framebuffer_font_page_valid[FRAMEBUFFER_FONT_INDEX_PAGES];

static const uint32_t g_framebuffer_palette[16][3] = {
    {0x00u, 0x00u, 0x00u},
    {0x00u, 0x00u, 0xaau},
    {0x00u, 0xaau, 0x00u},
    {0x00u, 0xaau, 0xaau},
    {0xaau, 0x00u, 0x00u},
    {0xaau, 0x00u, 0xaau},
    {0xaau, 0x55u, 0x00u},
    {0xaau, 0xaau, 0xaau},
    {0x55u, 0x55u, 0x55u},
    {0x55u, 0x55u, 0xffu},
    {0x55u, 0xffu, 0x55u},
    {0x55u, 0xffu, 0xffu},
    {0xffu, 0x55u, 0x55u},
    {0xffu, 0x55u, 0xffu},
    {0xffu, 0xffu, 0x55u},
    {0xffu, 0xffu, 0xffu}
};

static const uint16_t g_framebuffer_mouse_cursor_outline[FRAMEBUFFER_MOUSE_CURSOR_HEIGHT] = {
    0x8000u, 0xc000u, 0xe000u, 0xf000u, 0xf800u, 0xfc00u,
    0xfe00u, 0xff00u, 0xff80u, 0xffc0u, 0xffe0u, 0xf800u,
    0xdc00u, 0x8c00u, 0x0600u, 0x0600u, 0x0300u, 0x0300u
};

static const uint16_t g_framebuffer_mouse_cursor_fill[FRAMEBUFFER_MOUSE_CURSOR_HEIGHT] = {
    0x0000u, 0x4000u, 0x6000u, 0x7000u, 0x7800u, 0x7c00u,
    0x7e00u, 0x7f00u, 0x7f80u, 0x7c00u, 0x6c00u, 0x4800u,
    0x4400u, 0x0400u, 0x0200u, 0x0200u, 0x0100u, 0x0000u
};

static uint32_t framebuffer_pack_cell(uint32_t codepoint, uint8_t color, uint32_t flags) {
    return ((uint32_t)color << HAL_DISPLAY_CELL_COLOR_SHIFT) |
           (flags & HAL_DISPLAY_CELL_FLAGS_MASK) |
           (codepoint & HAL_DISPLAY_CELL_CODEPOINT_MASK);
}

static uint32_t framebuffer_blank_cell(uint8_t color) {
    return framebuffer_pack_cell(' ', color, 0);
}

static void framebuffer_glyph_cache_clear(void) {
    g_framebuffer_glyph_cache_next = 0;
    for (uint32_t i = 0; i < FRAMEBUFFER_GLYPH_CACHE_SIZE; i++) {
        g_framebuffer_glyph_cache[i].valid = 0;
    }
    for (uint32_t i = 0; i < FRAMEBUFFER_FONT_INDEX_PAGES; i++) {
        g_framebuffer_font_page_valid[i] = 0;
        g_framebuffer_font_page_offsets[i] = 0;
    }
}

static uint32_t framebuffer_pack_rgb(const struct framebuffer_display_state *state,
                                     uint8_t r,
                                     uint8_t g,
                                     uint8_t b) {
    uint32_t value = 0;

    if (state->bpp == 8u) {
        if (r > 220u && g > 220u && b > 220u) {
            return 15u;
        }
        if (r > 220u && g > 180u) {
            return 14u;
        }
        if (g > 180u && b > 140u) {
            return 11u;
        }
        if (g > 180u) {
            return 10u;
        }
        if (b > 180u) {
            return 9u;
        }
        if (r > 180u) {
            return 12u;
        }
        return 1u;
    }

    if (state->red_mask_size != 0u) {
        value |= ((uint32_t)r >> (8u - state->red_mask_size)) << state->red_mask_shift;
    }
    if (state->green_mask_size != 0u) {
        value |= ((uint32_t)g >> (8u - state->green_mask_size)) << state->green_mask_shift;
    }
    if (state->blue_mask_size != 0u) {
        value |= ((uint32_t)b >> (8u - state->blue_mask_size)) << state->blue_mask_shift;
    }
    return value;
}

static uint32_t framebuffer_vga_color_to_pixel(const struct framebuffer_display_state *state, uint8_t color) {
    const uint32_t *rgb = g_framebuffer_palette[color & 0x0fu];

    return framebuffer_pack_rgb(state, (uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
}

static void framebuffer_mark_dirty(struct framebuffer_display_state *state,
                                   uint32_t x,
                                   uint32_t y,
                                   uint32_t width,
                                   uint32_t height) {
    uint32_t x1;
    uint32_t y1;

    if (state == 0 || width == 0u || height == 0u || x >= state->width || y >= state->height) {
        return;
    }
    x1 = x + width;
    y1 = y + height;
    if (x1 > state->width || x1 < x) {
        x1 = state->width;
    }
    if (y1 > state->height || y1 < y) {
        y1 = state->height;
    }
    if (x1 <= x || y1 <= y) {
        return;
    }

    if (!state->dirty_valid) {
        state->dirty_valid = 1u;
        state->dirty_x0 = x;
        state->dirty_y0 = y;
        state->dirty_x1 = x1;
        state->dirty_y1 = y1;
        return;
    }
    if (x < state->dirty_x0) {
        state->dirty_x0 = x;
    }
    if (y < state->dirty_y0) {
        state->dirty_y0 = y;
    }
    if (x1 > state->dirty_x1) {
        state->dirty_x1 = x1;
    }
    if (y1 > state->dirty_y1) {
        state->dirty_y1 = y1;
    }
}

static void framebuffer_flush_dirty(struct framebuffer_display_state *state) {
    uint32_t bytes_per_pixel;
    uint32_t row_bytes;

    if (state == 0 || !state->backbuffer_enabled || !state->dirty_valid ||
        state->front_base == 0 || state->base == 0) {
        if (state != 0) {
            state->dirty_valid = 0u;
        }
        return;
    }

    bytes_per_pixel = state->bpp / 8u;
    row_bytes = (state->dirty_x1 - state->dirty_x0) * bytes_per_pixel;
    for (uint32_t y = state->dirty_y0; y < state->dirty_y1; y++) {
        volatile uint8_t *src = state->base + (uint64_t)y * state->pitch +
                                (uint64_t)state->dirty_x0 * bytes_per_pixel;
        volatile uint8_t *dst = state->front_base + (uint64_t)y * state->pitch +
                                (uint64_t)state->dirty_x0 * bytes_per_pixel;

        memmove((void *)dst, (const void *)src, row_bytes);
    }
    state->dirty_valid = 0u;
}

static void framebuffer_write_pixel(const struct framebuffer_display_state *state,
                                    uint32_t x,
                                    uint32_t y,
                                    uint32_t color) {
    volatile uint8_t *pixel;

    if (x >= state->width || y >= state->height) {
        return;
    }

    pixel = state->base + (uint64_t)y * state->pitch + (uint64_t)x * (state->bpp / 8u);
    if (state->bpp == 32u) {
        *(volatile uint32_t *)pixel = color;
    } else if (state->bpp == 24u) {
        pixel[0] = (uint8_t)color;
        pixel[1] = (uint8_t)(color >> 8);
        pixel[2] = (uint8_t)(color >> 16);
    } else if (state->bpp == 8u) {
        *pixel = (uint8_t)color;
    }
}

static uint32_t framebuffer_read_pixel(const struct framebuffer_display_state *state,
                                       uint32_t x,
                                       uint32_t y) {
    volatile uint8_t *pixel;

    if (state == 0 || state->base == 0 || x >= state->width || y >= state->height) {
        return 0;
    }
    pixel = state->base + (uint64_t)y * state->pitch + (uint64_t)x * (state->bpp / 8u);
    if (state->bpp == 32u) {
        return *(volatile uint32_t *)pixel;
    }
    if (state->bpp == 24u) {
        return (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16);
    }
    if (state->bpp == 8u) {
        return *pixel;
    }
    return 0;
}

static void framebuffer_fill_rect(const struct framebuffer_display_state *state,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t color) {
    uint32_t bytes_per_pixel;

    if (state == 0 || state->base == 0 || width == 0u || height == 0u) {
        return;
    }
    if (x >= state->width || y >= state->height) {
        return;
    }
    if (x + width > state->width) {
        width = state->width - x;
    }
    if (y + height > state->height) {
        height = state->height - y;
    }

    bytes_per_pixel = state->bpp / 8u;
    if (bytes_per_pixel == 4u && x == 0u && width == state->width && state->pitch == state->width * 4u) {
        volatile uint32_t *dst = (volatile uint32_t *)(state->base + (uint64_t)y * state->pitch);
        uint32_t pixels = width * height;

        for (uint32_t i = 0; i < pixels; i++) {
            dst[i] = color;
        }
        framebuffer_mark_dirty((struct framebuffer_display_state *)state, x, y, width, height);
        return;
    }

    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            framebuffer_write_pixel(state, x + col, y + row, color);
        }
    }
    framebuffer_mark_dirty((struct framebuffer_display_state *)state, x, y, width, height);
}

static uint32_t framebuffer_rgb_to_pixel(const struct framebuffer_display_state *state, uint32_t rgb) {
    return framebuffer_pack_rgb(state,
                                (uint8_t)((rgb >> 16) & 0xffu),
                                (uint8_t)((rgb >> 8) & 0xffu),
                                (uint8_t)(rgb & 0xffu));
}

static uint32_t framebuffer_xrgb_to_pixel(const struct framebuffer_display_state *state, uint32_t xrgb) {
    return framebuffer_pack_rgb(state,
                                (uint8_t)((xrgb >> 16) & 0xffu),
                                (uint8_t)((xrgb >> 8) & 0xffu),
                                (uint8_t)(xrgb & 0xffu));
}

static int32_t framebuffer_abs_i32(int32_t value) {
    return value < 0 ? -value : value;
}

static int32_t framebuffer_min_i32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t framebuffer_max_i32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static void framebuffer_write_pixel_i32(const struct framebuffer_display_state *state,
                                        int32_t x,
                                        int32_t y,
                                        uint32_t color) {
    if (state == 0 || state->base == 0 || x < 0 || y < 0) {
        return;
    }
    framebuffer_write_pixel(state, (uint32_t)x, (uint32_t)y, color);
}

static void framebuffer_mark_dirty_i32(struct framebuffer_display_state *state,
                                       int32_t x0,
                                       int32_t y0,
                                       int32_t x1,
                                       int32_t y1) {
    if (state == 0 || x1 < 0 || y1 < 0 ||
        x0 >= (int32_t)state->width || y0 >= (int32_t)state->height) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= (int32_t)state->width) {
        x1 = (int32_t)state->width - 1;
    }
    if (y1 >= (int32_t)state->height) {
        y1 = (int32_t)state->height - 1;
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }
    framebuffer_mark_dirty(state,
                           (uint32_t)x0,
                           (uint32_t)y0,
                           (uint32_t)(x1 - x0 + 1),
                           (uint32_t)(y1 - y0 + 1));
}

static void framebuffer_draw_line_raw(struct framebuffer_display_state *state,
                                      int32_t x0,
                                      int32_t y0,
                                      int32_t x1,
                                      int32_t y1,
                                      uint32_t color) {
    int32_t dx;
    int32_t dy;
    int32_t sx;
    int32_t sy;
    int32_t err;

    if (state == 0 || state->base == 0) {
        return;
    }
    dx = framebuffer_abs_i32(x1 - x0);
    dy = -framebuffer_abs_i32(y1 - y0);
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;

    for (;;) {
        int32_t e2;

        framebuffer_write_pixel_i32(state, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void framebuffer_draw_circle_points(struct framebuffer_display_state *state,
                                           int32_t cx,
                                           int32_t cy,
                                           int32_t x,
                                           int32_t y,
                                           uint32_t color) {
    framebuffer_write_pixel_i32(state, cx + x, cy + y, color);
    framebuffer_write_pixel_i32(state, cx - x, cy + y, color);
    framebuffer_write_pixel_i32(state, cx + x, cy - y, color);
    framebuffer_write_pixel_i32(state, cx - x, cy - y, color);
    framebuffer_write_pixel_i32(state, cx + y, cy + x, color);
    framebuffer_write_pixel_i32(state, cx - y, cy + x, color);
    framebuffer_write_pixel_i32(state, cx + y, cy - x, color);
    framebuffer_write_pixel_i32(state, cx - y, cy - x, color);
}

static int64_t framebuffer_triangle_edge(int32_t ax,
                                         int32_t ay,
                                         int32_t bx,
                                         int32_t by,
                                         int32_t px,
                                         int32_t py) {
    return (int64_t)(px - ax) * (int64_t)(by - ay) -
           (int64_t)(py - ay) * (int64_t)(bx - ax);
}

static void framebuffer_restore_mouse_cursor(struct framebuffer_display_state *state) {
    uint32_t index = 0;

    if (state == 0 || !state->mouse_cursor_visible || state->base == 0) {
        return;
    }
    for (uint32_t y = 0; y < state->mouse_cursor_saved_height; y++) {
        for (uint32_t x = 0; x < state->mouse_cursor_saved_width; x++) {
            framebuffer_write_pixel(state,
                                    state->mouse_cursor_saved_x + x,
                                    state->mouse_cursor_saved_y + y,
                                    state->mouse_cursor_saved[index++]);
        }
    }
    framebuffer_mark_dirty(state,
                           state->mouse_cursor_saved_x,
                           state->mouse_cursor_saved_y,
                           state->mouse_cursor_saved_width,
                           state->mouse_cursor_saved_height);
    state->mouse_cursor_visible = 0u;
}

static void framebuffer_draw_mouse_cursor(struct framebuffer_display_state *state) {
    uint32_t x0;
    uint32_t y0;
    uint32_t width;
    uint32_t height;
    uint32_t index = 0;
    uint32_t outline;
    uint32_t fill;

    if (state == 0 || !state->mouse_cursor_enabled || state->mouse_cursor_visible ||
        state->base == 0 || state->width == 0u || state->height == 0u) {
        return;
    }
    if (state->mouse_cursor_x < 0 || state->mouse_cursor_y < 0 ||
        state->mouse_cursor_x >= (int32_t)state->width ||
        state->mouse_cursor_y >= (int32_t)state->height) {
        return;
    }

    x0 = (uint32_t)state->mouse_cursor_x;
    y0 = (uint32_t)state->mouse_cursor_y;
    width = FRAMEBUFFER_MOUSE_CURSOR_WIDTH;
    height = FRAMEBUFFER_MOUSE_CURSOR_HEIGHT;
    if (x0 + width > state->width) {
        width = state->width - x0;
    }
    if (y0 + height > state->height) {
        height = state->height - y0;
    }
    if (width == 0u || height == 0u) {
        return;
    }

    state->mouse_cursor_saved_x = x0;
    state->mouse_cursor_saved_y = y0;
    state->mouse_cursor_saved_width = width;
    state->mouse_cursor_saved_height = height;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            state->mouse_cursor_saved[index++] = framebuffer_read_pixel(state, x0 + x, y0 + y);
        }
    }

    outline = framebuffer_rgb_to_pixel(state, 0x000000u);
    fill = framebuffer_rgb_to_pixel(state, 0xffffffu);
    for (uint32_t y = 0; y < height; y++) {
        uint16_t outline_bits = g_framebuffer_mouse_cursor_outline[y];
        uint16_t fill_bits = g_framebuffer_mouse_cursor_fill[y];

        for (uint32_t x = 0; x < width; x++) {
            uint16_t mask = (uint16_t)(0x8000u >> x);

            if ((outline_bits & mask) != 0u) {
                framebuffer_write_pixel(state, x0 + x, y0 + y, outline);
            }
            if ((fill_bits & mask) != 0u) {
                framebuffer_write_pixel(state, x0 + x, y0 + y, fill);
            }
        }
    }
    framebuffer_mark_dirty(state, x0, y0, width, height);
    state->mouse_cursor_visible = 1u;
}

static int framebuffer_rects_overlap(uint32_t ax,
                                     uint32_t ay,
                                     uint32_t aw,
                                     uint32_t ah,
                                     uint32_t bx,
                                     uint32_t by,
                                     uint32_t bw,
                                     uint32_t bh) {
    uint32_t ax1;
    uint32_t ay1;
    uint32_t bx1;
    uint32_t by1;

    if (aw == 0u || ah == 0u || bw == 0u || bh == 0u) {
        return 0;
    }
    ax1 = ax + aw;
    ay1 = ay + ah;
    bx1 = bx + bw;
    by1 = by + bh;
    if (ax1 < ax) {
        ax1 = 0xffffffffu;
    }
    if (ay1 < ay) {
        ay1 = 0xffffffffu;
    }
    if (bx1 < bx) {
        bx1 = 0xffffffffu;
    }
    if (by1 < by) {
        by1 = 0xffffffffu;
    }
    return ax < bx1 && bx < ax1 && ay < by1 && by < ay1;
}

static int framebuffer_begin_mouse_cursor_covered_update(struct framebuffer_display_state *state,
                                                         uint32_t x,
                                                         uint32_t y,
                                                         uint32_t width,
                                                         uint32_t height) {
    if (state == 0 || !state->mouse_cursor_visible) {
        return 0;
    }
    if (!framebuffer_rects_overlap(x,
                                   y,
                                   width,
                                   height,
                                   state->mouse_cursor_saved_x,
                                   state->mouse_cursor_saved_y,
                                   state->mouse_cursor_saved_width,
                                   state->mouse_cursor_saved_height)) {
        return 0;
    }
    framebuffer_restore_mouse_cursor(state);
    return 1;
}

static void framebuffer_end_mouse_cursor_covered_update(struct framebuffer_display_state *state, int redraw) {
    if (redraw) {
        framebuffer_draw_mouse_cursor(state);
    }
    framebuffer_flush_dirty(state);
}

static int framebuffer_hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int framebuffer_parse_hex_byte(const char *text, uint8_t *value) {
    int hi;
    int lo;

    if (text == 0 || value == 0) {
        return 0;
    }
    hi = framebuffer_hex_digit_value(text[0]);
    lo = framebuffer_hex_digit_value(text[1]);
    if (hi < 0 || lo < 0) {
        return 0;
    }
    *value = (uint8_t)((hi << 4) | lo);
    return 1;
}

static int framebuffer_parse_hex_codepoint(const char *text,
                                           uint32_t len,
                                           uint32_t *digits_out,
                                           uint32_t *value) {
    uint32_t result = 0;
    uint32_t digits = 0;

    if (text == 0 || value == 0) {
        return 0;
    }
    while (digits < len && text[digits] != ':') {
        int digit = framebuffer_hex_digit_value(text[digits]);

        if (digit < 0) {
            return 0;
        }
        if (digits >= 6u) {
            return 0;
        }
        result = (result << 4) | (uint32_t)digit;
        digits++;
    }
    if (digits == 0u || digits >= len || text[digits] != ':' || result > 0x10ffffu) {
        return 0;
    }
    if (digits_out != 0) {
        *digits_out = digits;
    }
    *value = result;
    return 1;
}

static int framebuffer_parse_hex_glyph(const char *text,
                                       uint32_t len,
                                       uint8_t *width,
                                       uint16_t rows[FRAMEBUFFER_FONT_HEIGHT]) {
    uint32_t bytes_per_row;

    if (text == 0 || width == 0 || rows == 0) {
        return 0;
    }
    if (len == FRAMEBUFFER_FONT_HEIGHT * 2u) {
        bytes_per_row = 1;
        *width = 8;
    } else if (len == FRAMEBUFFER_FONT_HEIGHT * 4u) {
        bytes_per_row = 2;
        *width = 16;
    } else {
        return 0;
    }

    for (uint32_t row = 0; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
        uint16_t bits = 0;

        for (uint32_t byte = 0; byte < bytes_per_row; byte++) {
            uint8_t value = 0;

            if (!framebuffer_parse_hex_byte(text + (row * bytes_per_row + byte) * 2u, &value)) {
                return 0;
            }
            bits = (uint16_t)((bits << 8) | value);
        }
        rows[row] = bits;
    }
    return 1;
}

static int framebuffer_parse_hex_font_line(const char *line,
                                           uint32_t len,
                                           uint32_t *codepoint,
                                           uint8_t *width,
                                           uint16_t rows[FRAMEBUFFER_FONT_HEIGHT]) {
    uint32_t digits = 0;

    if (!framebuffer_parse_hex_codepoint(line, len, &digits, codepoint)) {
        return 0;
    }
    return framebuffer_parse_hex_glyph(line + digits + 1u, len - digits - 1u, width, rows);
}

static int framebuffer_module_name_eq(const char name[12], const char *target) {
    char padded[11];
    uint32_t i = 0;
    uint32_t out = 0;

    if (target == 0) {
        return 0;
    }

    for (i = 0; i < 11u; i++) {
        padded[i] = ' ';
    }

    i = 0;

    while (target[i] != '\0' && out < 11u) {
        if (target[i] == '.') {
            out = 8u;
            i++;
            continue;
        }

        padded[out++] = target[i++];
    }

    for (i = 0; i < 11u; i++) {
        if (name[i] != padded[i]) {
            return 0;
        }
    }

    return 1;
}

static void framebuffer_font_reset_to_builtin(void) {
    for (uint32_t i = 0; i < sizeof(g_framebuffer_active_font); i++) {
        g_framebuffer_active_font[i] = g_framebuffer_font[i];
    }
    g_framebuffer_font_module_text = 0;
    g_framebuffer_font_module_size = 0;
    framebuffer_glyph_cache_clear();
}

static int framebuffer_try_load_hex_font_module(const struct bootx_module *module) {
    const char *text;
    uint32_t offset = 0;
    int loaded_any = 0;

    if (module == 0 || module->address == 0 || module->size == 0) {
        return 0;
    }

    text = (const char *)(uintptr_t)module->address;
    g_framebuffer_font_module_text = text;
    g_framebuffer_font_module_size = module->size;
    framebuffer_glyph_cache_clear();

    while (offset < module->size) {
        uint32_t codepoint = 0;
        uint32_t line_start = offset;
        uint32_t line_end;
        uint8_t glyph_width = 0;
        uint16_t rows[FRAMEBUFFER_FONT_HEIGHT];

        while (offset < module->size && text[offset] != '\n') {
            offset++;
        }
        line_end = offset;
        if (line_end > line_start && text[line_end - 1u] == '\r') {
            line_end--;
        }
        if (framebuffer_parse_hex_font_line(text + line_start,
                                            line_end - line_start,
                                            &codepoint,
                                            &glyph_width,
                                            rows)) {
            uint32_t page = codepoint >> 8;

            if (page < FRAMEBUFFER_FONT_INDEX_PAGES && g_framebuffer_font_page_valid[page] == 0u) {
                g_framebuffer_font_page_valid[page] = 1u;
                g_framebuffer_font_page_offsets[page] = line_start;
            }
        }
        if (codepoint <= 0xffu && glyph_width == 8u) {
            uint32_t glyph_offset = codepoint * FRAMEBUFFER_FONT_HEIGHT;

            for (uint32_t row = 0; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
                g_framebuffer_active_font[glyph_offset + row] = (uint8_t)rows[row];
            }
            loaded_any = 1;
        }
        if (offset < module->size && text[offset] == '\n') {
            offset++;
        }
    }

    return loaded_any;
}

static void framebuffer_copy_glyph_rows(uint16_t dst[FRAMEBUFFER_FONT_HEIGHT],
                                        const uint16_t src[FRAMEBUFFER_FONT_HEIGHT]) {
    for (uint32_t row = 0; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
        dst[row] = src[row];
    }
}

static int framebuffer_find_cached_glyph(uint32_t codepoint,
                                         uint8_t *width,
                                         uint16_t rows[FRAMEBUFFER_FONT_HEIGHT]) {
    for (uint32_t i = 0; i < FRAMEBUFFER_GLYPH_CACHE_SIZE; i++) {
        if (g_framebuffer_glyph_cache[i].valid != 0u &&
            g_framebuffer_glyph_cache[i].codepoint == codepoint) {
            *width = g_framebuffer_glyph_cache[i].width;
            framebuffer_copy_glyph_rows(rows, g_framebuffer_glyph_cache[i].rows);
            return 1;
        }
    }
    return 0;
}

static void framebuffer_store_cached_glyph(uint32_t codepoint,
                                           uint8_t width,
                                           const uint16_t rows[FRAMEBUFFER_FONT_HEIGHT]) {
    struct framebuffer_cached_glyph *glyph =
        &g_framebuffer_glyph_cache[g_framebuffer_glyph_cache_next % FRAMEBUFFER_GLYPH_CACHE_SIZE];

    glyph->codepoint = codepoint;
    glyph->valid = 1u;
    glyph->width = width;
    framebuffer_copy_glyph_rows(glyph->rows, rows);
    g_framebuffer_glyph_cache_next++;
}

static int framebuffer_find_module_glyph(uint32_t codepoint,
                                         uint8_t *width,
                                         uint16_t rows[FRAMEBUFFER_FONT_HEIGHT]) {
    uint32_t page = codepoint >> 8;
    uint32_t offset;
    const char *text = g_framebuffer_font_module_text;

    if (text == 0 || g_framebuffer_font_module_size == 0u ||
        page >= FRAMEBUFFER_FONT_INDEX_PAGES || g_framebuffer_font_page_valid[page] == 0u) {
        return 0;
    }

    offset = g_framebuffer_font_page_offsets[page];
    while (offset < g_framebuffer_font_module_size) {
        uint32_t line_start = offset;
        uint32_t line_end;
        uint32_t current_codepoint = 0;
        uint8_t current_width = 0;
        uint16_t current_rows[FRAMEBUFFER_FONT_HEIGHT];

        while (offset < g_framebuffer_font_module_size && text[offset] != '\n') {
            offset++;
        }
        line_end = offset;
        if (line_end > line_start && text[line_end - 1u] == '\r') {
            line_end--;
        }
        if (framebuffer_parse_hex_font_line(text + line_start,
                                            line_end - line_start,
                                            &current_codepoint,
                                            &current_width,
                                            current_rows)) {
            if (current_codepoint == codepoint) {
                *width = current_width;
                framebuffer_copy_glyph_rows(rows, current_rows);
                framebuffer_store_cached_glyph(codepoint, current_width, current_rows);
                return 1;
            }
            if (current_codepoint > codepoint || (current_codepoint >> 8) > page) {
                return 0;
            }
        }
        if (offset < g_framebuffer_font_module_size && text[offset] == '\n') {
            offset++;
        }
    }
    return 0;
}

static void framebuffer_get_glyph(uint32_t codepoint,
                                  uint8_t *width,
                                  uint16_t rows[FRAMEBUFFER_FONT_HEIGHT]) {
    if (codepoint <= 0xffu) {
        *width = 8;
        for (uint32_t row = 0; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
            rows[row] = g_framebuffer_active_font[codepoint * FRAMEBUFFER_FONT_HEIGHT + row];
        }
        return;
    }
    if (framebuffer_find_cached_glyph(codepoint, width, rows)) {
        return;
    }
    if (framebuffer_find_module_glyph(codepoint, width, rows)) {
        return;
    }
    if (codepoint != 0xfffdu && framebuffer_find_module_glyph(0xfffdu, width, rows)) {
        return;
    }

    *width = 8;
    for (uint32_t row = 0; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
        rows[row] = g_framebuffer_active_font['?' * FRAMEBUFFER_FONT_HEIGHT + row];
    }
}

static uint16_t framebuffer_glyph_row_bits(const uint16_t rows[FRAMEBUFFER_FONT_HEIGHT],
                                           uint8_t row,
                                           uint8_t cell_height) {
    uint8_t font_row = row;

    if (cell_height == FRAMEBUFFER_FONT_HEIGHT_SMALL) {
        font_row = (uint8_t)(row * 2u);
    }
    return rows[font_row];
}

static void framebuffer_render_cell(const struct framebuffer_display_state *state,
                                    uint16_t row,
                                    uint16_t col) {
    uint32_t cell;
    uint32_t codepoint;
    uint32_t flags;
    uint8_t color;
    uint8_t fg;
    uint8_t bg;
    uint8_t glyph_width = 8;
    uint8_t right_half = 0;
    uint16_t glyph_rows[FRAMEBUFFER_FONT_HEIGHT];
    uint32_t fg_pixel;
    uint32_t bg_pixel;
    uint32_t x = state->origin_x + (uint32_t)col * FRAMEBUFFER_FONT_WIDTH;
    uint32_t y = state->origin_y + (uint32_t)row * state->cell_height;
    if (row >= state->rows || col >= state->columns) {
        return;
    }

    cell = state->cells[row][col];
    codepoint = cell & HAL_DISPLAY_CELL_CODEPOINT_MASK;
    flags = cell & HAL_DISPLAY_CELL_FLAGS_MASK;
    color = (uint8_t)(cell >> HAL_DISPLAY_CELL_COLOR_SHIFT);
    if ((flags & HAL_DISPLAY_CELL_CONT) != 0u) {
        if (col > 0u) {
            uint32_t prev = state->cells[row][col - 1u];

            if ((prev & HAL_DISPLAY_CELL_WIDE) != 0u) {
                codepoint = prev & HAL_DISPLAY_CELL_CODEPOINT_MASK;
                color = (uint8_t)(prev >> HAL_DISPLAY_CELL_COLOR_SHIFT);
                right_half = 1u;
            } else {
                codepoint = ' ';
            }
        } else {
            codepoint = ' ';
        }
    }
    if (codepoint == 0u) {
        codepoint = ' ';
    }
    fg = color & 0x0fu;
    bg = (color >> 4) & 0x0fu;

    fg_pixel = framebuffer_vga_color_to_pixel(state, fg);
    bg_pixel = framebuffer_vga_color_to_pixel(state, bg);
    framebuffer_fill_rect(state, x, y, FRAMEBUFFER_FONT_WIDTH, state->cell_height, bg_pixel);
    framebuffer_get_glyph(codepoint, &glyph_width, glyph_rows);
    for (uint8_t glyph_row = 0; glyph_row < state->cell_height; glyph_row++) {
        uint16_t row_bits = framebuffer_glyph_row_bits(glyph_rows, glyph_row, state->cell_height);
        uint8_t bits;

        if (glyph_width == 16u) {
            bits = right_half != 0u ? (uint8_t)(row_bits & 0xffu) : (uint8_t)(row_bits >> 8);
        } else {
            bits = right_half != 0u ? 0u : (uint8_t)row_bits;
        }
        for (uint8_t glyph_col = 0; glyph_col < FRAMEBUFFER_FONT_WIDTH; glyph_col++) {
            if ((bits & (uint8_t)(0x80u >> glyph_col)) != 0u) {
                framebuffer_write_pixel(state, x + glyph_col, y + glyph_row, fg_pixel);
            }
        }
    }

    if (state->cursor_enabled != 0 &&
        state->cursor_visible != 0 &&
        row == state->cursor_row &&
        col == state->cursor_col) {
        uint8_t start = state->cursor_start;
        uint8_t end = state->cursor_end;
        uint32_t cursor_y;
        uint32_t cursor_height;

        if (start >= state->cell_height) {
            start = state->cell_height >= FRAMEBUFFER_CURSOR_UNDERLINE_ROWS
                ? (uint8_t)(state->cell_height - FRAMEBUFFER_CURSOR_UNDERLINE_ROWS)
                : 0u;
        }
        if (end >= state->cell_height) {
            end = (uint8_t)(state->cell_height - 1u);
        }
        if (end < start) {
            end = start;
        }
        cursor_y = y + start;
        cursor_height = (uint32_t)(end - start + 1u);

        framebuffer_fill_rect(state,
                              x,
                              cursor_y,
                              FRAMEBUFFER_FONT_WIDTH,
                              cursor_height,
                              fg_pixel);
    }
    framebuffer_mark_dirty((struct framebuffer_display_state *)state,
                           x,
                           y,
                           FRAMEBUFFER_FONT_WIDTH,
                           state->cell_height);
}

static void framebuffer_render_all(struct framebuffer_display_state *state) {
    uint32_t bg_pixel = framebuffer_vga_color_to_pixel(state, 0x00u);

    framebuffer_fill_rect(state, 0, 0, state->width, state->height, bg_pixel);
    for (uint16_t row = 0; row < state->rows; row++) {
        for (uint16_t col = 0; col < state->columns; col++) {
            framebuffer_render_cell(state, row, col);
        }
    }
    framebuffer_mark_dirty(state, 0, 0, state->width, state->height);
    framebuffer_flush_dirty(state);
}

void framebuffer_display_init(const struct bootx_console_info *console) {
    struct framebuffer_display_state *state = &g_framebuffer_display;
    uint8_t cell_height = FRAMEBUFFER_FONT_HEIGHT;
    uint16_t columns;
    uint16_t rows;

    state->active = 0;
    if (console == 0 || console->type != BOOTX_CONSOLE_FRAMEBUFFER ||
        console->framebuffer_addr == 0 || console->width == 0 || console->height == 0 ||
        console->pitch == 0 || (console->framebuffer_bpp != 32u &&
                                console->framebuffer_bpp != 24u &&
                                console->framebuffer_bpp != 8u)) {
        return;
    }

    if (console->height < 25u * FRAMEBUFFER_FONT_HEIGHT ||
        console->width < 80u * FRAMEBUFFER_FONT_WIDTH) {
        cell_height = FRAMEBUFFER_FONT_HEIGHT_SMALL;
    }

    columns = (uint16_t)(console->width / FRAMEBUFFER_FONT_WIDTH);
    rows = (uint16_t)(console->height / cell_height);
    if (columns > FRAMEBUFFER_TEXT_MAX_COLUMNS) {
        columns = FRAMEBUFFER_TEXT_MAX_COLUMNS;
    }
    if (rows > FRAMEBUFFER_TEXT_MAX_ROWS) {
        rows = FRAMEBUFFER_TEXT_MAX_ROWS;
    }
    if (columns == 0u || rows == 0u) {
        return;
    }

    state->front_base = (volatile uint8_t *)(uintptr_t)console->framebuffer_addr;
    state->base = state->front_base;
    state->backbuffer_enabled = 0u;
    state->width = console->width;
    state->height = console->height;
    state->pitch = console->pitch;
    state->bpp = console->framebuffer_bpp;
    state->red_mask_size = console->red_mask_size;
    state->red_mask_shift = console->red_mask_shift;
    state->green_mask_size = console->green_mask_size;
    state->green_mask_shift = console->green_mask_shift;
    state->blue_mask_size = console->blue_mask_size;
    state->blue_mask_shift = console->blue_mask_shift;
    state->columns = columns;
    state->rows = rows;
    state->cell_height = cell_height;
    state->origin_x = (uint16_t)((console->width - (uint32_t)columns * FRAMEBUFFER_FONT_WIDTH) / 2u);
    state->origin_y = (uint16_t)((console->height - (uint32_t)rows * cell_height) / 2u);
    state->cursor_enabled = FRAMEBUFFER_CURSOR_DISABLED;
    state->cursor_visible = 1u;
    state->cursor_start = state->cell_height >= FRAMEBUFFER_CURSOR_UNDERLINE_ROWS
        ? (uint8_t)(state->cell_height - FRAMEBUFFER_CURSOR_UNDERLINE_ROWS)
        : 0u;
    state->cursor_end = (uint8_t)(state->cell_height - 1u);
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->cursor_blink_ticks = 0u;
    state->dirty_valid = 0u;
    state->mouse_cursor_enabled = 0u;
    state->mouse_cursor_visible = 0u;
    state->mouse_cursor_initialized = 0u;
    state->mouse_cursor_x = 0;
    state->mouse_cursor_y = 0;
    state->mouse_cursor_saved_x = 0u;
    state->mouse_cursor_saved_y = 0u;
    state->mouse_cursor_saved_width = 0u;
    state->mouse_cursor_saved_height = 0u;

    for (uint16_t row = 0; row < state->rows; row++) {
        for (uint16_t col = 0; col < state->columns; col++) {
            state->cells[row][col] = framebuffer_blank_cell(0x07u);
        }
    }
    state->active = 1;
    framebuffer_render_all(state);
}

int framebuffer_display_enable_backbuffer(void) {
    struct framebuffer_display_state *state = &g_framebuffer_display;
    uint64_t bytes;
    uint32_t pages;
    uint64_t phys;
    volatile uint8_t *back;

    if (!state->active || state->backbuffer_enabled) {
        return state->active != 0;
    }

    bytes = (uint64_t)state->pitch * state->height;
    pages = (uint32_t)((bytes + 4095u) / 4096u);
    if (bytes == 0u || pages == 0u || bytes > 0xffffffffull) {
        return 0;
    }

    phys = pmm_alloc_contiguous(pages);
    if (phys == 0u) {
        return 0;
    }
    back = (volatile uint8_t *)hal_phys_direct_map(phys);
    if (back == 0) {
        for (uint32_t i = 0; i < pages; i++) {
            (void)pmm_free_page(phys + (uint64_t)i * 4096u);
        }
        return 0;
    }

    for (uint64_t i = 0; i < bytes; i++) {
        back[i] = state->front_base[i];
    }
    state->base = back;
    state->backbuffer_enabled = 1u;
    framebuffer_mark_dirty(state, 0, 0, state->width, state->height);
    framebuffer_flush_dirty(state);
    return 1;
}

void framebuffer_display_load_font_from_boot_modules(const struct bootx_boot_info *boot_info) {
    const struct bootx_module *modules;

    framebuffer_font_reset_to_builtin();
    if (boot_info == 0 || boot_info->module_count == 0 || boot_info->modules == 0) {
        return;
    }

    modules = (const struct bootx_module *)(uintptr_t)boot_info->modules;
    for (uint32_t i = 0; i < boot_info->module_count; i++) {
        if (framebuffer_module_name_eq(modules[i].name, "FONT.HEX") &&
            framebuffer_try_load_hex_font_module(&modules[i])) {
            return;
        }
    }
}

int framebuffer_display_active(void) {
    return g_framebuffer_display.active;
}

uint32_t framebuffer_device_size(void) {
    uint64_t size;

    if (!g_framebuffer_display.active) {
        return 0;
    }
    size = (uint64_t)g_framebuffer_display.pitch * g_framebuffer_display.height;
    return size > 0xffffffffull ? 0xffffffffu : (uint32_t)size;
}

int64_t framebuffer_device_read(uint32_t *offset_io, void *buffer, uint32_t size) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t fb_size;
    uint32_t copied;

    if (!g_framebuffer_display.active || offset_io == 0 || buffer == 0) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }
    fb_size = framebuffer_device_size();
    if (*offset_io >= fb_size) {
        return 0;
    }
    copied = size;
    if (copied > fb_size - *offset_io) {
        copied = fb_size - *offset_io;
    }
    for (uint32_t i = 0; i < copied; i++) {
        out[i] = g_framebuffer_display.front_base[*offset_io + i];
    }
    *offset_io += copied;
    return (int64_t)copied;
}

int64_t framebuffer_device_write(uint32_t *offset_io, const void *buffer, uint32_t size) {
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t fb_size;
    uint32_t copied;
    int redraw_mouse;

    if (!g_framebuffer_display.active || offset_io == 0 || buffer == 0) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }
    fb_size = framebuffer_device_size();
    if (*offset_io >= fb_size) {
        return 0;
    }
    copied = size;
    if (copied > fb_size - *offset_io) {
        copied = fb_size - *offset_io;
    }
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 0,
                                                                 0,
                                                                 g_framebuffer_display.width,
                                                                 g_framebuffer_display.height);
    for (uint32_t i = 0; i < copied; i++) {
        g_framebuffer_display.front_base[*offset_io + i] = in[i];
        if (g_framebuffer_display.backbuffer_enabled) {
            g_framebuffer_display.base[*offset_io + i] = in[i];
        }
    }
    framebuffer_mark_dirty(&g_framebuffer_display, 0, 0, g_framebuffer_display.width, g_framebuffer_display.height);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
    *offset_io += copied;
    return (int64_t)copied;
}

uint32_t framebuffer_display_read_cell(uint16_t row, uint16_t col) {
    if (!g_framebuffer_display.active ||
        row >= g_framebuffer_display.rows ||
        col >= g_framebuffer_display.columns) {
        return 0;
    }
    return g_framebuffer_display.cells[row][col];
}

void framebuffer_display_write_cell(uint16_t row, uint16_t col, uint32_t value) {
    uint32_t x;
    uint32_t y;
    int redraw_mouse;

    if (!g_framebuffer_display.active ||
        row >= g_framebuffer_display.rows ||
        col >= g_framebuffer_display.columns) {
        return;
    }
    x = g_framebuffer_display.origin_x + (uint32_t)col * FRAMEBUFFER_FONT_WIDTH;
    y = g_framebuffer_display.origin_y + (uint32_t)row * g_framebuffer_display.cell_height;
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 x,
                                                                 y,
                                                                 FRAMEBUFFER_FONT_WIDTH,
                                                                 g_framebuffer_display.cell_height);
    g_framebuffer_display.cells[row][col] = value;
    framebuffer_render_cell(&g_framebuffer_display, row, col);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
}

void framebuffer_display_clear_row(uint16_t row, uint8_t color) {
    uint32_t value = framebuffer_blank_cell(color);
    uint32_t bg_pixel;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    int redraw_mouse;

    if (!g_framebuffer_display.active || row >= g_framebuffer_display.rows) {
        return;
    }
    x = g_framebuffer_display.origin_x;
    y = g_framebuffer_display.origin_y + (uint32_t)row * g_framebuffer_display.cell_height;
    width = (uint32_t)g_framebuffer_display.columns * FRAMEBUFFER_FONT_WIDTH;
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 x,
                                                                 y,
                                                                 width,
                                                                 g_framebuffer_display.cell_height);
    for (uint16_t col = 0; col < g_framebuffer_display.columns; col++) {
        g_framebuffer_display.cells[row][col] = value;
    }
    bg_pixel = framebuffer_vga_color_to_pixel(&g_framebuffer_display, (uint8_t)(color >> 4));
    framebuffer_fill_rect(&g_framebuffer_display,
                          x,
                          y,
                          width,
                          g_framebuffer_display.cell_height,
                          bg_pixel);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
}

void framebuffer_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch) {
    framebuffer_display_write_cell(row, col, framebuffer_pack_cell((uint8_t)ch, color, 0));
}

void framebuffer_display_enable_cursor(uint8_t start, uint8_t end) {
    uint32_t x;
    uint32_t y;
    int redraw_mouse;

    if (!g_framebuffer_display.active) {
        return;
    }
    if (start >= g_framebuffer_display.cell_height) {
        start = g_framebuffer_display.cell_height >= FRAMEBUFFER_CURSOR_UNDERLINE_ROWS
            ? (uint8_t)(g_framebuffer_display.cell_height - FRAMEBUFFER_CURSOR_UNDERLINE_ROWS)
            : 0u;
    }
    if (end >= g_framebuffer_display.cell_height) {
        end = (uint8_t)(g_framebuffer_display.cell_height - 1u);
    }
    if (end < start) {
        end = start;
    }
    g_framebuffer_display.cursor_start = start;
    g_framebuffer_display.cursor_end = end;
    g_framebuffer_display.cursor_enabled = FRAMEBUFFER_CURSOR_ENABLED;
    g_framebuffer_display.cursor_visible = 1u;
    x = g_framebuffer_display.origin_x + (uint32_t)g_framebuffer_display.cursor_col * FRAMEBUFFER_FONT_WIDTH;
    y = g_framebuffer_display.origin_y + (uint32_t)g_framebuffer_display.cursor_row * g_framebuffer_display.cell_height;
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 x,
                                                                 y,
                                                                 FRAMEBUFFER_FONT_WIDTH,
                                                                 g_framebuffer_display.cell_height);
    framebuffer_render_cell(&g_framebuffer_display,
                            g_framebuffer_display.cursor_row,
                            g_framebuffer_display.cursor_col);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
}

void framebuffer_display_set_cursor(uint16_t row, uint16_t col) {
    uint16_t old_row;
    uint16_t old_col;
    uint32_t old_x;
    uint32_t old_y;
    uint32_t new_x;
    uint32_t new_y;
    int redraw_mouse_old;
    int redraw_mouse_new;

    if (!g_framebuffer_display.active || g_framebuffer_display.rows == 0u ||
        g_framebuffer_display.columns == 0u) {
        return;
    }

    old_row = g_framebuffer_display.cursor_row;
    old_col = g_framebuffer_display.cursor_col;
    if (row >= g_framebuffer_display.rows) {
        row = (uint16_t)(g_framebuffer_display.rows - 1u);
    }
    if (col >= g_framebuffer_display.columns) {
        col = (uint16_t)(g_framebuffer_display.columns - 1u);
    }
    g_framebuffer_display.cursor_row = row;
    g_framebuffer_display.cursor_col = col;
    if (g_framebuffer_display.cursor_enabled != 0u) {
        old_x = g_framebuffer_display.origin_x + (uint32_t)old_col * FRAMEBUFFER_FONT_WIDTH;
        old_y = g_framebuffer_display.origin_y + (uint32_t)old_row * g_framebuffer_display.cell_height;
        new_x = g_framebuffer_display.origin_x + (uint32_t)col * FRAMEBUFFER_FONT_WIDTH;
        new_y = g_framebuffer_display.origin_y + (uint32_t)row * g_framebuffer_display.cell_height;
        redraw_mouse_old = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                         old_x,
                                                                         old_y,
                                                                         FRAMEBUFFER_FONT_WIDTH,
                                                                         g_framebuffer_display.cell_height);
        redraw_mouse_new = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                         new_x,
                                                                         new_y,
                                                                         FRAMEBUFFER_FONT_WIDTH,
                                                                         g_framebuffer_display.cell_height);
        framebuffer_render_cell(&g_framebuffer_display, old_row, old_col);
        framebuffer_render_cell(&g_framebuffer_display, row, col);
        framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse_old || redraw_mouse_new);
    }
}

void framebuffer_display_tick(uint32_t ticks) {
    uint8_t visible;
    uint32_t x;
    uint32_t y;
    int redraw_mouse;

    if (!g_framebuffer_display.active || g_framebuffer_display.cursor_enabled == 0u) {
        return;
    }
    visible = ((ticks / FRAMEBUFFER_CURSOR_BLINK_TICKS) & 1u) == 0u ? 1u : 0u;
    if (visible == g_framebuffer_display.cursor_visible &&
        g_framebuffer_display.cursor_blink_ticks == ticks) {
        return;
    }
    g_framebuffer_display.cursor_blink_ticks = ticks;
    if (visible == g_framebuffer_display.cursor_visible) {
        return;
    }
    g_framebuffer_display.cursor_visible = visible;
    x = g_framebuffer_display.origin_x + (uint32_t)g_framebuffer_display.cursor_col * FRAMEBUFFER_FONT_WIDTH;
    y = g_framebuffer_display.origin_y + (uint32_t)g_framebuffer_display.cursor_row * g_framebuffer_display.cell_height;
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 x,
                                                                 y,
                                                                 FRAMEBUFFER_FONT_WIDTH,
                                                                 g_framebuffer_display.cell_height);
    framebuffer_render_cell(&g_framebuffer_display,
                            g_framebuffer_display.cursor_row,
                            g_framebuffer_display.cursor_col);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
}

uint16_t framebuffer_display_columns(void) {
    if (!g_framebuffer_display.active) {
        return FRAMEBUFFER_TEXT_MAX_COLUMNS;
    }
    return g_framebuffer_display.columns;
}

uint16_t framebuffer_display_rows(void) {
    if (!g_framebuffer_display.active) {
        return FRAMEBUFFER_TEXT_MAX_ROWS;
    }
    return g_framebuffer_display.rows;
}

uint32_t framebuffer_display_cell_height(void) {
    return g_framebuffer_display.cell_height;
}

static void framebuffer_bitblt(const struct framebuffer_display_state *state,
                               uint32_t src_x,
                               uint32_t src_y,
                               uint32_t width,
                               uint32_t height,
                               uint32_t dst_x,
                               uint32_t dst_y) {
    uint32_t bytes_per_pixel;
    uint32_t row_bytes;

    if (state == 0 || state->base == 0) {
        return;
    }

    if (width == 0 || height == 0) {
        return;
    }

    if (src_x >= state->width || src_y >= state->height ||
        dst_x >= state->width || dst_y >= state->height) {
        return;
    }

    if (src_x + width > state->width) {
        width = state->width - src_x;
    }

    if (dst_x + width > state->width) {
        width = state->width - dst_x;
    }

    if (src_y + height > state->height) {
        height = state->height - src_y;
    }

    if (dst_y + height > state->height) {
        height = state->height - dst_y;
    }

    bytes_per_pixel = state->bpp / 8u;
    row_bytes = width * bytes_per_pixel;

    /*
        overlap-safe copy

        아래 방향 복사일 경우
        아래에서 위로 복사해야 덮어쓰기 안됨
    */
    if (dst_y > src_y) {
        for (int32_t row = (int32_t)height - 1; row >= 0; row--) {
            volatile uint8_t *src;
            volatile uint8_t *dst;

            src = state->base +
                  (uint64_t)(src_y + row) * state->pitch +
                  (uint64_t)src_x * bytes_per_pixel;

            dst = state->base +
                  (uint64_t)(dst_y + row) * state->pitch +
                  (uint64_t)dst_x * bytes_per_pixel;

            memmove((void *)dst, (const void *)src, row_bytes);
        }
    } else {
        for (uint32_t row = 0; row < height; row++) {
            volatile uint8_t *src;
            volatile uint8_t *dst;

            src = state->base +
                  (uint64_t)(src_y + row) * state->pitch +
                  (uint64_t)src_x * bytes_per_pixel;

            dst = state->base +
                  (uint64_t)(dst_y + row) * state->pitch +
                  (uint64_t)dst_x * bytes_per_pixel;

            memmove((void *)dst, (const void *)src, row_bytes);
        }
    }
    framebuffer_mark_dirty((struct framebuffer_display_state *)state, dst_x, dst_y, width, height);
}

void framebuffer_display_bitblt(uint32_t src_x,
                                uint32_t src_y,
                                uint32_t width,
                                uint32_t height,
                                uint32_t dst_x,
                                uint32_t dst_y) {
    int redraw_mouse_src;
    int redraw_mouse_dst;

    if (!g_framebuffer_display.active) {
        return;
    }
    redraw_mouse_src = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                     src_x,
                                                                     src_y,
                                                                     width,
                                                                     height);
    redraw_mouse_dst = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                     dst_x,
                                                                     dst_y,
                                                                     width,
                                                                     height);
    framebuffer_bitblt(
        &g_framebuffer_display,
        src_x,
        src_y,
        width,
        height,
        dst_x,
        dst_y
    );
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse_src || redraw_mouse_dst);
}

void framebuffer_display_scroll_rows(uint16_t top_row, uint16_t bottom_row, uint8_t clear_color) {
    uint32_t row_height;
    uint32_t x;
    uint32_t src_y;
    uint32_t dst_y;
    uint32_t width;
    uint32_t height;
    uint32_t bg_pixel;
    int redraw_mouse;

    if (!g_framebuffer_display.active || top_row >= bottom_row ||
        bottom_row >= g_framebuffer_display.rows) {
        return;
    }

    row_height = g_framebuffer_display.cell_height;
    x = g_framebuffer_display.origin_x;
    src_y = g_framebuffer_display.origin_y + (uint32_t)(top_row + 1u) * row_height;
    dst_y = g_framebuffer_display.origin_y + (uint32_t)top_row * row_height;
    width = (uint32_t)g_framebuffer_display.columns * FRAMEBUFFER_FONT_WIDTH;
    height = (uint32_t)(bottom_row - top_row) * row_height;
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 x,
                                                                 dst_y,
                                                                 width,
                                                                 height + row_height);
    framebuffer_bitblt(&g_framebuffer_display, x, src_y, width, height, x, dst_y);

    bg_pixel = framebuffer_vga_color_to_pixel(&g_framebuffer_display, (uint8_t)(clear_color >> 4));
    framebuffer_fill_rect(&g_framebuffer_display,
                          x,
                          g_framebuffer_display.origin_y + (uint32_t)bottom_row * row_height,
                          width,
                          row_height,
                          bg_pixel);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
}

void framebuffer_display_blit_surface(const struct surface *surface,
                                      uint32_t src_x,
                                      uint32_t src_y,
                                      uint32_t width,
                                      uint32_t height,
                                      int32_t dst_x,
                                      int32_t dst_y) {
    uint32_t dst_width;
    uint32_t dst_height;
    int redraw_mouse;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0 ||
        surface == 0 || surface->pixels == 0 ||
        surface->format != SURFACE_FORMAT_XRGB8888 ||
        width == 0u || height == 0u ||
        src_x >= surface->width || src_y >= surface->height ||
        dst_x >= (int32_t)g_framebuffer_display.width ||
        dst_y >= (int32_t)g_framebuffer_display.height) {
        return;
    }
    if (dst_x < 0) {
        uint32_t crop = (uint32_t)(-dst_x);

        if (crop >= width) {
            return;
        }
        width -= crop;
        src_x += crop;
        dst_x = 0;
    }
    if (dst_y < 0) {
        uint32_t crop = (uint32_t)(-dst_y);

        if (crop >= height) {
            return;
        }
        height -= crop;
        src_y += crop;
        dst_y = 0;
    }
    if (src_x + width > surface->width) {
        width = surface->width - src_x;
    }
    if (src_y + height > surface->height) {
        height = surface->height - src_y;
    }
    if ((uint32_t)dst_x + width > g_framebuffer_display.width) {
        width = g_framebuffer_display.width - (uint32_t)dst_x;
    }
    if ((uint32_t)dst_y + height > g_framebuffer_display.height) {
        height = g_framebuffer_display.height - (uint32_t)dst_y;
    }
    if (width == 0u || height == 0u) {
        return;
    }

    dst_width = width;
    dst_height = height;
    redraw_mouse = framebuffer_begin_mouse_cursor_covered_update(&g_framebuffer_display,
                                                                 (uint32_t)dst_x,
                                                                 (uint32_t)dst_y,
                                                                 dst_width,
                                                                 dst_height);
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *src_row =
            (const uint32_t *)((const uint8_t *)surface->pixels + (src_y + y) * surface->pitch + src_x * 4u);

        for (uint32_t x = 0; x < width; x++) {
            framebuffer_write_pixel(&g_framebuffer_display,
                                    (uint32_t)dst_x + x,
                                    (uint32_t)dst_y + y,
                                    framebuffer_xrgb_to_pixel(&g_framebuffer_display, src_row[x]));
        }
    }
    framebuffer_mark_dirty(&g_framebuffer_display, (uint32_t)dst_x, (uint32_t)dst_y, dst_width, dst_height);
    framebuffer_end_mouse_cursor_covered_update(&g_framebuffer_display, redraw_mouse);
}

void framebuffer_display_draw_pixel(int32_t x, int32_t y, uint32_t rgb) {
    uint32_t color;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0) {
        return;
    }
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    framebuffer_write_pixel_i32(&g_framebuffer_display, x, y, color);
    framebuffer_mark_dirty_i32(&g_framebuffer_display, x, y, x, y);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb) {
    uint32_t color;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0) {
        return;
    }
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    framebuffer_draw_line_raw(&g_framebuffer_display, x0, y0, x1, y1, color);
    min_x = framebuffer_min_i32(x0, x1);
    min_y = framebuffer_min_i32(y0, y1);
    max_x = framebuffer_max_i32(x0, x1);
    max_y = framebuffer_max_i32(y0, y1);
    framebuffer_mark_dirty_i32(&g_framebuffer_display, min_x, min_y, max_x, max_y);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_fill_rect_rgb(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    uint32_t color;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0 ||
        width == 0u || height == 0u) {
        return;
    }
    if (x >= (int32_t)g_framebuffer_display.width || y >= (int32_t)g_framebuffer_display.height) {
        return;
    }
    if (x < 0) {
        uint32_t crop = (uint32_t)(-x);

        if (crop >= width) {
            return;
        }
        width -= crop;
        x = 0;
    }
    if (y < 0) {
        uint32_t crop = (uint32_t)(-y);

        if (crop >= height) {
            return;
        }
        height -= crop;
        y = 0;
    }
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    framebuffer_fill_rect(&g_framebuffer_display, (uint32_t)x, (uint32_t)y, width, height, color);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_draw_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    int32_t x1;
    int32_t y1;
    uint32_t color;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0 ||
        width == 0u || height == 0u) {
        return;
    }
    x1 = x + (int32_t)width - 1;
    y1 = y + (int32_t)height - 1;
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    framebuffer_draw_line_raw(&g_framebuffer_display, x, y, x1, y, color);
    if (height > 1u) {
        framebuffer_draw_line_raw(&g_framebuffer_display, x, y1, x1, y1, color);
    }
    if (height > 2u) {
        framebuffer_draw_line_raw(&g_framebuffer_display, x, y + 1, x, y1 - 1, color);
        if (width > 1u) {
            framebuffer_draw_line_raw(&g_framebuffer_display, x1, y + 1, x1, y1 - 1, color);
        }
    } else if (width > 1u && height == 1u) {
        framebuffer_draw_line_raw(&g_framebuffer_display, x1, y, x1, y, color);
    }
    framebuffer_mark_dirty_i32(&g_framebuffer_display, x, y, x1, y1);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_draw_triangle(int32_t x0,
                                       int32_t y0,
                                       int32_t x1,
                                       int32_t y1,
                                       int32_t x2,
                                       int32_t y2,
                                       uint32_t rgb) {
    uint32_t color;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0) {
        return;
    }
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    framebuffer_draw_line_raw(&g_framebuffer_display, x0, y0, x1, y1, color);
    framebuffer_draw_line_raw(&g_framebuffer_display, x1, y1, x2, y2, color);
    framebuffer_draw_line_raw(&g_framebuffer_display, x2, y2, x0, y0, color);
    min_x = framebuffer_min_i32(framebuffer_min_i32(x0, x1), x2);
    min_y = framebuffer_min_i32(framebuffer_min_i32(y0, y1), y2);
    max_x = framebuffer_max_i32(framebuffer_max_i32(x0, x1), x2);
    max_y = framebuffer_max_i32(framebuffer_max_i32(y0, y1), y2);
    framebuffer_mark_dirty_i32(&g_framebuffer_display, min_x, min_y, max_x, max_y);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_fill_triangle(int32_t x0,
                                       int32_t y0,
                                       int32_t x1,
                                       int32_t y1,
                                       int32_t x2,
                                       int32_t y2,
                                       uint32_t rgb) {
    uint32_t color;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    int64_t area;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0) {
        return;
    }
    min_x = framebuffer_min_i32(framebuffer_min_i32(x0, x1), x2);
    min_y = framebuffer_min_i32(framebuffer_min_i32(y0, y1), y2);
    max_x = framebuffer_max_i32(framebuffer_max_i32(x0, x1), x2);
    max_y = framebuffer_max_i32(framebuffer_max_i32(y0, y1), y2);
    if (max_x < 0 || max_y < 0 ||
        min_x >= (int32_t)g_framebuffer_display.width ||
        min_y >= (int32_t)g_framebuffer_display.height) {
        return;
    }
    if (min_x < 0) {
        min_x = 0;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_x >= (int32_t)g_framebuffer_display.width) {
        max_x = (int32_t)g_framebuffer_display.width - 1;
    }
    if (max_y >= (int32_t)g_framebuffer_display.height) {
        max_y = (int32_t)g_framebuffer_display.height - 1;
    }
    area = framebuffer_triangle_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
        framebuffer_draw_line_raw(&g_framebuffer_display, x0, y0, x1, y1, color);
        framebuffer_draw_line_raw(&g_framebuffer_display, x1, y1, x2, y2, color);
        framebuffer_draw_line_raw(&g_framebuffer_display, x2, y2, x0, y0, color);
        framebuffer_mark_dirty_i32(&g_framebuffer_display, min_x, min_y, max_x, max_y);
        framebuffer_flush_dirty(&g_framebuffer_display);
        return;
    }
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    for (int32_t y = min_y; y <= max_y; y++) {
        for (int32_t x = min_x; x <= max_x; x++) {
            int64_t w0 = framebuffer_triangle_edge(x1, y1, x2, y2, x, y);
            int64_t w1 = framebuffer_triangle_edge(x2, y2, x0, y0, x, y);
            int64_t w2 = framebuffer_triangle_edge(x0, y0, x1, y1, x, y);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                framebuffer_write_pixel_i32(&g_framebuffer_display, x, y, color);
            }
        }
    }
    framebuffer_mark_dirty_i32(&g_framebuffer_display, min_x, min_y, max_x, max_y);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_draw_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    int32_t x;
    int32_t y;
    int32_t err;
    uint32_t color;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0) {
        return;
    }
    x = (int32_t)radius;
    y = 0;
    err = 0;
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    while (x >= y) {
        y++;
        framebuffer_draw_circle_points(&g_framebuffer_display, cx, cy, x, y - 1, color);
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
    framebuffer_mark_dirty_i32(&g_framebuffer_display,
                               cx - (int32_t)radius,
                               cy - (int32_t)radius,
                               cx + (int32_t)radius,
                               cy + (int32_t)radius);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    int32_t r = (int32_t)radius;
    int32_t rr = r * r;
    uint32_t color;

    if (!g_framebuffer_display.active || g_framebuffer_display.base == 0) {
        return;
    }
    color = framebuffer_rgb_to_pixel(&g_framebuffer_display, rgb);
    for (int32_t y = -r; y <= r; y++) {
        for (int32_t x = -r; x <= r; x++) {
            if (x * x + y * y <= rr) {
                framebuffer_write_pixel_i32(&g_framebuffer_display, cx + x, cy + y, color);
            }
        }
    }
    framebuffer_mark_dirty_i32(&g_framebuffer_display, cx - r, cy - r, cx + r, cy + r);
    framebuffer_flush_dirty(&g_framebuffer_display);
}

void framebuffer_display_set_mouse_cursor_enabled(int enabled) {
    struct framebuffer_display_state *state = &g_framebuffer_display;

    if (!state->active || state->base == 0) {
        return;
    }
    if (enabled) {
        if (!state->mouse_cursor_initialized) {
            state->mouse_cursor_x = (int32_t)(state->width / 2u);
            state->mouse_cursor_y = (int32_t)(state->height / 2u);
            state->mouse_cursor_initialized = 1u;
        }
        state->mouse_cursor_enabled = 1u;
        framebuffer_draw_mouse_cursor(state);
        framebuffer_flush_dirty(state);
        return;
    }
    framebuffer_restore_mouse_cursor(state);
    state->mouse_cursor_enabled = 0u;
    framebuffer_flush_dirty(state);
}

void framebuffer_display_move_mouse_cursor(int32_t dx, int32_t dy) {
    struct framebuffer_display_state *state = &g_framebuffer_display;
    int32_t x;
    int32_t y;
    int32_t max_x;
    int32_t max_y;

    if (!state->active || state->base == 0 || !state->mouse_cursor_enabled) {
        return;
    }
    if (!state->mouse_cursor_initialized) {
        state->mouse_cursor_x = (int32_t)(state->width / 2u);
        state->mouse_cursor_y = (int32_t)(state->height / 2u);
        state->mouse_cursor_initialized = 1u;
    }
    framebuffer_restore_mouse_cursor(state);
    x = state->mouse_cursor_x + dx;
    y = state->mouse_cursor_y + dy;
    max_x = state->width == 0u ? 0 : (int32_t)state->width - 1;
    max_y = state->height == 0u ? 0 : (int32_t)state->height - 1;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x > max_x) {
        x = max_x;
    }
    if (y > max_y) {
        y = max_y;
    }
    state->mouse_cursor_x = x;
    state->mouse_cursor_y = y;
    framebuffer_draw_mouse_cursor(state);
    framebuffer_flush_dirty(state);
}

int framebuffer_display_mouse_cursor_cell(uint16_t *row_out, uint16_t *col_out) {
    struct framebuffer_display_state *state = &g_framebuffer_display;
    uint32_t x;
    uint32_t y;
    uint32_t col;
    uint32_t row;

    if (row_out == 0 || col_out == 0 || !state->active ||
        !state->mouse_cursor_initialized ||
        state->mouse_cursor_x < (int32_t)state->origin_x ||
        state->mouse_cursor_y < (int32_t)state->origin_y) {
        return 0;
    }
    x = (uint32_t)state->mouse_cursor_x - state->origin_x;
    y = (uint32_t)state->mouse_cursor_y - state->origin_y;
    col = x / FRAMEBUFFER_FONT_WIDTH;
    row = y / state->cell_height;
    if (col >= state->columns || row >= state->rows) {
        return 0;
    }
    *row_out = (uint16_t)row;
    *col_out = (uint16_t)col;
    return 1;
}
