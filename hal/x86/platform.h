#pragma once

#include <stdint.h>

#include "arch/x86/gdt64.h"
#include "arch/x86/idt64.h"
#include "arch/x86/io.h"
#include "arch/x86/paging.h"
#include "arch/x86/usermode.h"

#include "hal/hal.h"

void hal_x86_paging_init_impl(uint64_t kernel_phys_addr);
void hal_x86_cpu_cli_impl(void);
void hal_x86_cpu_sti_impl(void);
void hal_x86_cpu_halt_impl(void);
uint64_t hal_x86_cpu_current_sp_impl(void);
uint64_t hal_x86_cpu_read_tsc_impl(void);
void hal_x86_cpu_cpuid_impl(uint32_t leaf,
                            uint32_t subleaf,
                            uint32_t *eax,
                            uint32_t *ebx,
                            uint32_t *ecx,
                            uint32_t *edx);
void hal_x86_platform_init_impl(const struct hal_interrupt_handlers *handlers);
void hal_x86_timer_init_impl(uint32_t pit_hz);
void hal_x86_irq_ack_impl(uint8_t irq);
void hal_x86_irq_set_mask_impl(uint8_t irq, int masked);
uint8_t hal_x86_keyboard_read_scancode_impl(void);
uint8_t hal_x86_io_in8_impl(uint16_t port);
uint16_t hal_x86_io_in16_impl(uint16_t port);
void hal_x86_io_out8_impl(uint16_t port, uint8_t value);
void hal_x86_io_out16_impl(uint16_t port, uint16_t value);
uint64_t hal_x86_paging_current_root_impl(void);
void hal_x86_paging_switch_root_impl(uint64_t cr3);
uint64_t hal_x86_paging_create_user_root_impl(void);
void hal_x86_paging_allow_user_page_impl(uint64_t addr);
void hal_x86_paging_allow_user_range_impl(uint64_t start, uint64_t end);
void hal_x86_paging_set_supervisor_range_impl(uint64_t start, uint64_t end);
int hal_x86_paging_map_page_impl(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable);
int hal_x86_paging_map_page_with_exec_impl(uint64_t virt_addr,
                                           uint64_t phys_addr,
                                           int user_accessible,
                                           int writable,
                                           int executable);
int hal_x86_paging_unmap_page_impl(uint64_t virt_addr, uint64_t *phys_addr);
int hal_x86_paging_get_mapping_impl(uint64_t virt_addr, uint64_t *phys_addr);
int hal_x86_paging_get_mapping_info_impl(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags);
void *hal_x86_paging_phys_direct_map_impl(uint64_t phys_addr);
void hal_x86_usermode_enter_impl(uint64_t entry, uint64_t user_stack);
void hal_x86_usermode_resume_impl(const struct syscall_frame *frame);
uint64_t hal_x86_kernel_stack_top_impl(void);
void hal_x86_set_kernel_stack_top_impl(uint64_t rsp0);
uint32_t hal_x86_display_cell_height_impl(void);
void hal_x86_display_bitblt_impl(uint32_t src_x,
                                 uint32_t src_y,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t dst_x,
                                 uint32_t dst_y);
