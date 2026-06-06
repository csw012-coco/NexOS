#include "hal/x86/platform.h"
#include "bootx/bootx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/vga.h"

static volatile uint32_t g_hal_timer_ticks;
static uint32_t g_hal_timer_hz;
static uint64_t g_hal_next_mmio_virt = 0xffffffff82000000ull;

enum {
    HAL_MMIO_PAGE_SIZE = 4096u,
    HAL_MMIO_PAGE_MASK = 0xfffffffffffff000ull,
    HAL_MMIO_VIRT_LIMIT = 0xffffffff84000000ull
};

void hal_display_init(const struct bootx_console_info *console) {
    framebuffer_display_init(console);
}

int hal_display_enable_backbuffer(void) {
    if (!framebuffer_display_active()) {
        return 0;
    }
    return framebuffer_display_enable_backbuffer();
}

void hal_display_load_font(const struct bootx_boot_info *boot_info) {
    framebuffer_display_load_font_from_boot_modules(boot_info);
}

void hal_paging_init(uint64_t kernel_phys_addr) {
    hal_x86_paging_init_impl(kernel_phys_addr);
}

void hal_platform_init(const struct hal_interrupt_handlers *handlers) {
    hal_x86_platform_init_impl(handlers);
}

uint64_t hal_paging_current_root(void) {
    return hal_x86_paging_current_root_impl();
}

void hal_paging_switch_root(uint64_t cr3) {
    hal_x86_paging_switch_root_impl(cr3);
}

uint64_t hal_paging_create_user_root(void) {
    return hal_x86_paging_create_user_root_impl();
}

void hal_paging_destroy_user_root(uint64_t cr3) {
    hal_x86_paging_destroy_user_root_impl(cr3);
}

void hal_paging_allow_user_page(uint64_t addr) {
    hal_x86_paging_allow_user_page_impl(addr);
}

void hal_paging_allow_user_range(uint64_t start, uint64_t end) {
    hal_x86_paging_allow_user_range_impl(start, end);
}

void hal_paging_set_supervisor_range(uint64_t start, uint64_t end) {
    hal_x86_paging_set_supervisor_range_impl(start, end);
}

int hal_paging_map_page(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable) {
    return hal_x86_paging_map_page_impl(virt_addr, phys_addr, user_accessible, writable);
}

int hal_paging_map_page_with_exec(uint64_t virt_addr,
                                  uint64_t phys_addr,
                                  int user_accessible,
                                  int writable,
                                  int executable) {
    return hal_x86_paging_map_page_with_exec_impl(virt_addr, phys_addr, user_accessible, writable, executable);
}

int hal_paging_unmap_page(uint64_t virt_addr, uint64_t *phys_addr) {
    return hal_x86_paging_unmap_page_impl(virt_addr, phys_addr);
}

int hal_paging_get_mapping(uint64_t virt_addr, uint64_t *phys_addr) {
    return hal_x86_paging_get_mapping_impl(virt_addr, phys_addr);
}

int hal_paging_get_mapping_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags) {
    return hal_x86_paging_get_mapping_info_impl(virt_addr, phys_addr, flags);
}

void *hal_phys_direct_map(uint64_t phys_addr) {
    return hal_x86_paging_phys_direct_map_impl(phys_addr);
}

void *hal_mmio_map(uint64_t phys_addr, uint64_t length) {
    uint64_t phys_base = phys_addr & HAL_MMIO_PAGE_MASK;
    uint64_t page_off = phys_addr & ~HAL_MMIO_PAGE_MASK;
    uint64_t bytes;
    uint64_t pages;
    uint64_t virt_base;

    if (phys_addr == 0u || length == 0u) {
        return 0;
    }
    if (phys_addr <= 0xffffffffull && length - 1u <= 0xffffffffull - phys_addr) {
        return hal_phys_direct_map(phys_addr);
    }
    bytes = page_off + length;
    pages = (bytes + HAL_MMIO_PAGE_SIZE - 1u) / HAL_MMIO_PAGE_SIZE;
    virt_base = g_hal_next_mmio_virt;
    if (virt_base + pages * HAL_MMIO_PAGE_SIZE > HAL_MMIO_VIRT_LIMIT) {
        return 0;
    }
    for (uint64_t i = 0u; i < pages; i++) {
        uint64_t virt = virt_base + i * HAL_MMIO_PAGE_SIZE;
        uint64_t phys = phys_base + i * HAL_MMIO_PAGE_SIZE;

        if (!hal_paging_map_page_with_exec(virt, phys, 0, 1, 0)) {
            return 0;
        }
    }
    g_hal_next_mmio_virt = virt_base + pages * HAL_MMIO_PAGE_SIZE;
    return (void *)(uintptr_t)(virt_base + page_off);
}

