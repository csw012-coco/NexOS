#include "hal/x86/platform.h"
#include "arch/x86/x86_64/mm/pmm.h"
#include "bootx/bootx.h"
#include "drivers/bus/acpi.h"
#include "drivers/bus/ioapic.h"
#include "drivers/bus/lapic.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/vga.h"
#include "kernel/internal/mem/vmm_diag.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/context.h"
#include "kernel/public/sys/syscall.h"
#include "kernel/public/sys/syscall_request.h"
#include "kernel/internal/core/kernel_panic_internal.h"

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

void hal_display_load_font(const struct bootx_boot_info *boot_info) {
    framebuffer_display_load_font_from_boot_modules(boot_info);
}

void hal_paging_init(uint64_t kernel_phys_addr) {
    hal_x86_paging_init_impl(kernel_phys_addr);
}

void hal_platform_init(const struct hal_interrupt_handlers *handlers) {
    hal_x86_platform_init_impl(handlers);
}

int hal_pmm_init_from_boot(const struct bootx_boot_info *boot_info,
                           uint64_t kernel_phys_addr) {
    return x86_64_pmm_init(boot_info, kernel_phys_addr);
}

int hal_paging_enabled(void) {
    return 1;
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

uint64_t hal_paging_clone_root_cow(uint64_t source_cr3) {
    return hal_x86_paging_clone_root_cow_impl(source_cr3);
}

int hal_paging_resolve_cow_fault(uint64_t root_cr3,
                                 uint64_t fault_addr,
                                 uint64_t error_code) {
    return hal_x86_paging_resolve_cow_fault_impl(root_cr3, fault_addr, error_code);
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

int hal_paging_set_write_combining(uint64_t virt_addr, uint64_t size) {
    return hal_x86_paging_set_write_combining_impl(virt_addr, size);
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

int hal_paging_get_mapping_info_in_root(uint64_t root,
                                        uint64_t virt_addr,
                                        uint64_t *phys_addr,
                                        uint64_t *flags) {
    return vmm_query_mapping_in_context(root, virt_addr, phys_addr, flags);
}

void hal_paging_log_init_exec_failure(const struct hal_boot_trace_ops *ops, void *ctx) {
    struct vmm_page_fault_trace trace;
    struct vmm_page_clone_trace clone_trace;
    uint64_t phys = 0;
    uint64_t flags = 0;
    uint64_t pml4e = 0;
    uint64_t pdpte = 0;
    uint64_t pde = 0;
    uint64_t pte = 0;

    if (ops == 0 || ops->text == 0 || ops->hex64 == 0) {
        return;
    }

    vmm_get_page_fault_trace(&trace);
    vmm_get_page_clone_trace(&clone_trace);
    ops->hex64(ctx, "kernel: cur root", vmm_current_root());
    ops->hex64(ctx, "kernel: cl src", clone_trace.source_cr3);
    ops->hex64(ctx, "kernel: cl dst", clone_trace.clone_cr3);
    ops->hex64(ctx, "kernel: cl s e0", clone_trace.source_pml4e0);
    ops->hex64(ctx, "kernel: cl s511", clone_trace.source_pml4e511);
    ops->hex64(ctx, "kernel: cl d e0", clone_trace.clone_pml4e0);
    ops->hex64(ctx, "kernel: cl d511", clone_trace.clone_pml4e511);
    ops->hex64(ctx, "kernel: cl fail v", clone_trace.fail_virt);
    ops->hex64(ctx, "kernel: cl fail p", clone_trace.fail_phys);
    ops->hex64(ctx, "kernel: cl fail s", clone_trace.fail_stage);
    ops->hex64(ctx, "kernel: sw req", trace.requested_cr3);
    ops->hex64(ctx, "kernel: sw act", trace.actual_cr3);
    ops->hex64(ctx, "kernel: sw rej", trace.reject_flags);
    ops->hex64(ctx, "kernel: sw ip", trace.current_rip);
    ops->hex64(ctx, "kernel: sw sp", trace.current_rsp);
    if (vmm_query_mapping_in_context(trace.requested_cr3, trace.current_rip, &phys, &flags)) {
        ops->hex64(ctx, "kernel: req ip phys", phys);
        ops->hex64(ctx, "kernel: req ip flg", flags);
    } else {
        ops->text(ctx, "kernel: req ip unmapped");
    }
    (void)vmm_query_page_walk_in_context(trace.requested_cr3,
                                         trace.current_rip,
                                         &pml4e,
                                         &pdpte,
                                         &pde,
                                         &pte);
    ops->hex64(ctx, "kernel: req ip lvl0", pml4e);
    ops->hex64(ctx, "kernel: req ip lvl1", pdpte);
    ops->hex64(ctx, "kernel: req ip lvl2", pde);
    ops->hex64(ctx, "kernel: req ip lvl3", pte);
    if (vmm_query_mapping_in_context(trace.requested_cr3, trace.current_rsp, &phys, &flags)) {
        ops->hex64(ctx, "kernel: req sp phys", phys);
        ops->hex64(ctx, "kernel: req sp flg", flags);
    } else {
        ops->text(ctx, "kernel: req sp unmapped");
    }
    pml4e = 0;
    pdpte = 0;
    pde = 0;
    pte = 0;
    (void)vmm_query_page_walk_in_context(trace.requested_cr3,
                                         trace.current_rsp,
                                         &pml4e,
                                         &pdpte,
                                         &pde,
                                         &pte);
    ops->hex64(ctx, "kernel: req sp lvl0", pml4e);
    ops->hex64(ctx, "kernel: req sp lvl1", pdpte);
    ops->hex64(ctx, "kernel: req sp lvl2", pde);
    ops->hex64(ctx, "kernel: req sp lvl3", pte);
    ops->hex64(ctx, "kernel: init final swrej", trace.reject_flags);
}

void hal_paging_log_panic_entry(const struct hal_boot_trace_ops *ops,
                                void *ctx,
                                uint64_t entry) {
    uint64_t entry_phys = 0;
    uint64_t entry_flags = 0;
    uint64_t pml4e = 0;
    uint64_t pdpte = 0;
    uint64_t pde = 0;
    uint64_t pte = 0;

    if (ops == 0 || ops->text == 0 || ops->hex64 == 0 || entry == 0) {
        return;
    }

    if (vmm_query_info(entry, &entry_phys, &entry_flags)) {
        ops->hex64(ctx, "ENTRY MAP PHYS   : ", entry_phys);
        ops->hex64(ctx, "ENTRY MAP FLAGS  : ", entry_flags);
    } else {
        ops->text(ctx, "ENTRY MAP        : <unmapped>");
    }

    if (vmm_query_page_walk(entry, &pml4e, &pdpte, &pde, &pte)) {
        ops->hex64(ctx, "ENTRY PML4E      : ", pml4e);
        ops->hex64(ctx, "ENTRY PDPTE      : ", pdpte);
        ops->hex64(ctx, "ENTRY PDE        : ", pde);
        ops->hex64(ctx, "ENTRY PTE        : ", pte);
    }
}

void hal_paging_log_panic_target_entry(const struct hal_boot_trace_ops *ops,
                                       void *ctx,
                                       uint64_t target_root,
                                       uint64_t entry) {
    uint64_t entry_phys = 0;
    uint64_t entry_flags = 0;
    uint64_t pml4e = 0;
    uint64_t pdpte = 0;
    uint64_t pde = 0;
    uint64_t pte = 0;

    if (ops == 0 || ops->text == 0 || ops->hex64 == 0 ||
        target_root == 0 || entry == 0) {
        return;
    }

    ops->hex64(ctx, "TARGET USER ROOT : ", target_root);
    if (vmm_query_mapping_in_context(target_root, entry, &entry_phys, &entry_flags)) {
        ops->hex64(ctx, "TARGET MAP PHYS  : ", entry_phys);
        ops->hex64(ctx, "TARGET MAP FLAGS : ", entry_flags);
    } else {
        ops->text(ctx, "TARGET ENTRY MAP : <unmapped>");
    }

    if (vmm_query_page_walk_in_context(target_root, entry, &pml4e, &pdpte, &pde, &pte)) {
        ops->hex64(ctx, "TARGET PML4E     : ", pml4e);
        ops->hex64(ctx, "TARGET PDPTE     : ", pdpte);
        ops->hex64(ctx, "TARGET PDE       : ", pde);
        ops->hex64(ctx, "TARGET PTE       : ", pte);
    }
}

void hal_paging_log_panic_switch_trace(const struct hal_boot_trace_ops *ops,
                                       void *ctx,
                                       uint64_t entry) {
    struct vmm_page_fault_trace trace = {0};

    if (ops == 0 || ops->text == 0 || ops->hex64 == 0) {
        return;
    }

    vmm_get_page_fault_trace(&trace);
    if (trace.requested_cr3 == 0 && trace.previous_cr3 == 0 && trace.actual_cr3 == 0) {
        return;
    }

    ops->text(ctx, "--- CR3 SWITCH TRACE ---");
    ops->hex64(ctx, "REQ              : ", trace.requested_cr3);
    ops->hex64(ctx, "PREV             : ", trace.previous_cr3);
    ops->hex64(ctx, "ACTUAL           : ", trace.actual_cr3);
    ops->hex64(ctx, "FLAGS            : ", trace.reject_flags);
    ops->hex64(ctx, "CHECKPOINT RIP   : ", trace.current_rip);
    ops->hex64(ctx, "CHECKPOINT RSP   : ", trace.current_rsp);
    ops->hex64(ctx, "CUR CR3          : ", vmm_get_current_cr3());
    ops->hex64(ctx, "ENTRY            : ", entry);

    if (trace.reject_flags != 0) {
        if (trace.reject_flags & VMM_SWITCH_REJECT_ZERO) {
            ops->text(ctx, "REJECT REASON    : ZERO");
        }
        if (trace.reject_flags & VMM_SWITCH_REJECT_RIP_UNMAPPED) {
            ops->text(ctx, "REJECT REASON    : RIP");
        }
        if (trace.reject_flags & VMM_SWITCH_REJECT_RSP_UNMAPPED) {
            ops->text(ctx, "REJECT REASON    : RSP");
        }
    }
}

void hal_paging_log_panic_summary(const struct hal_boot_trace_ops *ops,
                                  void *ctx,
                                  uint64_t target_root,
                                  uint64_t entry) {
    struct vmm_page_fault_trace trace = {0};
    struct vmm_page_walk_info walk = {0};

    if (ops == 0 || ops->hex64 == 0) {
        return;
    }

    vmm_get_page_fault_trace(&trace);
    (void)vmm_query_page_walk_full(target_root, entry, &walk);
    ops->hex64(ctx, "ARCH SW FLAGS    : ", trace.reject_flags);
    ops->hex64(ctx, "ARCH WALK ROOT0  : ", walk.pml4_phys);
    ops->hex64(ctx, "ARCH WALK ROOT1  : ", walk.pdpt_phys);
    ops->hex64(ctx, "ARCH WALK TAB0   : ", walk.pd_phys);
    ops->hex64(ctx, "ARCH WALK TAB1   : ", walk.pt_phys);
}

int hal_process_context_init_user(struct process_context *context,
                                  uint64_t entry,
                                  uint64_t stack,
                                  uint64_t first_argument,
                                  int user_mode) {
    if (context == 0 || !user_mode) {
        return 0;
    }
    process_context_reset(context);
    context->registers[PROCESS_CONTEXT_ARG0] = first_argument;
    context->instruction_pointer = entry;
    context->stack_pointer = stack;
    context->flags = 0x202u;
    context->code_selector = GDT64_USER_CODE;
    context->stack_selector = GDT64_USER_DATA;
    context->user_mode = 1u;
    return 1;
}

void *hal_phys_direct_map(uint64_t phys_addr) {
    return hal_x86_paging_phys_direct_map_impl(phys_addr);
}

int hal_phys_temporary_map(uint64_t phys_addr, uint32_t slot, void **virt_out) {
    (void)slot;
    if (virt_out == 0) {
        return 0;
    }
    *virt_out = hal_phys_direct_map(phys_addr);
    return *virt_out != 0;
}

void hal_phys_temporary_unmap(uint32_t slot) {
    (void)slot;
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
    if (ioapic_irq_enabled(irq)) {
        lapic_send_eoi();
        return;
    }
    hal_x86_irq_ack_impl(irq);
}

void hal_irq_set_mask(uint8_t irq, int masked) {
    if (ioapic_irq_enabled(irq)) {
        (void)ioapic_set_irq_mask(irq, masked);
        hal_x86_irq_set_mask_impl(irq, 1);
        return;
    }
    hal_x86_irq_set_mask_impl(irq, masked);
}

int hal_irq_route(uint8_t irq, struct hal_irq_route *out) {
    struct acpi_irq_override_route route;
    int overridden;

    if (out == 0 || irq >= 16u) {
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
    return hal_x86_keyboard_read_scancode_impl();
}

int hal_keyboard_inject_scancode(uint8_t scancode) {
    (void)scancode;
    return 0;
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

void hal_cpu_wait_for_interrupt(void) {
    hal_x86_cpu_wait_for_interrupt_impl();
}

void hal_cpu_wait_for_event(void) {
    hal_x86_cpu_wait_for_event_impl();
}

void hal_cpu_relax(void) {
    hal_x86_cpu_relax_impl();
}

void hal_cpu_enable_sse(void) {
    hal_x86_cpu_enable_sse_impl();
}

void hal_cpu_trigger_triple_fault(void) {
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = {0u, 0u};

    __asm__ __volatile__("lidt %0\n\t"
                         "int3\n\t"
                         :
                         : "m"(null_idt)
                         : "memory");
}

void hal_fpu_state_init(void *state) {
    hal_x86_fpu_state_init_impl(state);
}

void hal_fpu_state_save(void *state) {
    hal_x86_fpu_state_save_impl(state);
}

void hal_fpu_state_restore(const void *state) {
    hal_x86_fpu_state_restore_impl(state);
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

const char *hal_arch_name(void) {
    return "x86_64";
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

int hal_exception_frame_is_user(const struct exception_frame *frame) {
    return frame != 0 && (frame->cs & 0x3u) == 0x3u;
}

uint64_t hal_exception_frame_ip(const struct exception_frame *frame) {
    return frame != 0 ? frame->instruction_pointer : 0u;
}

uint64_t hal_exception_frame_error_code(const struct exception_frame *frame) {
    return frame != 0 ? frame->error_code : 0u;
}

uint64_t hal_page_fault_address(void) {
    uint64_t fault_addr;

    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
    return fault_addr;
}

void hal_exception_snapshot(const struct exception_frame *frame,
                            struct hal_exception_snapshot *snapshot) {
    const uint64_t *raw = (const uint64_t *)frame;
    uint64_t stack_segment = 0;
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
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(snapshot->control0));
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(snapshot->fault_address));
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(snapshot->paging_root));
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(snapshot->control4));
    __asm__ __volatile__("mov %%ss, %0" : "=r"(stack_segment));
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

void hal_syscall_decode_request(const struct syscall_frame *frame,
                                struct kernel_syscall_request *request) {
    if (frame == 0 || request == 0) {
        return;
    }
    request->number = (uint32_t)frame->rax;
    request->user_bits = 64u;
    request->args[0] = frame->rbx;
    request->args[1] = frame->rcx;
    request->args[2] = frame->rdx;
    request->args[3] = frame->rsi;
    request->args[4] = frame->rdi;
    request->args[5] = frame->rbp;
    request->instruction_pointer = frame->instruction_pointer;
    request->stack_pointer = frame->stack_pointer;
}
