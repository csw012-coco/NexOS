#include "arch/x86/i386/gdt.h"
#include "arch/x86/i386/keyboard.h"
#include "arch/x86/i386/paging.h"
#include "arch/x86/common/pic.h"
#include "arch/x86/common/io.h"
#include "hal/hal.h"

enum {
    VGA_COLUMNS = 80,
    VGA_ROWS = 25,
    VGA_CRTC_INDEX = 0x3d4,
    VGA_CRTC_DATA = 0x3d5
};

static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static volatile uint32_t timer_ticks;
static uint32_t timer_hz;

static uint16_t display_cell_to_vga(uint32_t cell) {
    uint32_t codepoint = cell & HAL_DISPLAY_CELL_CODEPOINT_MASK;
    uint8_t color = (uint8_t)(cell >> HAL_DISPLAY_CELL_COLOR_SHIFT);
    uint8_t ch = codepoint <= 0xffu ? (uint8_t)codepoint : (uint8_t)'?';

    if ((cell & HAL_DISPLAY_CELL_CONT) != 0u) {
        ch = ' ';
    }
    return (uint16_t)(((uint16_t)color << 8) | ch);
}

void hal_display_init(const struct bootx_console_info *console) {
    (void)console;
}

void hal_display_load_font(const struct bootx_boot_info *boot_info) {
    (void)boot_info;
}

int hal_display_enable_backbuffer(void) {
    return 0;
}

void hal_display_begin_update(void) {
}

void hal_display_end_update(void) {
}

void hal_display_service_pending(void) {
}

uint32_t hal_display_read_cell(uint16_t row, uint16_t col) {
    uint16_t cell;

    if (row >= VGA_ROWS || col >= VGA_COLUMNS) {
        return 0;
    }
    cell = vga[row * VGA_COLUMNS + col];
    return ((uint32_t)(cell >> 8) << HAL_DISPLAY_CELL_COLOR_SHIFT) |
           (uint8_t)cell;
}

void hal_display_write_cell(uint16_t row, uint16_t col, uint32_t value) {
    if (row < VGA_ROWS && col < VGA_COLUMNS) {
        vga[row * VGA_COLUMNS + col] = display_cell_to_vga(value);
    }
}

void hal_display_clear_row(uint16_t row, uint8_t color) {
    if (row >= VGA_ROWS) {
        return;
    }
    for (uint16_t col = 0; col < VGA_COLUMNS; col++) {
        vga[row * VGA_COLUMNS + col] =
            (uint16_t)(((uint16_t)color << 8) | ' ');
    }
}

void hal_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch) {
    if (row < VGA_ROWS && col < VGA_COLUMNS) {
        vga[row * VGA_COLUMNS + col] =
            (uint16_t)(((uint16_t)color << 8) | (uint8_t)ch);
    }
}

void hal_display_enable_cursor(uint8_t start, uint8_t end) {
    outb(VGA_CRTC_INDEX, 0x0a);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xc0u) | start));
    outb(VGA_CRTC_INDEX, 0x0b);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xe0u) | end));
}

void hal_display_set_cursor(uint16_t row, uint16_t col) {
    uint16_t position;

    if (row >= VGA_ROWS) {
        row = VGA_ROWS - 1u;
    }
    if (col >= VGA_COLUMNS) {
        col = VGA_COLUMNS - 1u;
    }
    position = (uint16_t)(row * VGA_COLUMNS + col);
    outb(VGA_CRTC_INDEX, 0x0f);
    outb(VGA_CRTC_DATA, (uint8_t)position);
    outb(VGA_CRTC_INDEX, 0x0e);
    outb(VGA_CRTC_DATA, (uint8_t)(position >> 8));
}

uint16_t hal_display_text_columns(void) {
    return VGA_COLUMNS;
}

uint16_t hal_display_text_rows(void) {
    return VGA_ROWS;
}

uint32_t hal_display_cell_height(void) {
    return 16u;
}

void hal_display_scroll_rows(uint16_t top_row,
                             uint16_t bottom_row,
                             uint8_t clear_color) {
    if (top_row >= bottom_row || top_row >= VGA_ROWS) {
        return;
    }
    if (bottom_row >= VGA_ROWS) {
        bottom_row = VGA_ROWS - 1u;
    }
    for (uint16_t row = top_row; row < bottom_row; row++) {
        for (uint16_t col = 0; col < VGA_COLUMNS; col++) {
            vga[row * VGA_COLUMNS + col] =
                vga[(row + 1u) * VGA_COLUMNS + col];
        }
    }
    hal_display_clear_row(bottom_row, clear_color);
}

void hal_display_present(void) {
}

void hal_timer_init(uint32_t pit_hz) {
    timer_ticks = 0;
    timer_hz = pit_hz == 0u ? 100u : pit_hz;
    i386_pit_init(timer_hz);
}

void hal_timer_notify_tick(void) {
    timer_ticks++;
}

uint32_t hal_timer_current_ticks(void) {
    return timer_ticks;
}

uint32_t hal_timer_hz(void) {
    return timer_hz;
}

void hal_irq_ack(uint8_t irq) {
    i386_pic_send_eoi(irq);
}

void hal_irq_set_mask(uint8_t irq, int masked) {
    i386_pic_set_mask(irq, masked);
}

uint8_t hal_keyboard_read_scancode(void) {
    struct i386_key_event event;

    return i386_keyboard_pop(&event) ? event.scancode : 0u;
}

uint64_t hal_paging_current_root(void) {
    return i386_paging_root();
}

void hal_paging_switch_root(uint64_t cr3) {
    i386_paging_switch((uint32_t)cr3);
}

void hal_cpu_cli(void) {
    __asm__ volatile("cli" : : : "memory");
}

void hal_cpu_sti(void) {
    __asm__ volatile("sti" : : : "memory");
}

void hal_cpu_halt(void) {
    __asm__ volatile("hlt");
}

void hal_cpu_wait_for_interrupt(void) {
    __asm__ volatile("sti; hlt" : : : "memory");
}

void hal_cpu_wait_for_event(void) {
    hal_cpu_wait_for_interrupt();
}

void hal_cpu_relax(void) {
    __asm__ volatile("pause");
}

uint64_t hal_cpu_current_sp(void) {
    uint32_t value;

    __asm__ volatile("mov %%esp, %0" : "=r"(value));
    return value;
}

uint64_t hal_kernel_stack_top(void) {
    return i386_gdt_kernel_stack_top();
}

void hal_set_kernel_stack_top(uint64_t rsp0) {
    (void)rsp0;
}
