#include "drivers/video/framebuffer.h"
#include "drivers/video/framebuffer_font.h"
#include <stddef.h>
#include "lib/string.h"

enum {
    FRAMEBUFFER_CURSOR_DISABLED = 0,
    FRAMEBUFFER_CURSOR_ENABLED = 1,
    FRAMEBUFFER_FONT_WIDTH = 8,
    FRAMEBUFFER_FONT_HEIGHT = 16,
    FRAMEBUFFER_FONT_HEIGHT_SMALL = 8,
    FRAMEBUFFER_CURSOR_UNDERLINE_ROWS = 2,
    FRAMEBUFFER_CURSOR_BLINK_TICKS = 50
};

struct framebuffer_display_state {
    int active;
    volatile uint8_t *base;
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
    uint16_t cursor_row;
    uint16_t cursor_col;
    uint32_t cursor_blink_ticks;
    uint16_t cells[FRAMEBUFFER_TEXT_MAX_ROWS][FRAMEBUFFER_TEXT_MAX_COLUMNS];
};

static struct framebuffer_display_state g_framebuffer_display;
static uint8_t g_framebuffer_active_font[256 * FRAMEBUFFER_FONT_HEIGHT];

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

static void framebuffer_fill_rect(const struct framebuffer_display_state *state,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t color) {
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            framebuffer_write_pixel(state, x + col, y + row, color);
        }
    }
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

static int framebuffer_parse_hex_codepoint(const char *text, uint32_t *value) {
    uint32_t result = 0;

    if (text == 0 || value == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < 4; i++) {
        int digit = framebuffer_hex_digit_value(text[i]);
        if (digit < 0) {
            return 0;
        }
        result = (result << 4) | (uint32_t)digit;
    }
    *value = result;
    return 1;
}

