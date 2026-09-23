#include "arch/x86/i386/gdt.h"
#include "arch/x86/i386/context.h"
#include "arch/x86/i386/keyboard.h"
#include "arch/x86/i386/mm/paging.h"
#include "arch/x86/i386/mm/pmm.h"
#include "arch/x86/common/pic.h"
#include "arch/x86/common/io.h"
#include "block/blockdev.h"
#include "drivers/bus/acpi.h"
#include "drivers/bus/ioapic.h"
#include "drivers/bus/lapic.h"
#include "drivers/video/framebuffer.h"
#include "hal/hal.h"
#include "kernel/internal/core/kernel_panic_internal.h"
#include "kernel/internal/mem/vmm_diag.h"
#include "kernel/public/sys/syscall.h"

enum {
    VGA_COLUMNS = 80,
    VGA_ROWS = 25,
    VGA_CRTC_INDEX = 0x3d4,
    VGA_CRTC_DATA = 0x3d5,
    I386_HAL_DIRECT_MAP_BASE = 0xf9400000u,
    I386_HAL_DIRECT_MAP_PAGES_PER_SLOT = 16u,
    I386_HAL_DIRECT_MAP_SLOTS = 128u,
    I386_HAL_MMIO_MAP_BASE = 0xf8000000u,
    I386_HAL_MMIO_MAP_PAGES = 512u
};

static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static volatile uint32_t timer_ticks;
static uint32_t timer_hz;
static uint32_t direct_map_slots_used;
static uint32_t mmio_map_pages_used;

static uint16_t display_cell_to_vga(uint32_t cell) {
    uint32_t codepoint = cell & HAL_DISPLAY_CELL_CODEPOINT_MASK;
    uint8_t color = (uint8_t)(cell >> HAL_DISPLAY_CELL_COLOR_SHIFT);
    uint8_t ch = codepoint <= 0xffu ? (uint8_t)codepoint : (uint8_t)'?';

    if ((cell & HAL_DISPLAY_CELL_CONT) != 0u) {
        ch = ' ';
    }
    return (uint16_t)(((uint16_t)color << 8) | ch);
}

void hal_display_init(const struct janus_console_info *console) {
    framebuffer_display_init(console);
}

void hal_display_load_font(const struct janus_boot_info *boot_info) {
    framebuffer_display_load_font_from_boot_modules(boot_info);
}

int hal_display_enable_backbuffer(void) {
    if (framebuffer_display_active()) {
        return framebuffer_display_enable_backbuffer();
    }
    return 0;
}

void hal_display_begin_update(void) {
    if (framebuffer_display_active()) {
        framebuffer_display_begin_update();
    }
}

void hal_display_end_update(void) {
    if (framebuffer_display_active()) {
        framebuffer_display_end_update();
    }
}

void hal_display_service_pending(void) {
    if (framebuffer_display_active()) {
        framebuffer_display_service_pending();
    }
}

uint32_t hal_display_read_cell(uint16_t row, uint16_t col) {
    uint16_t cell;

    if (framebuffer_display_active()) {
        return framebuffer_display_read_cell(row, col);
    }
    if (row >= VGA_ROWS || col >= VGA_COLUMNS) {
        return 0;
    }
    cell = vga[row * VGA_COLUMNS + col];
    return ((uint32_t)(cell >> 8) << HAL_DISPLAY_CELL_COLOR_SHIFT) |
           (uint8_t)cell;
}

void hal_display_write_cell(uint16_t row, uint16_t col, uint32_t value) {
    if (framebuffer_display_active()) {
        framebuffer_display_write_cell(row, col, value);
        return;
    }
    if (row < VGA_ROWS && col < VGA_COLUMNS) {
        vga[row * VGA_COLUMNS + col] = display_cell_to_vga(value);
    }
}

