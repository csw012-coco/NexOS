#include "bootx.h"

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static const char digits[] = "0123456789ABCDEF";

static void debug_puts64(const char *text) {
    while (*text != '\0') {
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(*text));
        text++;
    }
}

static void debug_put_hex32_local(uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(digits[(value >> shift) & 0xF]));
    }
}

static void debug_put_name11(const char name[12]) {
    for (uint32_t i = 0; i < 11 && name[i] != '\0'; i++) {
        if (i == 8 && name[i] != ' ') {
            uint8_t dot = '.';
            __asm__ __volatile__("outb %0, $0xE9" : : "a"(dot));
        }
        if (name[i] != ' ') {
            uint8_t ch = (uint8_t)name[i];
            __asm__ __volatile__("outb %0, $0xE9" : : "a"(ch));
        }
    }
}

static void write_line(uint16_t row, uint16_t col, uint8_t color, const char *text) {
    while (*text != '\0' && col < 80) {
        vga[row * 80 + col] = (uint16_t)color << 8 | (uint8_t)*text++;
        col++;
    }
}

static void write_hex32(uint16_t row, uint16_t col, uint8_t color, uint32_t value) {
    for (int shift = 28; shift >= 0 && col < 80; shift -= 4) {
        vga[row * 80 + col] = (uint16_t)color << 8 | digits[(value >> shift) & 0xF];
        col++;
    }
}

static void write_hex64(uint16_t row, uint16_t col, uint8_t color, uint64_t value) {
    write_hex32(row, col, color, (uint32_t)(value >> 32));
    write_hex32(row, col + 8, color, (uint32_t)value);
}

static void write_dec(uint16_t row, uint16_t col, uint8_t color, uint32_t value) {
    char buf[11];
    int pos = 0;

    if (value == 0) {
        vga[row * 80 + col] = (uint16_t)color << 8 | '0';
        return;
    }

    while (value > 0 && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (pos > 0 && col < 80) {
        vga[row * 80 + col] = (uint16_t)color << 8 | buf[--pos];
        col++;
    }
}

static uint32_t make_fb_color(const struct bootx_console_info *console, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t value = 0;

    if (console->framebuffer_bpp == 8) {
        if (r > 220 && g > 220 && b > 220) {
            return 15;
        }
        if (r > 220 && g > 180) {
            return 14;
        }
        if (g > 180 && b > 140) {
            return 11;
        }
        if (g > 180) {
            return 10;
        }
        if (b > 180) {
            return 9;
        }
        if (r > 180) {
            return 12;
        }
        return 1;
    }

    if (console->red_mask_size != 0) {
        value |= ((uint32_t)r >> (8 - console->red_mask_size)) << console->red_mask_shift;
    }
    if (console->green_mask_size != 0) {
        value |= ((uint32_t)g >> (8 - console->green_mask_size)) << console->green_mask_shift;
    }
    if (console->blue_mask_size != 0) {
        value |= ((uint32_t)b >> (8 - console->blue_mask_size)) << console->blue_mask_shift;
    }

    return value;
}

static void fb_put_pixel(const struct bootx_console_info *console, uint32_t x, uint32_t y, uint32_t color) {
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)console->framebuffer_addr;
    volatile uint8_t *pixel;

    if (x >= console->width || y >= console->height) {
        return;
    }

    pixel = fb + (uint64_t)y * console->pitch + (uint64_t)x * (console->framebuffer_bpp / 8u);
    if (console->framebuffer_bpp == 32) {
        *(volatile uint32_t *)pixel = color;
    } else if (console->framebuffer_bpp == 24) {
        pixel[0] = (uint8_t)color;
        pixel[1] = (uint8_t)(color >> 8);
        pixel[2] = (uint8_t)(color >> 16);
    }
}

static void fb_fill_rect(const struct bootx_console_info *console, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            fb_put_pixel(console, x + col, y + row, color);
        }
    }
}

