#pragma once

#include <stdint.h>
#include "janus/janus.h"

int kernel_boot_info_valid(const struct janus_boot_info *boot_info);
uint64_t kernel_detect_phys_base(const struct janus_boot_info *boot_info);
void kernel_log_boot_info(const struct janus_boot_info *boot_info);
void kernel_log_paging_info(void);
void kernel_log_memmap(const struct janus_memmap_entry *memmap, uint32_t memmap_count);
void kernel_log_pmm_info(void);
void kernel_log_pci_info(void);
void kernel_log_ac97_info(void);
void kernel_log_hda_info(void);
void kernel_log_rtl8139_info(void);
void kernel_log_ata_info(void);
void kernel_log_block_devices(void);
void kernel_init_storage_devices(const struct janus_boot_info *boot_info);
struct vfs *kernel_bootstrap_vfs(const struct janus_boot_info *boot_info);
int kernel_apply_root_cmdline(struct vfs *vfs, const struct janus_boot_info *boot_info);
void kernel_reserve_boot_modules(const struct janus_boot_info *boot_info);
int kernel_extract_init_path(const char *cmdline, char *out, uint32_t out_size);