void hal_display_clear_row(uint16_t row, uint8_t color) {
    if (framebuffer_display_active()) {
        framebuffer_display_clear_row(row, color);
        return;
    }
    if (row >= VGA_ROWS) {
        return;
    }
    for (uint16_t col = 0; col < VGA_COLUMNS; col++) {
        vga[row * VGA_COLUMNS + col] =
            (uint16_t)(((uint16_t)color << 8) | ' ');
    }
}

void hal_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch) {
    if (framebuffer_display_active()) {
        framebuffer_display_put_at(row, col, color, ch);
        return;
    }
    if (row < VGA_ROWS && col < VGA_COLUMNS) {
        vga[row * VGA_COLUMNS + col] =
            (uint16_t)(((uint16_t)color << 8) | (uint8_t)ch);
    }
}

void hal_display_enable_cursor(uint8_t start, uint8_t end) {
    if (framebuffer_display_active()) {
        framebuffer_display_enable_cursor(start, end);
        return;
    }
    outb(VGA_CRTC_INDEX, 0x0a);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xc0u) | start));
    outb(VGA_CRTC_INDEX, 0x0b);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xe0u) | end));
}

void hal_display_disable_cursor(void) {
    if (framebuffer_display_active()) {
        framebuffer_display_disable_cursor();
        return;
    }
    outb(VGA_CRTC_INDEX, 0x0a);
    outb(VGA_CRTC_DATA, (uint8_t)(inb(VGA_CRTC_DATA) | 0x20u));
}

void hal_display_set_cursor(uint16_t row, uint16_t col) {
    uint16_t position;

    if (framebuffer_display_active()) {
        framebuffer_display_set_cursor(row, col);
        return;
    }
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
    if (framebuffer_display_active()) {
        return framebuffer_display_columns();
    }
    return VGA_COLUMNS;
}

uint16_t hal_display_text_rows(void) {
    if (framebuffer_display_active()) {
        return framebuffer_display_rows();
    }
    return VGA_ROWS;
}

uint32_t hal_display_cell_height(void) {
    if (framebuffer_display_active()) {
        return framebuffer_display_cell_height();
    }
    return 16u;
}

void hal_display_bitblt(uint32_t src_x,
                        uint32_t src_y,
                        uint32_t width,
                        uint32_t height,
                        uint32_t dst_x,
                        uint32_t dst_y) {
    if (framebuffer_display_active()) {
        framebuffer_display_bitblt(src_x, src_y, width, height, dst_x, dst_y);
    }
}

