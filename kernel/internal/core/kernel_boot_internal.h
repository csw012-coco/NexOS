#pragma once

#include <stdint.h>
#include "bootx/bootx.h"

int kernel_boot_info_valid(const struct bootx_boot_info *boot_info);
uint64_t kernel_detect_phys_base(const struct bootx_boot_info *boot_info);
void kernel_log_boot_info(const struct bootx_boot_info *boot_info);
void kernel_log_paging_info(void);
void kernel_log_memmap(const struct bootx_memmap_entry *memmap, uint32_t memmap_count);
void kernel_log_pmm_info(void);
void kernel_log_pci_info(void);
void kernel_log_ac97_info(void);
void kernel_log_hda_info(void);
void kernel_log_rtl8139_info(void);
void kernel_log_block_devices(void);
void kernel_init_storage_devices(const struct bootx_boot_info *boot_info);
struct vfs *kernel_bootstrap_vfs(void);
void kernel_reserve_boot_modules(const struct bootx_boot_info *boot_info);
int kernel_extract_init_path(const char *cmdline, char *out, uint32_t out_size);
