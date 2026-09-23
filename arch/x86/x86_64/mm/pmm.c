#include "arch/x86/x86_64/mm/pmm.h"
#include "janus/janus.h"

int x86_64_pmm_init(const struct janus_boot_info *boot_info,
                    uint64_t kernel_phys_addr) {
    const struct janus_memmap_entry *memmap;

    if (boot_info == 0 ||
        boot_info->memmap == 0 ||
        boot_info->memmap_count == 0u) {
        return 0;
    }

    memmap = (const struct janus_memmap_entry *)(uintptr_t)boot_info->memmap;
    pmm_init(memmap,
             boot_info->memmap_count,
             kernel_phys_addr,
             boot_info->kernel_phys_size);
    return pmm_free_pages() != 0u;
}
