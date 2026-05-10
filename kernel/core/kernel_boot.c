#include <stdint.h>
#include "bootx/bootx.h"
#include "block/blockdev.h"
#include "drivers/audio/ac97.h"
#include "drivers/bus/pci.h"
#include "drivers/net/rtl8139.h"
#include "drivers/storage/ata.h"
#include "drivers/storage/ahci.h"
#include "drivers/storage/ramdisk.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/xhci.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/mem/vmm.h"
#include "lib/string.h"

extern char __kernel_start[];
extern char __kernel_text_start[];
extern char __kernel_data_start[];

static struct vfs g_kernel_vfs;
enum {
    KERNEL_PAGE_FLAG_PRESENT = 1ull << 0,
    KERNEL_PAGE_FLAG_RW = 1ull << 1,
    KERNEL_PAGE_FLAG_USER = 1ull << 2,
    KERNEL_PAGE_FLAG_NX = 1ull << 63
};

static const char *kernel_memmap_type_name(uint32_t type) {
    switch (type) {
        case BOOTX_MEMMAP_USABLE:
            return "usable";
        case BOOTX_MEMMAP_RESERVED:
            return "reserved";
        case BOOTX_MEMMAP_ACPI_RECLAIMABLE:
            return "acpi reclaim";
        case BOOTX_MEMMAP_ACPI_NVS:
            return "acpi nvs";
        case BOOTX_MEMMAP_BAD:
            return "bad";
        case BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return "boot reclaim";
        default:
            return "unknown";
    }
}

void kernel_log_boot_info(const struct bootx_boot_info *boot_info) {
    uint64_t detected_phys = 0;
    uint64_t mapped_phys = 0;
    int mapped = 0;

    if (boot_info == 0) {
        return;
    }

    kprint("system: NexOS x86_64\n");
    kprint("bootx: magic=%x version=%u size=%u\n",
           boot_info->hdr.magic,
           (uint32_t)boot_info->hdr.version,
           (uint32_t)boot_info->hdr.size);
    kprint("boot: drive=0x%x part_lba=%u part_sectors=%u modules=%u cmdline=%x\n",
           (uint32_t)boot_info->boot_drive,
           boot_info->partition_lba,
           boot_info->partition_sectors,
           boot_info->module_count,
           boot_info->cmdline);
    kprint("boot: kernel_phys=%lx kernel_size=%lx entry=%lx\n",
           boot_info->kernel_phys_addr,
           boot_info->kernel_phys_size,
           boot_info->kernel_entry);
    if (boot_info->console.type == BOOTX_CONSOLE_FRAMEBUFFER) {
        kprint("boot: console=framebuffer %ux%u pitch=%u bpp=%u text=%ux%u\n",
               boot_info->console.width,
               boot_info->console.height,
               boot_info->console.pitch,
               (uint32_t)boot_info->console.framebuffer_bpp,
               (uint32_t)boot_info->console.width / 8u,
               (uint32_t)boot_info->console.height /
                   ((boot_info->console.height < 25u * 16u || boot_info->console.width < 80u * 8u) ? 8u : 16u));
    } else {
        kprint("boot: console=text %ux%u color=%u\n",
               (uint32_t)boot_info->console.text_columns,
               (uint32_t)boot_info->console.text_rows,
               (uint32_t)boot_info->console.text_color);
    }
    mapped = vmm_query((uint64_t)(uintptr_t)__kernel_start, &mapped_phys);
    detected_phys = kernel_detect_phys_base(boot_info);
    kprint("boot: kernel_phys(proto)=%lx kernel_phys(detect)=%lx kernel_phys(map)=%lx mapped=%u\n",
           boot_info->kernel_phys_addr,
           detected_phys,
           mapped ? mapped_phys : 0,
           (uint32_t)mapped);
}

static void kernel_log_page_mapping(const char *label, uint64_t virt_addr) {
    uint64_t phys = 0;
    uint64_t flags = 0;
    int mapped = vmm_query_info(virt_addr, &phys, &flags);

    kprint("paging: %s virt=%lx phys=%lx mapped=%u user=%u write=%u nx=%u flags=%lx\n",
           label,
           virt_addr,
           mapped ? phys : 0,
           (uint32_t)mapped,
           mapped ? (uint32_t)((flags & KERNEL_PAGE_FLAG_USER) != 0) : 0u,
           mapped ? (uint32_t)((flags & KERNEL_PAGE_FLAG_RW) != 0) : 0u,
           mapped ? (uint32_t)((flags & KERNEL_PAGE_FLAG_NX) != 0) : 0u,
           mapped ? flags : 0);
}

