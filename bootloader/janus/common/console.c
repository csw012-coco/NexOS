#include "janus.h"
#include "../../../drivers/video/framebuffer_font.h"

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static uint16_t cursor_row;
static uint16_t cursor_col;
static uint8_t current_color = 0x00;

#define JANUS_FONT_WIDTH 8u
#define JANUS_FONT_HEIGHT 16u

static struct janus_console_info fb_console;
static uint8_t fb_console_enabled;

static const uint8_t vga_palette[16][3] = {
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0xaa},
    {0x00, 0xaa, 0x00},
    {0x00, 0xaa, 0xaa},
    {0xaa, 0x00, 0x00},
    {0xaa, 0x00, 0xaa},
    {0xaa, 0x55, 0x00},
    {0xaa, 0xaa, 0xaa},
    {0x55, 0x55, 0x55},
    {0x55, 0x55, 0xff},
    {0x55, 0xff, 0x55},
    {0x55, 0xff, 0xff},
    {0xff, 0x55, 0x55},
    {0xff, 0x55, 0xff},
    {0xff, 0xff, 0x55},
    {0xff, 0xff, 0xff},
};

static uint32_t mask_from_size(uint8_t size) {
    if (size == 0) {
        return 0;
    }
    if (size >= 32) {
        return 0xFFFFFFFFu;
    }
    return (1u << size) - 1u;
}

static uint32_t expand_color_component(uint8_t value, uint8_t mask_size) {
    uint32_t mask = mask_from_size(mask_size);

    if (mask_size == 0) {
        return 0;
    }
    if (mask_size >= 8) {
        return ((uint32_t)value << (mask_size - 8)) & mask;
    }
    return ((uint32_t)value * mask + 127u) / 255u;
}

static uint32_t fb_make_color(uint8_t index) {
    const uint8_t *rgb = vga_palette[index & 0x0Fu];

    if (fb_console.framebuffer_bpp == 16 &&
        fb_console.red_mask_size == 0 &&
        fb_console.green_mask_size == 0 &&
        fb_console.blue_mask_size == 0) {
        return ((uint32_t)(rgb[0] >> 3) << 11) |
               ((uint32_t)(rgb[1] >> 2) << 5) |
               (uint32_t)(rgb[2] >> 3);
    }

    return (expand_color_component(rgb[0], fb_console.red_mask_size) << fb_console.red_mask_shift) |
           (expand_color_component(rgb[1], fb_console.green_mask_size) << fb_console.green_mask_shift) |
           (expand_color_component(rgb[2], fb_console.blue_mask_size) << fb_console.blue_mask_shift);
}

static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *pixel;

    if (x >= fb_console.width || y >= fb_console.height) {
        return;
    }

    pixel = (uint8_t *)(uintptr_t)fb_console.framebuffer_addr +
            (uint32_t)y * fb_console.pitch +
            (uint32_t)x * ((uint32_t)fb_console.framebuffer_bpp / 8u);

    if (fb_console.framebuffer_bpp == 32) {
        *(uint32_t *)pixel = color;
    } else if (fb_console.framebuffer_bpp == 24) {
        pixel[0] = (uint8_t)color;
        pixel[1] = (uint8_t)(color >> 8);
        pixel[2] = (uint8_t)(color >> 16);
    } else if (fb_console.framebuffer_bpp == 16) {
        *(uint16_t *)pixel = (uint16_t)color;
    }
}

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

static void fb_draw_cell(uint16_t row, uint16_t col, uint8_t color, uint8_t ch) {
    uint32_t x = (uint32_t)col * JANUS_FONT_WIDTH;
    uint32_t y = (uint32_t)row * JANUS_FONT_HEIGHT;
    uint32_t fg = fb_make_color(color & 0x0Fu);
    uint32_t bg = fb_make_color((uint8_t)(color >> 4));
    const uint8_t *glyph = &g_framebuffer_font[(uint32_t)ch * JANUS_FONT_HEIGHT];

    for (uint32_t gy = 0; gy < JANUS_FONT_HEIGHT; gy++) {
        uint8_t bits = glyph[gy];
        for (uint32_t gx = 0; gx < JANUS_FONT_WIDTH; gx++) {
            uint32_t pixel = (bits & (0x80u >> gx)) ? fg : bg;
            fb_put_pixel(x + gx, y + gy, pixel);
        }
    }
}