static int framebuffer_module_name_eq(const char name[12], const char *target) {
    char padded[12];
    uint32_t i = 0;
    uint32_t out = 0;

    if (target == 0) {
        return 0;
    }
    for (i = 0; i < sizeof(padded); i++) {
        padded[i] = ' ';
    }
    while (target[i] != '\0' && out < 11u) {
        if (target[i] == '.') {
            out = 8u;
            i++;
            continue;
        }
        padded[out++] = target[i++];
    }
    for (i = 0; i < sizeof(padded); i++) {
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
}

static int framebuffer_try_load_hex_font_module(const struct bootx_module *module) {
    const char *text;
    uint32_t offset = 0;
    int loaded_any = 0;

    if (module == 0 || module->address == 0 || module->size == 0) {
        return 0;
    }

    text = (const char *)(uintptr_t)module->address;
    while (offset + 37u <= module->size) {
        uint32_t codepoint = 0;
        uint32_t line_start = offset;

        while (offset < module->size && text[offset] != '\n') {
            offset++;
        }
        if (offset - line_start >= 37u &&
            framebuffer_parse_hex_codepoint(text + line_start, &codepoint) &&
            text[line_start + 4u] == ':' &&
            codepoint <= 0xffu) {
            uint32_t glyph_offset = codepoint * FRAMEBUFFER_FONT_HEIGHT;
            int valid = 1;

            for (uint32_t row = 0; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
                uint8_t value = 0;

                if (!framebuffer_parse_hex_byte(text + line_start + 5u + row * 2u, &value)) {
                    valid = 0;
                    break;
                }
                g_framebuffer_active_font[glyph_offset + row] = value;
            }
            if (valid) {
                loaded_any = 1;
            }
        }
        if (offset < module->size && text[offset] == '\n') {
            offset++;
        }
    }

    return loaded_any;
}

static uint8_t framebuffer_font_row(uint8_t ch, uint8_t row, uint8_t cell_height) {
    uint8_t font_row = row;

    if (cell_height == FRAMEBUFFER_FONT_HEIGHT_SMALL) {
        font_row = (uint8_t)(row * 2u);
    }
    return g_framebuffer_active_font[(uint16_t)ch * FRAMEBUFFER_FONT_HEIGHT + font_row];
}

static void framebuffer_render_cell(const struct framebuffer_display_state *state,
                                    uint16_t row,
                                    uint16_t col) {
    uint16_t cell;
    uint8_t ch;
    uint8_t color;
    uint8_t fg;
    uint8_t bg;
    uint32_t fg_pixel;
    uint32_t bg_pixel;
    uint32_t x = state->origin_x + (uint32_t)col * FRAMEBUFFER_FONT_WIDTH;
    uint32_t y = state->origin_y + (uint32_t)row * state->cell_height;
    if (row >= state->rows || col >= state->columns) {
        return;
    }

    cell = state->cells[row][col];
    ch = (uint8_t)(cell & 0xffu);
    color = (uint8_t)(cell >> 8);
    fg = color & 0x0fu;
    bg = (color >> 4) & 0x0fu;

    fg_pixel = framebuffer_vga_color_to_pixel(state, fg);
    bg_pixel = framebuffer_vga_color_to_pixel(state, bg);
    framebuffer_fill_rect(state, x, y, FRAMEBUFFER_FONT_WIDTH, state->cell_height, bg_pixel);
    for (uint8_t glyph_row = 0; glyph_row < state->cell_height; glyph_row++) {
        uint8_t bits = framebuffer_font_row(ch, glyph_row, state->cell_height);
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
        uint8_t underline_rows = state->cell_height >= FRAMEBUFFER_CURSOR_UNDERLINE_ROWS
            ? FRAMEBUFFER_CURSOR_UNDERLINE_ROWS
            : 1u;
        uint32_t underline_y = y + state->cell_height - underline_rows;

        framebuffer_fill_rect(state,
                              x,
                              underline_y,
                              FRAMEBUFFER_FONT_WIDTH,
                              underline_rows,
                              fg_pixel);
    }
}

static void framebuffer_render_all(const struct framebuffer_display_state *state) {
    uint32_t bg_pixel = framebuffer_vga_color_to_pixel(state, 0x00u);

    framebuffer_fill_rect(state, 0, 0, state->width, state->height, bg_pixel);
    for (uint16_t row = 0; row < state->rows; row++) {
        for (uint16_t col = 0; col < state->columns; col++) {
            framebuffer_render_cell(state, row, col);
        }
    }
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

    state->base = (volatile uint8_t *)(uintptr_t)console->framebuffer_addr;
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
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->cursor_blink_ticks = 0u;

    for (uint16_t row = 0; row < state->rows; row++) {
        for (uint16_t col = 0; col < state->columns; col++) {
            state->cells[row][col] = 0x0720u;
        }
    }
    state->active = 1;
    framebuffer_render_all(state);
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

uint16_t framebuffer_display_read_cell(uint16_t row, uint16_t col) {
    if (!g_framebuffer_display.active ||
        row >= g_framebuffer_display.rows ||
        col >= g_framebuffer_display.columns) {
        return 0;
    }
    return g_framebuffer_display.cells[row][col];
}

void framebuffer_display_write_cell(uint16_t row, uint16_t col, uint16_t value) {
    if (!g_framebuffer_display.active ||
        row >= g_framebuffer_display.rows ||
        col >= g_framebuffer_display.columns) {
        return;
    }
    g_framebuffer_display.cells[row][col] = value;
    framebuffer_render_cell(&g_framebuffer_display, row, col);
}

void framebuffer_display_clear_row(uint16_t row, uint8_t color) {
    uint16_t value = (uint16_t)(((uint16_t)color << 8) | (uint8_t)' ');

    if (!g_framebuffer_display.active || row >= g_framebuffer_display.rows) {
        return;
    }
    for (uint16_t col = 0; col < g_framebuffer_display.columns; col++) {
        g_framebuffer_display.cells[row][col] = value;
    }
    for (uint16_t col = 0; col < g_framebuffer_display.columns; col++) {
        framebuffer_render_cell(&g_framebuffer_display, row, col);
    }
}

void framebuffer_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch) {
    framebuffer_display_write_cell(row, col, (uint16_t)(((uint16_t)color << 8) | (uint8_t)ch));
}

void framebuffer_display_enable_cursor(uint8_t start, uint8_t end) {
    (void)start;
    (void)end;
    if (!g_framebuffer_display.active) {
        return;
    }
    g_framebuffer_display.cursor_enabled = FRAMEBUFFER_CURSOR_ENABLED;
    g_framebuffer_display.cursor_visible = 1u;
    framebuffer_render_cell(&g_framebuffer_display,
                            g_framebuffer_display.cursor_row,
                            g_framebuffer_display.cursor_col);
}

void framebuffer_display_set_cursor(uint16_t row, uint16_t col) {
    uint16_t old_row;
    uint16_t old_col;

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
        framebuffer_render_cell(&g_framebuffer_display, old_row, old_col);
        framebuffer_render_cell(&g_framebuffer_display, row, col);
    }
}

void framebuffer_display_tick(uint32_t ticks) {
    uint8_t visible;

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
    framebuffer_render_cell(&g_framebuffer_display,
                            g_framebuffer_display.cursor_row,
                            g_framebuffer_display.cursor_col);
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