void kernel_log_paging_info(void) {
    kprint("paging: root=%lx nx_supported=%u nx_enabled=%u\n",
           vmm_current_root(),
           (uint32_t)vmm_cpu_supports_nx(),
           (uint32_t)vmm_nx_enabled());
    kernel_log_page_mapping("kernel_text", (uint64_t)(uintptr_t)__kernel_text_start);
    kernel_log_page_mapping("kernel_data", (uint64_t)(uintptr_t)__kernel_data_start);
    kernel_log_page_mapping("kernel_start", (uint64_t)(uintptr_t)__kernel_start);
}

void kernel_log_memmap(const struct bootx_memmap_entry *memmap, uint32_t memmap_count) {
    uint64_t total_usable = 0;
    uint32_t i;

    if (memmap == 0) {
        kprint("memmap: unavailable\n");
        return;
    }

    kprint("memmap: entries=%u\n", memmap_count);
    for (i = 0; i < memmap_count; i++) {
        if (memmap[i].type == BOOTX_MEMMAP_USABLE) {
            total_usable += memmap[i].length;
        }
        kprint("memmap[%u]: base=%lx len=%lx type=%s\n",
               i,
               memmap[i].base,
               memmap[i].length,
               kernel_memmap_type_name(memmap[i].type));
    }
    kprint("memmap: usable_total=%lx bytes\n", total_usable);
}

void kernel_log_pmm_info(void) {
    kprint("pmm: total=%u free=%u used=%u dropped=%u\n",
           pmm_total_pages(),
           pmm_free_pages(),
           pmm_used_pages(),
           pmm_dropped_pages());
}

void kernel_log_pci_info(void) {
    struct pci_ide_controller ide;

    if (!pci_find_ide_controller(&ide)) {
        kprint("pci: ide controller not found\n");
        return;
    }

    kprint("pci: ide bdf=%u:%u.%u vendor=%x device=%x prog_if=%x\n",
           (uint32_t)ide.bus,
           (uint32_t)ide.slot,
           (uint32_t)ide.function,
           (uint32_t)ide.vendor_id,
           (uint32_t)ide.device_id,
           (uint32_t)ide.prog_if);
    kprint("pci: ide bar0=%x bar1=%x bar2=%x bar3=%x bar4=%x\n",
           ide.bar0,
           ide.bar1,
           ide.bar2,
           ide.bar3,
           ide.bar4);
}

void kernel_log_ac97_info(void) {
    struct ac97_status status;

    if (!ac97_query_status(&status) || !status.present) {
        kprint("ac97: controller not found\n");
        return;
    }

    kprint("ac97: bdf=%u:%u.%u vendor=%x device=%x irq=%u pin=%u prog_if=%x\n",
           (uint32_t)status.bus,
           (uint32_t)status.slot,
           (uint32_t)status.function,
           (uint32_t)status.vendor_id,
           (uint32_t)status.device_id,
           (uint32_t)status.irq_line,
           (uint32_t)status.irq_pin,
           (uint32_t)status.prog_if);
    kprint("ac97: nambar=%x nabmbar=%x mixer_reset=%x power=%x ext_id=%x ext_ctrl=%x\n",
           status.nambar,
           status.nabmbar,
           status.mixer_reset,
           status.powerdown,
           status.ext_audio_id,
           status.ext_audio_ctrl);
    kprint("ac97: codec_id=%x global_sta=%x global_cnt=%x init=%u\n",
           status.codec_id,
           status.global_status,
           status.global_control,
           (uint32_t)status.initialized);
}

void kernel_log_rtl8139_info(void) {
    struct rtl8139_status status;

    if (!rtl8139_query_status(&status) || !status.present) {
        kprint("rtl8139: controller not found\n");
        return;
    }

    kprint("rtl8139: bdf=%u:%u.%u vendor=%x device=%x irq=%u pin=%u init=%u\n",
           (uint32_t)status.bus,
           (uint32_t)status.slot,
           (uint32_t)status.function,
           (uint32_t)status.vendor_id,
           (uint32_t)status.device_id,
           (uint32_t)status.irq_line,
           (uint32_t)status.irq_pin,
           (uint32_t)status.initialized);
    kprint("rtl8139: io=%x pci_cmd=%x cmd=%x imr=%x isr=%x media=%x speed=%u link=%u\n",
           (uint32_t)status.io_base,
           (uint32_t)status.pci_command,
           (uint32_t)status.chip_cmd,
           (uint32_t)status.intr_mask,
           (uint32_t)status.intr_status,
           (uint32_t)status.media_status,
           (uint32_t)status.speed_mbps,
           (uint32_t)status.link_up);
    kprint("rtl8139: mac=%x:%x:%x:%x:%x:%x tx=%x rx=%x\n",
           (uint32_t)status.mac[0],
           (uint32_t)status.mac[1],
           (uint32_t)status.mac[2],
           (uint32_t)status.mac[3],
           (uint32_t)status.mac[4],
           (uint32_t)status.mac[5],
           status.tx_config,
           status.rx_config);
}

