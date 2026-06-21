#include "bootx.h"

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static const char digits[] = "0123456789ABCDEF";

static void debug_puts_local(const char *text) {
    while (*text != '\0') {
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(*text));
        text++;
    }
}

static void write_line(uint16_t row, uint8_t color, const char *text) {
    uint16_t col = 0;
    while (*text != '\0' && col < 80) {
        vga[row * 80 + col] = (uint16_t)color << 8 | (uint8_t)*text++;
        col++;
    }
}

static void write_line_at(uint16_t row, uint16_t col, uint8_t color, const char *text) {
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

void kernel_main(const struct bootx_boot_info *boot_info) {
    debug_puts_local("kernel: entered\n");
    for (uint16_t i = 0; i < 80 * 25; i++) {
        vga[i] = 0x1F20;
    }

    write_line(2, 0x1F, "boot/x demo kernel");
    write_line(4, 0x1E, "protocol magic:");
    write_hex32(4, 16, 0x1F, boot_info->hdr.magic);

    write_line(5, 0x1E, "protocol version:");
    write_dec(5, 18, 0x1F, boot_info->hdr.version);

    write_line(6, 0x1E, "boot drive:");
    write_hex32(6, 12, 0x1F, boot_info->boot_drive);

    write_line(7, 0x1E, "cmdline:");
    write_line_at(7, 10, 0x1F, (const char *)(uintptr_t)boot_info->cmdline);

    write_line(9, 0x1E, "kernel phys:");
    write_hex64(9, 13, 0x1F, boot_info->kernel_phys_addr);

    write_line(10, 0x1E, "kernel size:");
    write_hex64(10, 13, 0x1F, boot_info->kernel_phys_size);

    write_line(11, 0x1E, "modules:");
    write_dec(11, 10, 0x1F, boot_info->module_count);

    write_line(12, 0x1E, "memmap entries:");
    write_dec(12, 16, 0x1F, boot_info->memmap_count);

    write_line(13, 0x1E, "console:");
    write_dec(13, 10, 0x1F, boot_info->console.type);

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