void hal_timer_init(uint32_t pit_hz) {
    g_hal_timer_ticks = 0;
    g_hal_timer_hz = pit_hz;
    hal_x86_timer_init_impl(pit_hz);
}

void hal_timer_notify_tick(void) {
    g_hal_timer_ticks++;
    if (framebuffer_display_active()) {
        framebuffer_display_tick(g_hal_timer_ticks);
    }
}

uint32_t hal_timer_current_ticks(void) {
    return g_hal_timer_ticks;
}

uint32_t hal_timer_hz(void) {
    return g_hal_timer_hz;
}

void hal_irq_ack(uint8_t irq) {
    hal_x86_irq_ack_impl(irq);
}

void hal_irq_set_mask(uint8_t irq, int masked) {
    hal_x86_irq_set_mask_impl(irq, masked);
}

uint8_t hal_keyboard_read_scancode(void) {
    return hal_x86_keyboard_read_scancode_impl();
}

static uint32_t hal_display_cell_from_vga(uint16_t cell) {
    return ((uint32_t)((cell >> 8) & 0xffu) << HAL_DISPLAY_CELL_COLOR_SHIFT) |
           (uint32_t)(cell & 0xffu);
}

static uint16_t hal_display_cell_to_vga(uint32_t cell) {
    uint32_t codepoint = cell & HAL_DISPLAY_CELL_CODEPOINT_MASK;
    uint8_t color = (uint8_t)(cell >> HAL_DISPLAY_CELL_COLOR_SHIFT);
    uint8_t ch = '?';

    if ((cell & HAL_DISPLAY_CELL_CONT) != 0u) {
        ch = ' ';
    } else if (codepoint <= 0xffu) {
        ch = (uint8_t)codepoint;
    }
    return (uint16_t)(((uint16_t)color << 8) | ch);
}

uint32_t hal_display_read_cell(uint16_t row, uint16_t col) {
    if (framebuffer_display_active()) {
        return framebuffer_display_read_cell(row, col);
    }
    return hal_display_cell_from_vga(vga_read_cell(row, col));
}

void hal_display_write_cell(uint16_t row, uint16_t col, uint32_t value) {
    if (framebuffer_display_active()) {
        framebuffer_display_write_cell(row, col, value);
        return;
    }
    vga_write_cell(row, col, hal_display_cell_to_vga(value));
}

void hal_display_clear_row(uint16_t row, uint8_t color) {
    if (framebuffer_display_active()) {
        framebuffer_display_clear_row(row, color);
        return;
    }
    vga_clear_row(row, color);
}

void hal_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch) {
    if (framebuffer_display_active()) {
        framebuffer_display_put_at(row, col, color, ch);
        return;
    }
    vga_put_at(row, col, color, ch);
}

void hal_display_enable_cursor(uint8_t start, uint8_t end) {
    if (framebuffer_display_active()) {
        framebuffer_display_enable_cursor(start, end);
        return;
    }
    vga_enable_cursor(start, end);
}

void hal_display_set_cursor(uint16_t row, uint16_t col) {
    if (framebuffer_display_active()) {
        framebuffer_display_set_cursor(row, col);
        return;
    }
    vga_set_cursor(row, col);
}

uint16_t hal_display_text_columns(void) {
    if (framebuffer_display_active()) {
        return framebuffer_display_columns();
    }
    return VGA_WIDTH;
}

uint16_t hal_display_text_rows(void) {
    if (framebuffer_display_active()) {
        return framebuffer_display_rows();
    }
    return VGA_HEIGHT;
}

uint32_t hal_x86_display_cell_height_impl(void) {
    return framebuffer_display_cell_height();
}

void hal_x86_display_bitblt_impl(uint32_t src_x,
                                 uint32_t src_y,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t dst_x,
                                 uint32_t dst_y) {
    framebuffer_display_bitblt(
        src_x,
        src_y,
        width,
        height,
        dst_x,
        dst_y
    );
}

uint32_t hal_display_cell_height(void) {
    return hal_x86_display_cell_height_impl();
}

void hal_display_bitblt(uint32_t src_x,
                        uint32_t src_y,
                        uint32_t width,
                        uint32_t height,
                        uint32_t dst_x,
                        uint32_t dst_y) {
    hal_x86_display_bitblt_impl(
        src_x,
        src_y,
        width,
        height,
        dst_x,
        dst_y
    );
}