void kernel_log_block_devices(void) {
    struct blockdev_info info;
    uint32_t count = blockdev_count();
    uint32_t i;

    kprint("block: devices=%u\n", count);
    for (i = 0; i < count; i++) {
        if (blockdev_get_info(i, &info) != 0) {
            continue;
        }
        kprint("block[%u]: name=%s block_size=%u block_count=%lx writable=%u\n",
               i,
               info.name != 0 ? info.name : "(null)",
               info.block_size,
               info.block_count,
               (uint32_t)info.writable);
    }
}

void kernel_init_storage_devices(const struct bootx_boot_info *boot_info) {
    blockdev_init();
    ata_init();
    ahci_init();
    xhci_init();
    ehci_init();
    kernel_log_pci_info();
    (void)ac97_init();
    kernel_log_ac97_info();
    (void)rtl8139_init();
    kernel_log_rtl8139_info();
    ramdisk_init_from_boot_modules(boot_info);
    kernel_log_block_devices();
}

static void kernel_mount_ramdisk_root(struct vfs *vfs) {
    uint32_t ramdisk_index;

    if (vfs == 0) {
        return;
    }
    ramdisk_index = ramdisk_disk_index_by_name("RAMDISK.IMG");
    if (ramdisk_index == 0xffffffffu) {
        ramdisk_index = ramdisk_first_disk_index();
    }
    if (ramdisk_index != 0xffffffffu && blockdev_get(ramdisk_index) != 0) {
        uint32_t ramdisk_part_index =
            blockdev_partition_count(blockdev_get(ramdisk_index)) != 0 ? 0u : VFS_PARTITION_RAW;

        (void)vfs_mount_fs(vfs, VFS_MOUNT_FAT32, ramdisk_index, ramdisk_part_index, "/ram");
        (void)vfs_set_root_mount(vfs, "/ram");
    }
}

struct vfs *kernel_bootstrap_vfs(void) {
    vfs_init(&g_kernel_vfs);
    kernel_mount_ramdisk_root(&g_kernel_vfs);
    return &g_kernel_vfs;
}

int kernel_boot_info_valid(const struct bootx_boot_info *boot_info) {
    return boot_info != 0 && boot_info->hdr.magic == BOOTX_MAGIC &&
           boot_info->hdr.version >= BOOTX_PROTOCOL_VERSION;
}

uint64_t kernel_detect_phys_base(const struct bootx_boot_info *boot_info) {
    uint64_t phys = 0;

    if (vmm_query((uint64_t)(uintptr_t)__kernel_start, &phys)) {
        return phys;
    }
    return boot_info != 0 ? boot_info->kernel_phys_addr : 0;
}

void kernel_reserve_boot_modules(const struct bootx_boot_info *boot_info) {
    const struct bootx_module *modules;
    uint32_t i;

    if (boot_info == 0 || boot_info->module_count == 0 || boot_info->modules == 0) {
        return;
    }

    modules = (const struct bootx_module *)(uintptr_t)boot_info->modules;
    for (i = 0; i < boot_info->module_count; i++) {
        if (modules[i].address == 0 || modules[i].size == 0) {
            continue;
        }
        pmm_reserve_range(modules[i].address, modules[i].size);
    }
}

int kernel_extract_init_path(const char *cmdline, char *out, uint32_t out_size) {
    uint32_t i;

    if (cmdline == 0 || out == 0 || out_size < 2u) {
        return 0;
    }

    while (*cmdline != '\0') {
        cmdline = skip_spaces(cmdline);
        if (*cmdline == '\0') {
            break;
        }
        if (starts_with(cmdline, "init=")) {
            cmdline += 5;
            if (*cmdline == '/') {
                cmdline++;
            }
            if (*cmdline == '\0' || *cmdline == ' ') {
                return 0;
            }
            i = 0;
            while (cmdline[i] != '\0' && cmdline[i] != ' ' && i + 1u < out_size) {
                out[i] = cmdline[i];
                i++;
            }
            out[i] = '\0';
            return i != 0;
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    return 0;
}