void hal_display_scroll_rows(uint16_t top_row,
                             uint16_t bottom_row,
                             uint8_t clear_color) {
    if (framebuffer_display_active()) {
        framebuffer_display_scroll_rows(top_row, bottom_row, clear_color);
        return;
    }
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

void hal_display_blit_xrgb8888(const uint32_t *pixels,
                               uint32_t pitch,
                               uint32_t width,
                               uint32_t height,
                               int32_t dst_x,
                               int32_t dst_y) {
    if (framebuffer_display_active()) {
        framebuffer_display_blit_xrgb8888(pixels, pitch, width, height, dst_x, dst_y);
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

void hal_timer_init(uint32_t pit_hz) {
    timer_ticks = 0;
    timer_hz = pit_hz == 0u ? 100u : pit_hz;
    i386_pit_init(timer_hz);
}

void hal_timer_notify_tick(void) {
    timer_ticks++;

    if (framebuffer_display_active()) {
        framebuffer_display_tick(timer_ticks);
    }
}

uint32_t hal_timer_current_ticks(void) {
    return timer_ticks;
}

uint32_t hal_timer_hz(void) {
    return timer_hz;
}

void hal_irq_ack(uint8_t irq) {
    if (ioapic_irq_enabled(irq)) {
        lapic_send_eoi();
        return;
    }
    i386_pic_send_eoi(irq);
}

void hal_irq_set_mask(uint8_t irq, int masked) {
    if (ioapic_irq_enabled(irq)) {
        (void)ioapic_set_irq_mask(irq, masked);
        i386_pic_set_mask(irq, 1);
        return;
    }
    i386_pic_set_mask(irq, masked);
}

int hal_irq_route(uint8_t irq, struct hal_irq_route *out) {
    struct acpi_irq_override_route route;
    int overridden;

    if (out == 0) {
        return 0;
    }
    if (irq >= 16u) {
        return 0;
    }
    overridden = acpi_irq_route_for_isa(irq, &route);
    out->irq = irq;
    out->acpi_override = overridden ? 1u : 0u;
    out->flags = overridden ? route.flags : 0u;
    out->gsi = overridden ? route.gsi : irq;
    return 1;
}

uint8_t hal_keyboard_read_scancode(void) {
    struct i386_key_event event;

    return i386_keyboard_pop(&event) ? event.scancode : 0u;
}

int hal_keyboard_inject_scancode(uint8_t scancode) {
    return i386_keyboard_inject_scancode(scancode);
}

int hal_paging_enabled(void) {
    return i386_paging_enabled();
}

int hal_pmm_init_from_boot(const struct janus_boot_info *boot_info,
                           uint64_t kernel_phys_addr) {
    (void)kernel_phys_addr;
    return i386_pmm_init(boot_info);
}

uint64_t hal_paging_current_root(void) {
    return i386_paging_root();
}

void hal_paging_switch_root(uint64_t cr3) {
    i386_paging_switch((uint32_t)cr3);
}

int hal_paging_get_mapping_info_in_root(uint64_t root,
                                        uint64_t virt_addr,
                                        uint64_t *phys_addr,
                                        uint64_t *flags) {
    return vmm_query_mapping_in_context(root, virt_addr, phys_addr, flags);
}

void hal_paging_log_init_exec_failure(const struct hal_boot_trace_ops *ops, void *ctx) {
    (void)ops;
    (void)ctx;
}

void hal_paging_log_panic_entry(const struct hal_boot_trace_ops *ops,
                                void *ctx,
                                uint64_t entry) {
    (void)ops;
    (void)ctx;
    (void)entry;
}

void hal_paging_log_panic_target_entry(const struct hal_boot_trace_ops *ops,
                                       void *ctx,
                                       uint64_t target_root,
                                       uint64_t entry) {
    (void)ops;
    (void)ctx;
    (void)target_root;
    (void)entry;
}

void hal_paging_log_panic_switch_trace(const struct hal_boot_trace_ops *ops,
                                       void *ctx,
                                       uint64_t entry) {
    (void)ops;
    (void)ctx;
    (void)entry;
}

void hal_paging_log_panic_summary(const struct hal_boot_trace_ops *ops,
                                  void *ctx,
                                  uint64_t target_root,
                                  uint64_t entry) {
    (void)ops;
    (void)ctx;
    (void)target_root;
    (void)entry;
}

int hal_process_context_init_user(struct process_context *context,
                                  uint64_t entry,
                                  uint64_t stack,
                                  uint64_t first_argument,
                                  int user_mode) {
    if (context == 0 ||
        entry > 0xffffffffull ||
        stack > 0xffffffffull ||
        first_argument > 0xffffffffull) {
        return 0;
    }
    if (!user_mode) {
        return 0;
    }
    i386_context_init_user(context,
                           (uint32_t)entry,
                           (uint32_t)stack,
                           (uint32_t)first_argument);
    return 1;
}

void *hal_phys_direct_map(uint64_t phys_addr) {
    uint32_t phys;
    uint32_t virt;

    if (phys_addr > 0xffffffffull) {
        return 0;
    }
    phys = (uint32_t)phys_addr;
    if (phys < I386_PAGING_IDENTITY_LIMIT) {
        return (void *)(uintptr_t)phys;
    }
    for (uint32_t slot = 0u; slot < direct_map_slots_used; slot++) {
        uint32_t base = I386_HAL_DIRECT_MAP_BASE +
                        slot * I386_HAL_DIRECT_MAP_PAGES_PER_SLOT * I386_PAGE_SIZE;
        uint32_t mapped;

        if (i386_paging_translate(base, &mapped) &&
            mapped == (phys & ~(uint32_t)(I386_PAGE_SIZE - 1u))) {
            return (void *)(uintptr_t)(base + (phys & (I386_PAGE_SIZE - 1u)));
        }
    }
    if (direct_map_slots_used >= I386_HAL_DIRECT_MAP_SLOTS) {
        return 0;
    }
    virt = I386_HAL_DIRECT_MAP_BASE +
           direct_map_slots_used * I386_HAL_DIRECT_MAP_PAGES_PER_SLOT * I386_PAGE_SIZE;
    direct_map_slots_used++;
    phys &= ~(uint32_t)(I386_PAGE_SIZE - 1u);
    for (uint32_t i = 0u; i < I386_HAL_DIRECT_MAP_PAGES_PER_SLOT; i++) {
        if (!i386_paging_map_page(virt + i * I386_PAGE_SIZE,
                                  phys + i * I386_PAGE_SIZE,
                                  1,
                                  0)) {
            return 0;
        }
    }
    return (void *)(uintptr_t)(virt + ((uint32_t)phys_addr & (I386_PAGE_SIZE - 1u)));
}

int hal_phys_temporary_map(uint64_t phys_addr, uint32_t slot, void **virt_out) {
    if (virt_out == 0 || phys_addr > 0xffffffffull) {
        return 0;
    }
    return i386_paging_temporary_map((uint32_t)phys_addr, slot, virt_out);
}

void hal_phys_temporary_unmap(uint32_t slot) {
    i386_paging_temporary_unmap(slot);
}

void *hal_mmio_map(uint64_t phys_addr, uint64_t length) {
    uint32_t phys;
    uint32_t offset;
    uint32_t pages;
    uint32_t virt;

    if (phys_addr > 0xffffffffull || length == 0u) {
        return 0;
    }
    phys = (uint32_t)phys_addr & ~(uint32_t)(I386_PAGE_SIZE - 1u);
    offset = (uint32_t)phys_addr & (I386_PAGE_SIZE - 1u);
    pages = (uint32_t)((offset + length + I386_PAGE_SIZE - 1u) / I386_PAGE_SIZE);
    if (pages == 0u || mmio_map_pages_used + pages > I386_HAL_MMIO_MAP_PAGES) {
        return 0;
    }
    virt = I386_HAL_MMIO_MAP_BASE + mmio_map_pages_used * I386_PAGE_SIZE;
    mmio_map_pages_used += pages;
    for (uint32_t i = 0u; i < pages; i++) {
        if (!i386_paging_map_page(virt + i * I386_PAGE_SIZE,
                                  phys + i * I386_PAGE_SIZE,
                                  1,
                                  0)) {
            return 0;
        }
    }
    return (void *)(uintptr_t)(virt + offset);
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

void hal_cpu_trigger_triple_fault(void) {
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0u, 0u};

    __asm__ volatile("lidt %0\n\t"
                     "int3\n\t"
                     :
                     : "m"(null_idt)
                     : "memory");
}

uint64_t hal_cpu_read_tsc(void) {
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void hal_cpu_cpuid(uint32_t leaf,
                   uint32_t subleaf,
                   uint32_t *eax,
                   uint32_t *ebx,
                   uint32_t *ecx,
                   uint32_t *edx) {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;

    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(subleaf));
    if (eax != 0) {
        *eax = a;
    }
    if (ebx != 0) {
        *ebx = b;
    }
    if (ecx != 0) {
        *ecx = c;
    }
    if (edx != 0) {
        *edx = d;
    }
}

const char *hal_arch_name(void) {
    return "i386";
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

int hal_exception_frame_is_user(const struct exception_frame *frame) {
    (void)frame;
    return 0;
}

uint64_t hal_exception_frame_ip(const struct exception_frame *frame) {
    (void)frame;
    return 0u;
}

uint64_t hal_exception_frame_error_code(const struct exception_frame *frame) {
    (void)frame;
    return 0u;
}

uint64_t hal_page_fault_address(void) {
    uint32_t fault_addr;

    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    return fault_addr;
}

void hal_exception_snapshot(const struct exception_frame *frame,
                            struct hal_exception_snapshot *snapshot) {
    const uint64_t *raw = (const uint64_t *)frame;
    uint32_t control0 = 0;
    uint32_t fault_addr = 0;
    uint32_t paging_root = 0;
    uint32_t control4 = 0;
    uint32_t stack_segment = 0;
    uint32_t raw_count = 19u;
    uint32_t i;

    if (frame == 0 || snapshot == 0) {
        return;
    }

    snapshot->general[HAL_EXCEPTION_REGISTER_RAX] = frame->rax;
    snapshot->general[HAL_EXCEPTION_REGISTER_RBX] = frame->rbx;
    snapshot->general[HAL_EXCEPTION_REGISTER_RCX] = frame->rcx;
    snapshot->general[HAL_EXCEPTION_REGISTER_RDX] = frame->rdx;
    snapshot->general[HAL_EXCEPTION_REGISTER_RSI] = frame->rsi;
    snapshot->general[HAL_EXCEPTION_REGISTER_RDI] = frame->rdi;
    snapshot->general[HAL_EXCEPTION_REGISTER_RBP] = frame->rbp;
    snapshot->general[HAL_EXCEPTION_REGISTER_R8] = frame->r8;
    snapshot->general[HAL_EXCEPTION_REGISTER_R9] = frame->r9;
    snapshot->general[HAL_EXCEPTION_REGISTER_R10] = frame->r10;
    snapshot->general[HAL_EXCEPTION_REGISTER_R11] = frame->r11;
    snapshot->general[HAL_EXCEPTION_REGISTER_R12] = frame->r12;
    snapshot->general[HAL_EXCEPTION_REGISTER_R13] = frame->r13;
    snapshot->general[HAL_EXCEPTION_REGISTER_R14] = frame->r14;
    snapshot->general[HAL_EXCEPTION_REGISTER_R15] = frame->r15;
    snapshot->error_code = frame->error_code;
    snapshot->instruction_pointer = frame->instruction_pointer;
    snapshot->code_selector = frame->cs;
    snapshot->flags = frame->rflags;
    snapshot->user_mode = hal_exception_frame_is_user(frame) ? 1u : 0u;
    __asm__ volatile("mov %%cr0, %0" : "=r"(control0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    __asm__ volatile("mov %%cr3, %0" : "=r"(paging_root));
    __asm__ volatile("mov %%cr4, %0" : "=r"(control4));
    __asm__ volatile("mov %%ss, %0" : "=r"(stack_segment));
    snapshot->control0 = control0;
    snapshot->fault_address = fault_addr;
    snapshot->paging_root = paging_root;
    snapshot->control4 = control4;
    if (snapshot->user_mode) {
        snapshot->stack_pointer = raw[19];
        snapshot->stack_selector = raw[20];
        raw_count = 21u;
    } else {
        snapshot->stack_pointer = (uint64_t)(uintptr_t)(frame + 1);
        snapshot->stack_selector = stack_segment;
    }
    snapshot->raw_word_count = raw_count < HAL_EXCEPTION_RAW_WORD_MAX ?
                               raw_count :
                               HAL_EXCEPTION_RAW_WORD_MAX;
    for (i = 0; i < snapshot->raw_word_count; i++) {
        snapshot->raw_words[i] = raw[i];
    }
}

int hal_syscall_frame_is_user(const struct syscall_frame *frame) {
    return frame != 0 && (frame->cs & 0x3u) == 0x3u;
}

uint64_t hal_syscall_frame_ip(const struct syscall_frame *frame) {
    return frame != 0 ? frame->instruction_pointer : 0u;
}

uint64_t hal_syscall_frame_sp(const struct syscall_frame *frame) {
    return frame != 0 ? frame->stack_pointer : 0u;
}