void hal_display_scroll_rows(uint16_t top_row, uint16_t bottom_row, uint8_t clear_color) {
    if (framebuffer_display_active()) {
        framebuffer_display_scroll_rows(top_row, bottom_row, clear_color);
        return;
    }
    vga_scroll_rows(top_row, bottom_row, clear_color);
}

void hal_display_blit_surface(const struct surface *surface,
                              uint32_t src_x,
                              uint32_t src_y,
                              uint32_t width,
                              uint32_t height,
                              int32_t dst_x,
                              int32_t dst_y) {
    if (framebuffer_display_active()) {
        framebuffer_display_blit_surface(surface, src_x, src_y, width, height, dst_x, dst_y);
    }
}

void hal_display_draw_pixel(int32_t x, int32_t y, uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_draw_pixel(x, y, rgb);
    }
}

void hal_display_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_draw_line(x0, y0, x1, y1, rgb);
    }
}

void hal_display_draw_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_draw_rect(x, y, width, height, rgb);
    }
}

void hal_display_fill_rect_rgb(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_fill_rect_rgb(x, y, width, height, rgb);
    }
}

void hal_display_draw_triangle(int32_t x0,
                               int32_t y0,
                               int32_t x1,
                               int32_t y1,
                               int32_t x2,
                               int32_t y2,
                               uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_draw_triangle(x0, y0, x1, y1, x2, y2, rgb);
    }
}

void hal_display_fill_triangle(int32_t x0,
                               int32_t y0,
                               int32_t x1,
                               int32_t y1,
                               int32_t x2,
                               int32_t y2,
                               uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_fill_triangle(x0, y0, x1, y1, x2, y2, rgb);
    }
}

void hal_display_draw_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_draw_circle(cx, cy, radius, rgb);
    }
}

void hal_display_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    if (framebuffer_display_active()) {
        framebuffer_display_fill_circle(cx, cy, radius, rgb);
    }
}

void hal_display_present(void) {
    if (framebuffer_display_active()) {
        framebuffer_display_present();
    }
}

void hal_display_set_mouse_cursor_enabled(int enabled) {
    if (framebuffer_display_active()) {
        framebuffer_display_set_mouse_cursor_enabled(enabled);
    }
}

void hal_display_move_mouse_cursor(int32_t dx, int32_t dy) {
    if (framebuffer_display_active()) {
        framebuffer_display_move_mouse_cursor(dx, dy);
    }
}

int hal_display_mouse_cursor_cell(uint16_t *row_out, uint16_t *col_out) {
    if (framebuffer_display_active()) {
        return framebuffer_display_mouse_cursor_cell(row_out, col_out);
    }
    return 0;
}

uint8_t hal_io_in8(uint16_t port) {
    return hal_x86_io_in8_impl(port);
}

uint16_t hal_io_in16(uint16_t port) {
    return hal_x86_io_in16_impl(port);
}

void hal_io_out8(uint16_t port, uint8_t value) {
    hal_x86_io_out8_impl(port, value);
}

void hal_io_out16(uint16_t port, uint16_t value) {
    hal_x86_io_out16_impl(port, value);
}

void hal_cpu_cli(void) {
    hal_x86_cpu_cli_impl();
}

void hal_cpu_sti(void) {
    hal_x86_cpu_sti_impl();
}

void hal_cpu_halt(void) {
    hal_x86_cpu_halt_impl();
}

uint64_t hal_cpu_current_sp(void) {
    return hal_x86_cpu_current_sp_impl();
}

uint64_t hal_cpu_read_tsc(void) {
    return hal_x86_cpu_read_tsc_impl();
}

void hal_cpu_cpuid(uint32_t leaf,
                   uint32_t subleaf,
                   uint32_t *eax,
                   uint32_t *ebx,
                   uint32_t *ecx,
                   uint32_t *edx) {
    hal_x86_cpu_cpuid_impl(leaf, subleaf, eax, ebx, ecx, edx);
}

void hal_usermode_enter(uint64_t entry, uint64_t user_stack) {
    hal_x86_usermode_enter_impl(entry, user_stack);
}

void hal_usermode_resume(const struct syscall_frame *frame) {
    hal_x86_usermode_resume_impl(frame);
}

uint64_t hal_kernel_stack_top(void) {
    return hal_x86_kernel_stack_top_impl();
}

void hal_set_kernel_stack_top(uint64_t rsp0) {
    hal_x86_set_kernel_stack_top_impl(rsp0);
}
