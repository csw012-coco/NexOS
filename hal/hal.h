#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"

struct hal_interrupt_handlers {
    void (*divide_error)(void);
    void (*double_fault)(void);
    void (*invalid_opcode)(void);
    void (*general_protection_fault)(void);
    void (*page_fault)(void);
    void (*irq0)(void);
    void (*irq1)(void);
    void (*syscall)(void);
};

struct bootx_console_info;
struct bootx_boot_info;

enum {
    HAL_TEXT_WIDTH = 240,
    HAL_TEXT_HEIGHT = 80
};

void hal_paging_init(uint64_t kernel_phys_addr);
void hal_display_load_font(const struct bootx_boot_info *boot_info);
void hal_display_init(const struct bootx_console_info *console);
void hal_platform_init(const struct hal_interrupt_handlers *handlers);
uint64_t hal_paging_current_root(void);
void hal_paging_switch_root(uint64_t cr3);
uint64_t hal_paging_create_user_root(void);
void hal_paging_allow_user_page(uint64_t addr);
void hal_paging_allow_user_range(uint64_t start, uint64_t end);
void hal_paging_set_supervisor_range(uint64_t start, uint64_t end);
int hal_paging_map_page(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable);
int hal_paging_map_page_with_exec(uint64_t virt_addr,
                                  uint64_t phys_addr,
                                  int user_accessible,
                                  int writable,
                                  int executable);
int hal_paging_unmap_page(uint64_t virt_addr, uint64_t *phys_addr);
int hal_paging_get_mapping(uint64_t virt_addr, uint64_t *phys_addr);
int hal_paging_get_mapping_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags);
void *hal_phys_direct_map(uint64_t phys_addr);
void hal_timer_init(uint32_t pit_hz);
void hal_timer_notify_tick(void);
uint32_t hal_timer_current_ticks(void);
uint32_t hal_timer_hz(void);
void hal_irq_ack(uint8_t irq);
void hal_irq_set_mask(uint8_t irq, int masked);
uint8_t hal_keyboard_read_scancode(void);
uint16_t hal_display_read_cell(uint16_t row, uint16_t col);
void hal_display_write_cell(uint16_t row, uint16_t col, uint16_t value);
void hal_display_clear_row(uint16_t row, uint8_t color);
void hal_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch);
void hal_display_enable_cursor(uint8_t start, uint8_t end);
void hal_display_set_cursor(uint16_t row, uint16_t col);
uint16_t hal_display_text_columns(void);
uint16_t hal_display_text_rows(void);
uint8_t hal_io_in8(uint16_t port);
uint16_t hal_io_in16(uint16_t port);
void hal_io_out8(uint16_t port, uint8_t value);
void hal_io_out16(uint16_t port, uint16_t value);
void hal_cpu_cli(void);
void hal_cpu_sti(void);
void hal_cpu_halt(void);
uint64_t hal_cpu_current_sp(void);
uint64_t hal_cpu_read_tsc(void);
void hal_cpu_cpuid(uint32_t leaf,
                   uint32_t subleaf,
                   uint32_t *eax,
                   uint32_t *ebx,
                   uint32_t *ecx,
                   uint32_t *edx);
void hal_usermode_enter(uint64_t entry, uint64_t user_stack);
void hal_usermode_resume(const struct syscall_frame *frame);
uint64_t hal_kernel_stack_top(void);
void hal_set_kernel_stack_top(uint64_t rsp0);
