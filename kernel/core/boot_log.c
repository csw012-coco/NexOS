#include "kernel/internal/core/boot_log_internal.h"
#include "kernel/public/core/kprint.h"

void kernel_boot_log_stage(const char *stage) {
    if (stage != 0) {
        kprint("kernel: %s\n", stage);
    }
}

void kernel_boot_log_system(const char *arch) {
    kprint("system: NexOS %s\n", arch != 0 ? arch : "unknown");
}

void kernel_boot_log_boot_info_common(const struct bootx_boot_info *boot_info) {
    if (boot_info == 0) {
        return;
    }
    kprint("boot: drive=0x%x part_lba=%u part_sectors=%u modules=%u\n",
           (uint32_t)boot_info->boot_drive,
           boot_info->partition_lba,
           boot_info->partition_sectors,
           boot_info->module_count);
}

void kernel_boot_log_console(const struct bootx_console_info *console) {
    if (console == 0) {
        return;
    }
    if (console->type == BOOTX_CONSOLE_FRAMEBUFFER) {
        uint32_t cell_height =
            (console->height < 25u * 16u || console->width < 80u * 8u) ? 8u : 16u;

        kprint("boot: console=framebuffer %ux%u pitch=%u bpp=%u text=%ux%u\n",
               console->width,
               console->height,
               console->pitch,
               (uint32_t)console->framebuffer_bpp,
               (uint32_t)console->width / 8u,
               (uint32_t)console->height / cell_height);
    } else {
        kprint("boot: console=text %ux%u color=%u\n",
               (uint32_t)console->text_columns,
               (uint32_t)console->text_rows,
               (uint32_t)console->text_color);
    }
}

void kernel_boot_log_memmap_summary(uint64_t usable_total, uint32_t entries) {
    kprint("memmap: usable_total=%lx bytes entries=%u\n", usable_total, entries);
}

void kernel_boot_log_paging_root(uint64_t root, uint32_t nx_supported, uint32_t nx_enabled) {
    kprint("paging: root=%lx nx_supported=%u nx_enabled=%u\n",
           root,
           nx_supported,
           nx_enabled);
}

void kernel_boot_log_pmm(uint64_t total_pages,
                         uint64_t free_pages,
                         uint64_t used_pages,
                         uint64_t dropped_pages) {
    kprint("pmm: total=%lx free=%lx used=%lx dropped=%lx pages\n",
           total_pages,
           free_pages,
           used_pages,
           dropped_pages);
}

void kernel_boot_log_framebuffer(uint64_t addr,
                                 uint64_t size,
                                 int write_combining_known,
                                 uint32_t write_combining) {
    if (write_combining_known) {
        kprint("paging: framebuffer=%lx size=%lx write_combining=%u\n",
               addr,
               size,
               write_combining);
    } else {
        kprint("paging: framebuffer=%lx size=%lx\n", addr, size);
    }
}
