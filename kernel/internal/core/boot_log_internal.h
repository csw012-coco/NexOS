#pragma once

#include <stdint.h>
#include "bootx/bootx.h"

void kernel_boot_log_stage(const char *stage);
void kernel_boot_log_system(const char *arch);
void kernel_boot_log_boot_info_common(const struct bootx_boot_info *boot_info);
void kernel_boot_log_console(const struct bootx_console_info *console);
void kernel_boot_log_memmap_summary(uint64_t usable_total, uint32_t entries);
void kernel_boot_log_paging_root(uint64_t root, uint32_t nx_supported, uint32_t nx_enabled);
void kernel_boot_log_pmm(uint64_t total_pages,
                         uint64_t free_pages,
                         uint64_t used_pages,
                         uint64_t dropped_pages);
void kernel_boot_log_framebuffer(uint64_t addr, uint64_t size, int write_combining_known, uint32_t write_combining);