static void fb_scroll_if_needed(void) {
    uint8_t *base;
    uint32_t scroll_bytes;
    uint32_t visible_rows;

    if (!fb_console_enabled || cursor_row < fb_console.text_rows) {
        return;
    }

    base = (uint8_t *)(uintptr_t)fb_console.framebuffer_addr;
    scroll_bytes = JANUS_FONT_HEIGHT * fb_console.pitch;
    visible_rows = (uint32_t)fb_console.text_rows * JANUS_FONT_HEIGHT;

    for (uint32_t y = 0; y + JANUS_FONT_HEIGHT < visible_rows; y++) {
        memcpy(base + y * fb_console.pitch,
               base + y * fb_console.pitch + scroll_bytes,
               fb_console.pitch);
    }

    fb_fill_rect(0,
                 (uint32_t)(fb_console.text_rows - 1u) * JANUS_FONT_HEIGHT,
                 fb_console.width,
                 JANUS_FONT_HEIGHT,
                 fb_make_color((uint8_t)(current_color >> 4)));
    cursor_row = (uint16_t)(fb_console.text_rows - 1u);
}

static void scroll_if_needed(void) {
    if (fb_console_enabled) {
        fb_scroll_if_needed();
        return;
    }
    if (cursor_row < 25) {
        return;
    }
    for (uint16_t row = 1; row < 25; row++) {
        for (uint16_t col = 0; col < 80; col++) {
            vga[(row - 1) * 80 + col] = vga[row * 80 + col];
        }
    }
    for (uint16_t col = 0; col < 80; col++) {
        vga[24 * 80 + col] = (uint16_t)current_color << 8 | ' ';
    }
    cursor_row = 24;
}

void console_clear(void) {
    if (fb_console_enabled) {
        fb_fill_rect(0, 0, fb_console.width, fb_console.height,
                     fb_make_color((uint8_t)(current_color >> 4)));
        cursor_row = 0;
        cursor_col = 0;
        return;
    }

    for (uint16_t i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)current_color << 8 | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
}

void console_set_color(uint8_t color) {
    current_color = color;
}

void console_set_cursor(uint16_t row, uint16_t col) {
    cursor_row = row;
    cursor_col = col;
}

void console_configure_framebuffer(struct janus_console_info *info) {
    if (info == 0 || info->type != JANUS_CONSOLE_FRAMEBUFFER ||
        info->framebuffer_addr == 0 || info->width < JANUS_FONT_WIDTH ||
        info->height < JANUS_FONT_HEIGHT || info->pitch == 0 ||
        (info->framebuffer_bpp != 16 && info->framebuffer_bpp != 24 &&
         info->framebuffer_bpp != 32)) {
        fb_console_enabled = 0;
        return;
    }

    fb_console = *info;
    current_color = 0x07;
    fb_console.text_columns = (uint16_t)(fb_console.width / JANUS_FONT_WIDTH);
    fb_console.text_rows = (uint16_t)(fb_console.height / JANUS_FONT_HEIGHT);
    fb_console.text_color = current_color;
    *info = fb_console;
    fb_console_enabled = 1;
    cursor_row = 0;
    cursor_col = 0;
}

void console_configure_text(void) {
    fb_console_enabled = 0;
    current_color = 0x07;
    cursor_row = 0;
    cursor_col = 0;
}

void console_putc(char ch) {
    if (ch == '\n') {
        cursor_row++;
        cursor_col = 0;
        scroll_if_needed();
        return;
    }
    if (ch == '\r') {
        cursor_col = 0;
        return;
    }

    if (fb_console_enabled) {
        fb_draw_cell(cursor_row, cursor_col, current_color, (uint8_t)ch);
    } else {
        vga[cursor_row * 80 + cursor_col] = (uint16_t)current_color << 8 | (uint8_t)ch;
    }
    cursor_col++;
    if (cursor_col >= (fb_console_enabled ? fb_console.text_columns : 80)) {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
    }
}

void console_puts(const char *str) {
    while (*str != '\0') {
        console_putc(*str++);
    }
}

void console_write_at(uint16_t row, uint16_t col, uint8_t color, const char *str) {
    uint16_t rows = fb_console_enabled ? fb_console.text_rows : 25;
    uint16_t columns = fb_console_enabled ? fb_console.text_columns : 80;

    while (*str != '\0' && row < rows && col < columns) {
        if (fb_console_enabled) {
            fb_draw_cell(row, col, color, (uint8_t)*str++);
        } else {
            vga[row * 80 + col] = (uint16_t)color << 8 | (uint8_t)*str++;
        }
        col++;
    }
}

void console_write_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    console_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> shift) & 0xF]);
    }
}

void debug_puts(const char *str) {
    while (*str != '\0') {
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(*str));
        str++;
    }
}

void debug_put_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    debug_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        char ch = digits[(value >> shift) & 0xF];
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(ch));
    }
}

void halt_forever(void) {
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