static void draw_framebuffer_demo(const struct bootx_boot_info *boot_info) {
    const struct bootx_console_info *console = &boot_info->console;
    uint32_t bg = make_fb_color(console, 16, 24, 44);
    uint32_t panel = make_fb_color(console, 35, 76, 160);
    uint32_t accent = make_fb_color(console, 255, 206, 64);
    uint32_t accent2 = make_fb_color(console, 84, 214, 174);
    uint32_t stripe = make_fb_color(console, 196, 234, 255);

    fb_fill_rect(console, 0, 0, console->width, console->height, bg);
    fb_fill_rect(console, 0, 0, console->width, 72, panel);
    fb_fill_rect(console, 0, 88, console->width, 8, accent);
    fb_fill_rect(console, 48, 144, console->width > 96 ? console->width - 96 : console->width, 96, panel);
    fb_fill_rect(console, 48, 260, console->width > 96 ? console->width - 96 : console->width, 18, stripe);
    fb_fill_rect(console, 64, 320, console->width / 3, 24, accent);
    fb_fill_rect(console, 64, 360, console->width / 2, 24, accent2);
    fb_fill_rect(console, 64, 400, console->width / 4, 24, stripe);

    for (uint32_t y = 460; y < 460 + 32 && y < console->height; y++) {
        for (uint32_t x = 64; x < console->width - 64; x++) {
            if (console->framebuffer_bpp == 8) {
                fb_put_pixel(console, x, y, 1u + ((x * 14u) / console->width));
            } else {
                uint8_t shade = (uint8_t)((x * 255u) / console->width);
                fb_put_pixel(console, x, y, make_fb_color(console, shade, 80, 255u - shade));
            }
        }
    }
}

void kernel_main64(const struct bootx_boot_info *boot_info) {
    const struct bootx_module *modules =
        (const struct bootx_module *)(uintptr_t)boot_info->modules;

    debug_puts64("kernel64: entered\n");
    debug_puts64("kernel64: console=");
    debug_put_hex32_local(boot_info->console.type);
    debug_puts64("\n");
    debug_puts64("kernel64: modules=");
    debug_put_hex32_local(boot_info->module_count);
    debug_puts64("\n");
    for (uint32_t i = 0; i < boot_info->module_count; i++) {
        debug_puts64("kernel64: module ");
        debug_put_hex32_local(i);
        debug_puts64(" ");
        debug_put_name11(modules[i].name);
        debug_puts64(" addr=");
        debug_put_hex32_local(modules[i].address);
        debug_puts64(" size=");
        debug_put_hex32_local(modules[i].size);
        debug_puts64("\n");
    }

    if (boot_info->console.type == BOOTX_CONSOLE_FRAMEBUFFER) {
        debug_puts64("kernel64: framebuffer active\n");
        draw_framebuffer_demo(boot_info);
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    for (uint16_t i = 0; i < 80 * 25; i++) {
        vga[i] = 0x2F20;
    }

    write_line(2, 0, 0x2F, "boot/x demo kernel64");
    write_line(4, 0, 0x2E, "long mode path reached");
    write_line(6, 0, 0x2E, "protocol magic:");
    write_hex32(6, 16, 0x2F, boot_info->hdr.magic);

    write_line(7, 0, 0x2E, "protocol version:");
    write_dec(7, 18, 0x2F, boot_info->hdr.version);

    write_line(8, 0, 0x2E, "boot drive:");
    write_hex32(8, 12, 0x2F, boot_info->boot_drive);

    write_line(9, 0, 0x2E, "cmdline:");
    write_line(9, 10, 0x2F, (const char *)(uintptr_t)boot_info->cmdline);

    write_line(11, 0, 0x2E, "kernel entry:");
    write_hex64(11, 13, 0x2F, boot_info->kernel_entry);

    write_line(12, 0, 0x2E, "memmap entries:");
    write_dec(12, 16, 0x2F, boot_info->memmap_count);

    write_line(13, 0, 0x2E, "console:");
    write_dec(13, 10, 0x2F, boot_info->console.type);

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
