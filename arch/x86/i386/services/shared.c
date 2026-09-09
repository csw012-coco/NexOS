#include "block/blockdev.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "drivers/bus/pci.h"
#include "drivers/bus/acpi.h"
#include "drivers/bus/lapic.h"
#include "drivers/bus/ioapic.h"
#include "drivers/storage/ata.h"
#include "drivers/storage/ramdisk.h"
#include "drivers/net/rtl8139.h"
#include "fs/fat32.h"
#include "fs/nxfs.h"
#include "fs/vfs_internal.h"
#include "hal/hal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/internal/core/boot_log_internal.h"
#include "kernel/internal/core/boot_state_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "arch/x86/i386/services/shared_services.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/public/core/early_console.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/input/input_focus.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/process_mm_ops.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "kernel/public/proc/process_user_backend.h"
#include "kernel/public/core/kernel_init_flow.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "arch/x86/i386/syscall/compat32.h"
#include "lib/string.h"

static struct vfs shared_services_vfs;
static struct tty *shared_services_tty;
static char shared_services_syscall_io_buffer[4096];
static uint32_t shared_services_input_focus_pid;

struct vfs *shared_services_active_vfs(void) {
    return &shared_services_vfs;
}

struct tty *shared_services_active_tty(void) {
    return shared_services_tty;
}

int kernel_runtime_run_with_irqs_enabled(int (*fn)(void *ctx), void *ctx) {
    return fn != 0 ? fn(ctx) : 0;
}

int input_focus_grab(uint32_t pid) {
    if (pid == 0u || !job_current_process_foreground_allowed()) {
        return 0;
    }
    shared_services_input_focus_pid = pid;
    return 1;
}

int input_focus_release(uint32_t pid) {
    if (pid == 0u || shared_services_input_focus_pid != pid) {
        return 0;
    }
    shared_services_input_focus_pid = 0u;
    return 1;
}

void input_focus_clear(void) {
    shared_services_input_focus_pid = 0u;
}

uint32_t input_focus_owner_pid(void) {
    return shared_services_input_focus_pid;
}

int job_foreground_pid(uint32_t pid) {
    if (shared_services_tty == 0 || pid == 0u) {
        return -NEX_ERR_INVAL;
    }
    tty_set_foreground_pid(shared_services_tty, pid);
    return 1;
}

int job_background_pid(uint32_t pid) {
    if (shared_services_tty == 0 || pid == 0u) {
        return -NEX_ERR_INVAL;
    }
    tty_clear_foreground_pid(shared_services_tty, pid);
    if (shared_services_input_focus_pid == pid) {
        shared_services_input_focus_pid = 0u;
    }
    return 1;
}

int job_current_process_foreground_allowed(void) {
    uint32_t pid = process_scheduler_current_pid();

    return shared_services_tty != 0 &&
           pid != 0u &&
           tty_foreground_pid(shared_services_tty) == pid;
}

struct shared_services_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

static const struct shared_services_memmap_entry *shared_services_memmap;
static uint32_t shared_services_memmap_count;
static const struct bootx_boot_info *shared_services_boot_info;
static struct syscall_boot_info shared_services_boot_info_query;
static struct syscall_framebuffer_info shared_services_fb_info;
static const char *shared_services_cmdline;
void boot_services_log(const char *text);

static void shared_services_fill_machine_info(struct syscall_machine_info *info);
static void shared_services_fill_block_info(struct syscall_block_info *info,
                                 uint32_t index,
                                 struct block_device *dev);
static void shared_services_fill_part_info(struct syscall_partition_info *info,
                                uint32_t disk_index,
                                uint32_t part_index,
                                const struct blockdev_partition *part);
static int shared_services_fill_mount_info(struct syscall_mount_info *info,
                                uint32_t index,
                                uint32_t flags);
static int shared_services_init_process_runtime_step(void *context);
static int shared_services_init_driver_model_step(void *context);
static int shared_services_init_backend_smoke_step(void *context);

static void shared_services_gfx_init_from_framebuffer_info(const struct syscall_framebuffer_info *fb_info) {
    struct bootx_console_info console;

    if (fb_info == 0 || fb_info->present == 0u || fb_info->width == 0u ||
        fb_info->height == 0u || fb_info->pitch == 0u || fb_info->bpp == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < sizeof(console); i++) {
        ((uint8_t *)&console)[i] = 0u;
    }
    console.type = BOOTX_CONSOLE_FRAMEBUFFER;
    console.framebuffer_addr = fb_info->addr;
    console.width = fb_info->width;
    console.height = fb_info->height;
    console.pitch = fb_info->pitch;
    console.framebuffer_bpp = (uint8_t)fb_info->bpp;
    console.red_mask_size = (uint8_t)fb_info->red_mask_size;
    console.red_mask_shift = (uint8_t)fb_info->red_mask_shift;
    console.green_mask_size = (uint8_t)fb_info->green_mask_size;
    console.green_mask_shift = (uint8_t)fb_info->green_mask_shift;
    console.blue_mask_size = (uint8_t)fb_info->blue_mask_size;
    console.blue_mask_shift = (uint8_t)fb_info->blue_mask_shift;
    console.text_columns = (uint16_t)fb_info->text_columns;
    console.text_rows = (uint16_t)fb_info->text_rows;
    console.text_color = (uint8_t)fb_info->text_color;
    kernel_gfx_init(&console);
}

void shared_services_query_init(const struct syscall_boot_info *boot_info,
                                const struct syscall_framebuffer_info *fb_info,
                                const struct bootx_boot_info *raw_boot_info,
                                uint32_t cmdline,
                                uint32_t memmap,
                                uint32_t memmap_count) {
    shared_services_boot_info = raw_boot_info;
    if (boot_info != 0) {
        shared_services_boot_info_query = *boot_info;
    } else {
        for (uint32_t i = 0u; i < sizeof(shared_services_boot_info_query); i++) {
            ((uint8_t *)&shared_services_boot_info_query)[i] = 0u;
        }
    }
    if (fb_info != 0) {
        shared_services_fb_info = *fb_info;
    } else {
        for (uint32_t i = 0u; i < sizeof(shared_services_fb_info); i++) {
            ((uint8_t *)&shared_services_fb_info)[i] = 0u;
        }
    }
    if (raw_boot_info != 0) {
        kernel_gfx_init(&raw_boot_info->console);
    }
    shared_services_gfx_init_from_framebuffer_info(&shared_services_fb_info);
    if (memmap != 0u && memmap_count != 0u) {
        shared_services_memmap = (const struct shared_services_memmap_entry *)memmap;
        shared_services_memmap_count = memmap_count;
    } else {
        shared_services_memmap = 0;
        shared_services_memmap_count = 0u;
    }
    syscall_common_request_core_query_state_init(
        0,
        raw_boot_info,
        (const struct bootx_memmap_entry *)(uintptr_t)memmap,
        memmap_count);
    shared_services_cmdline = cmdline != 0u ? (const char *)(uintptr_t)cmdline : "";
    boot_flags_init(shared_services_cmdline);
    kernel_boot_state_init(shared_services_cmdline, "kernel-i386", "i386", "i386");
}

void boot_services_log(const char *text) {
    kprint("%s\n", text);
}

int shared_services_selftest_verbose(void) {
    return boot_flags_dev_selftest_enabled();
}

void shared_services_syscall_context(struct syscall_compat32_context *ctx) {
    ctx->vfs = &shared_services_vfs;
    ctx->tty = shared_services_tty;
    ctx->io_buffer = shared_services_syscall_io_buffer;
    ctx->io_buffer_size = sizeof(shared_services_syscall_io_buffer);
    ctx->boot_info = &shared_services_boot_info_query;
    ctx->fb_info = &shared_services_fb_info;
    ctx->memmap =
        (const struct syscall_compat32_memmap_entry *)shared_services_memmap;
    ctx->memmap_count = shared_services_memmap_count;
    ctx->pop_keyboard_event = input_services_pop_keyboard_event;
    ctx->page_alloc = process_mm_page_alloc;
    ctx->page_alloc_prot = process_mm_page_alloc_prot;
    ctx->page_alloc_at = process_mm_page_alloc_at;
    ctx->page_protect = process_mm_page_protect;
    ctx->page_free = process_mm_page_free;
    ctx->page_free_pid = process_mm_page_free_pid;
    ctx->shared_page_alloc = process_mm_shared_page_alloc;
    ctx->shared_page_free = process_mm_shared_page_free;
    ctx->shared_page_map = process_mm_shared_page_map;
    ctx->shared_page_unmap = process_mm_shared_page_unmap;
    ctx->shared_page_unmap_pid = process_mm_shared_page_unmap_pid;
    ctx->spawn_command = boot_user_services_spawn_command;
    ctx->wait = process_scheduler_wait;
    ctx->exit = process_scheduler_exit;
    ctx->yield = process_scheduler_yield;
    ctx->sleep = process_scheduler_sleep;
    ctx->kill = process_scheduler_kill;
    ctx->process_snapshot = process_scheduler_snapshot;
    ctx->fill_machine_info = shared_services_fill_machine_info;
    ctx->fill_block_info = shared_services_fill_block_info;
    ctx->fill_part_info = shared_services_fill_part_info;
    ctx->fill_mount_info = shared_services_fill_mount_info;
    ctx->pmm_total_pages = pmm_total_pages;
    ctx->pmm_free_pages = pmm_free_pages;
    ctx->pmm_reserved_pages = pmm_used_pages;
}

void shared_services_syscall_cleanup_pid(uint32_t pid) {
    struct syscall_compat32_context ctx;

    if (pid == 0u) {
        return;
    }
    shared_services_syscall_context(&ctx);
    ctx.pid = pid;
    ctx.ticks = process_scheduler_ticks();
    syscall_compat32_cleanup_pid(&ctx, pid);
}

int shared_services_syscall_page_is_shared(uint32_t pid, uint32_t user_page) {
    return syscall_compat32_page_is_shared(pid, user_page);
}

static uint64_t shared_services_memmap_usable_total(void) {
    uint64_t total = 0u;

    for (uint32_t i = 0u; shared_services_memmap != 0 && i < shared_services_memmap_count; i++) {
        if (shared_services_memmap[i].type == 1u) {
            total += shared_services_memmap[i].length;
        }
    }
    return total;
}

static uint32_t shared_services_block_device_count(void) {
    uint32_t count = 0u;

    while (blockdev_get(count) != 0) {
        count++;
    }
    return count;
}

static int shared_services_find_free_mount_slot(uint32_t *slot_out) {
    if (slot_out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (!shared_services_vfs.mounts[i].used) {
            *slot_out = i;
            return 1;
        }
    }
    return 0;
}

static void shared_services_mount_ramdisk(void) {
    struct block_device *disk;
    struct blockdev_partition partition;
    uint32_t ramdisk_index;
    uint32_t slot;

    ramdisk_index = ramdisk_disk_index_by_name("RAMDISK.IMG");
    if (ramdisk_index == 0xffffffffu) {
        ramdisk_index = ramdisk_first_disk_index();
    }
    disk = ramdisk_index != 0xffffffffu ? blockdev_get(ramdisk_index) : 0;
    if (disk == 0 ||
        blockdev_partition_get(disk, 0u, &partition) != 0 ||
        !shared_services_find_free_mount_slot(&slot) ||
        fat32_mount(&shared_services_vfs.mounts[slot].fat32,
                    disk,
                    (uint32_t)partition.start_lba) != 0) {
        return;
    }

    shared_services_vfs.mounts[slot].used = 1u;
    shared_services_vfs.mounts[slot].kind = VFS_MOUNT_FAT32;
    shared_services_vfs.mounts[slot].disk_index = ramdisk_index;
    shared_services_vfs.mounts[slot].part_index = 0u;
    vfs_copy_name(shared_services_vfs.mounts[slot].name,
                  sizeof(shared_services_vfs.mounts[slot].name),
                  "ram");
    boot_services_log("ramdisk: FAT32 /ram mounted");
}

static int shared_services_mount_nxfs_root(struct block_device *boot_disk) {
    struct blockdev_partition partition;

    if (boot_disk == 0 ||
        blockdev_partition_get(boot_disk, 1u, &partition) != 0 ||
        nxfs_mount(&shared_services_vfs.nxfs, boot_disk, (uint32_t)partition.start_lba) != 0) {
        return 0;
    }
    shared_services_vfs.root_kind = VFS_MOUNT_NXFS;
    shared_services_vfs.root_slot = 0u;
    blockdev_set_rootfs_protected(boot_disk, 1u);
    return 1;
}

static void shared_services_log_boot_info(void) {
    kernel_boot_log_system("i386");
    if (shared_services_boot_info != 0) {
        kprint("bootx: magic=%x version=%u size=%u\n",
               shared_services_boot_info->hdr.magic,
               (uint32_t)shared_services_boot_info->hdr.version,
               (uint32_t)shared_services_boot_info->hdr.size);
        kernel_boot_log_boot_info_common(shared_services_boot_info);
        kprint("boot: cmdline=%x\n", shared_services_boot_info->cmdline);
        kprint("boot: kernel_phys=%lx kernel_size=%lx entry=%lx\n",
               shared_services_boot_info->kernel_phys_addr,
               shared_services_boot_info->kernel_phys_size,
               shared_services_boot_info->kernel_entry);
        kernel_boot_log_console(&shared_services_boot_info->console);
        kprint("boot: kernel_phys(proto)=%lx kernel_phys(detect)=%lx kernel_phys(map)=%lx mapped=%u\n",
               shared_services_boot_info->kernel_phys_addr,
               shared_services_boot_info->kernel_phys_addr,
               shared_services_boot_info->kernel_phys_addr,
               1u);
        return;
    }
    kprint("boot: drive=0x%x part_lba=%u part_sectors=%u modules=%u\n",
           shared_services_boot_info_query.boot_drive,
           (uint32_t)shared_services_boot_info_query.partition_lba,
           (uint32_t)shared_services_boot_info_query.partition_sectors,
           shared_services_boot_info_query.module_count);
    if (shared_services_fb_info.present != 0u) {
        kprint("boot: console=framebuffer %ux%u pitch=%u bpp=%u text=%ux%u\n",
               shared_services_fb_info.width,
               shared_services_fb_info.height,
               shared_services_fb_info.pitch,
               shared_services_fb_info.bpp,
               shared_services_fb_info.text_columns,
               shared_services_fb_info.text_rows);
    } else {
        kprint("boot: console=text %ux%u color=%u\n",
               shared_services_fb_info.text_columns,
               shared_services_fb_info.text_rows,
               shared_services_fb_info.text_color);
    }
}

static void shared_services_log_pci_ide_info(void) {
    struct pci_ide_controller ide;

    if (pci_find_ide_controller(&ide)) {
        kprint("pci: ide bdf=%u:%u.%u vendor=%x device=%x prog_if=%x\n",
               ide.bus,
               ide.slot,
               ide.function,
               ide.vendor_id,
               ide.device_id,
               ide.prog_if);
        kprint("pci: ide bar0=%x bar1=%x bar2=%x bar3=%x bar4=%x\n",
               ide.bar0,
               ide.bar1,
               ide.bar2,
               ide.bar3,
               ide.bar4);
    } else {
        kprint("pci: ide controller not found\n");
    }
}

static void shared_services_log_ata_info(void) {
    struct ata_device *primary = ata_get_primary_master();

    if (primary != 0 && primary->present != 0u) {
        kprint("ata: primary master sectors=%u model=%s\n",
               primary->sector_count,
               primary->model);
    } else {
        kprint("ata: primary master not found\n");
    }
}

static void shared_services_log_ac97_info(void) {
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

static void shared_services_log_hda_info(void) {
    struct hda_status status;

    if (!hda_query_status(&status) || !status.present) {
        kprint("hda: controller not found\n");
        return;
    }

    kprint("hda: bdf=%u:%u.%u vendor=%x device=%x irq=%u pin=%u prog_if=%x\n",
           (uint32_t)status.bus,
           (uint32_t)status.slot,
           (uint32_t)status.function,
           (uint32_t)status.vendor_id,
           (uint32_t)status.device_id,
           (uint32_t)status.irq_line,
           (uint32_t)status.irq_pin,
           (uint32_t)status.prog_if);
    kprint("hda: mmio=%x:%x pci_cmd=%x gcap=%x version=%u.%u\n",
           status.mmio_base_hi,
           status.mmio_base_lo,
           status.pci_command,
           status.gcap,
           status.vmaj,
           status.vmin);
    kprint("hda: inpay=%x outpay=%x gctl=%x statests=%x wakeen=%x codec_mask=%x init=%u\n",
           status.inpay,
           status.outpay,
           status.gctl,
           status.statests,
           status.wakeen,
           status.codec_mask,
           (uint32_t)status.initialized);
}

static void shared_services_log_rtl8139_info(void) {
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

static void shared_services_log_block_devices(void) {
    struct blockdev_info info;
    uint32_t count = shared_services_block_device_count();

    kprint("block: devices=%u\n", count);
    for (uint32_t i = 0u; i < count; i++) {
        if (blockdev_get_info(i, &info) != 0) {
            continue;
        }
        kprint("block[%u]: name=%s block_size=%u block_count=%lx writable=%u\n",
               i,
               info.name,
               info.block_size,
               info.block_count,
               info.writable);
    }
}

static void shared_services_copy_text(char *dst, uint32_t size, const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void shared_services_cpuid(uint32_t leaf,
                       uint32_t *eax,
                       uint32_t *ebx,
                       uint32_t *ecx,
                       uint32_t *edx) {
    uint32_t a = 0u;
    uint32_t b = 0u;
    uint32_t c = 0u;
    uint32_t d = 0u;

    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(0u));
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

static void shared_services_fill_machine_info(struct syscall_machine_info *info) {
    if (info == 0) {
        return;
    }
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    shared_services_copy_text(info->os_name, sizeof(info->os_name), "NexOS");
    shared_services_copy_text(info->kernel_name, sizeof(info->kernel_name), "kernel-i386");
    shared_services_copy_text(info->kernel_version, sizeof(info->kernel_version), "i386");
    shared_services_copy_text(info->build_date, sizeof(info->build_date), __DATE__ " " __TIME__);
    shared_services_copy_text(info->arch_name, sizeof(info->arch_name), "i386");
    shared_services_cpuid(0u,
               &info->cpuid_leaf0_eax,
               &info->cpuid_leaf0_ebx,
               &info->cpuid_leaf0_ecx,
               &info->cpuid_leaf0_edx);
    ((uint32_t *)info->cpu_vendor)[0] = info->cpuid_leaf0_ebx;
    ((uint32_t *)info->cpu_vendor)[1] = info->cpuid_leaf0_edx;
    ((uint32_t *)info->cpu_vendor)[2] = info->cpuid_leaf0_ecx;
    info->cpu_vendor[12] = '\0';
    shared_services_cpuid(1u,
               &info->cpuid_leaf1_eax,
               &info->cpuid_leaf1_ebx,
               &info->cpuid_leaf1_ecx,
               &info->cpuid_leaf1_edx);
    shared_services_copy_text(info->cpu_brand, sizeof(info->cpu_brand), info->cpu_vendor);
    info->text_columns = hal_display_text_columns();
    info->text_rows = hal_display_text_rows();
    info->text_cell_width = 8u;
    info->text_cell_height = hal_display_cell_height();
}

static void shared_services_fill_block_info(struct syscall_block_info *info,
                                 uint32_t index,
                                 struct block_device *dev) {
    struct blockdev_info block_info;

    if (info == 0 || dev == 0 || blockdev_get_info(index, &block_info) != 0) {
        return;
    }
    info->index = index;
    info->block_size = block_info.block_size;
    info->partition_count = block_info.partition_count;
    info->writable = block_info.writable;
    info->block_count = block_info.block_count;
    shared_services_copy_text(info->name, sizeof(info->name), block_info.name);
}

static void shared_services_fill_part_info(struct syscall_partition_info *info,
                                uint32_t disk_index,
                                uint32_t slot,
                                const struct blockdev_partition *part) {
    if (info == 0 || part == 0) {
        return;
    }
    info->disk_index = disk_index;
    info->slot = slot;
    info->part_index = part->index;
    info->start_lba = part->start_lba;
    info->sector_count = part->sector_count;
    info->type = part->type;
    info->bootable = part->bootable;
}

static void shared_services_mount_info_init(struct syscall_mount_info *info) {
    if (info == 0) {
        return;
    }
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    info->kind = SYS_MOUNT_INFO_NONE;
}

static void shared_services_mount_fill_space(struct syscall_mount_info *info,
                                  uint8_t kind,
                                  uint32_t slot) {
    uint32_t block_size = 0u;
    uint64_t total_blocks = 0u;
    uint64_t free_blocks = 0u;

    if (info == 0) {
        return;
    }
    if (kind == VFS_MOUNT_FAT32) {
        struct fat32_volume *fat32 = slot == 0u
            ? &shared_services_vfs.fat32
            : &shared_services_vfs.mounts[slot - 1u].fat32;

        if (fat32_space_info(fat32,
                             &block_size,
                             &total_blocks,
                             &free_blocks) != 0) {
            return;
        }
    } else if (kind == VFS_MOUNT_NXFS) {
        struct nxfs_volume *nxfs = slot == 0u
            ? &shared_services_vfs.nxfs
            : &shared_services_vfs.mounts[slot - 1u].nxfs;

        if (nxfs_space_info(nxfs,
                            &block_size,
                            &total_blocks,
                            &free_blocks) != 0) {
            return;
        }
    } else {
        return;
    }
    info->space_known = 1u;
    info->block_size = block_size;
    info->total_blocks = total_blocks;
    info->free_blocks = free_blocks;
}

static int shared_services_fill_mount_info(struct syscall_mount_info *info,
                                uint32_t index,
                                uint32_t flags) {
    uint32_t visible = 0u;
    int include_space = (flags & SYS_QUERY_FLAG_NO_SPACE) == 0u;

    shared_services_mount_info_init(info);
    if (index == visible && shared_services_vfs.fat32.mounted) {
        info->kind = SYS_MOUNT_INFO_FAT32;
        info->disk_index = 0u;
        info->part_index = 0u;
        info->source_known = 1u;
        shared_services_copy_text(info->target, sizeof(info->target), "boot");
        if (include_space) {
            shared_services_mount_fill_space(info, VFS_MOUNT_FAT32, 0u);
        }
        return 1;
    }
    if (shared_services_vfs.fat32.mounted) {
        visible++;
    }
    if (index == visible && shared_services_vfs.nxfs.mounted) {
        info->kind = SYS_MOUNT_INFO_NXFS;
        info->disk_index = 0u;
        info->part_index = 0u;
        info->source_known = 1u;
        shared_services_copy_text(info->target, sizeof(info->target), "nxfs");
        if (include_space) {
            shared_services_mount_fill_space(info, VFS_MOUNT_NXFS, 0u);
        }
        return 1;
    }
    if (shared_services_vfs.nxfs.mounted) {
        visible++;
    }
    for (uint32_t slot = 0u; slot < VFS_MOUNT_SLOT_MAX; slot++) {
        if (!shared_services_vfs.mounts[slot].used) {
            continue;
        }
        if (index == visible) {
            if (shared_services_vfs.mounts[slot].kind == VFS_MOUNT_FAT32) {
                info->kind = SYS_MOUNT_INFO_FAT32;
            } else if (shared_services_vfs.mounts[slot].kind == VFS_MOUNT_NXFS) {
                info->kind = SYS_MOUNT_INFO_NXFS;
            } else {
                info->kind = SYS_MOUNT_INFO_NONE;
            }
            info->disk_index = shared_services_vfs.mounts[slot].disk_index;
            info->part_index = shared_services_vfs.mounts[slot].part_index;
            info->source_known = 1u;
            shared_services_copy_text(info->target,
                           sizeof(info->target),
                           shared_services_vfs.mounts[slot].name);
            if (include_space) {
                shared_services_mount_fill_space(info,
                                      shared_services_vfs.mounts[slot].kind,
                                      slot + 1u);
            }
            return 1;
        }
        visible++;
    }
    return 0;
}

static int shared_services_init_process_runtime_step(void *context) {
    struct vfs *vfs = (struct vfs *)context;

    if (vfs == 0) {
        return 0;
    }
    process_user_init_runtime_vfs(vfs);
    return 1;
}

static int shared_services_init_driver_model_step(void *context) {
    struct vfs *vfs = (struct vfs *)context;

    if (vfs == 0) {
        return 0;
    }
    driver_services_init(vfs,
                           boot_flags_dev_selftest_enabled() ||
                           boot_flags_full_smoke_enabled() ||
                           boot_flags_driver_smoke_enabled());
    boot_services_log("pseudo fs: devfs procfs eventfs ready");
    boot_services_log("kernel: interrupts");
    return 1;
}

static int shared_services_init_backend_smoke_step(void *context) {
    (void)context;
    return smoke_services_run_backend(boot_flags_ahci_smoke_enabled(),
                                  boot_flags_usb_smoke_enabled(),
                                  boot_flags_usb_hid_smoke_enabled(),
                                  boot_flags_rtl8139_smoke_enabled(),
                                  boot_flags_hda_smoke_enabled(),
                                  boot_flags_ac97_smoke_enabled(),
                                  boot_flags_gfx_editor_smoke_enabled());
}

int shared_services_init(void) {
    static const struct kernel_init_step shared_services_init_steps[] = {
        {"process-runtime", shared_services_init_process_runtime_step},
        {"driver-model", shared_services_init_driver_model_step},
        {"backend-smoke", shared_services_init_backend_smoke_step}
    };
    struct block_device *boot_disk;
    struct blockdev_partition partition;
    int dev_selftest = boot_flags_dev_selftest_enabled();

    tty_virtual_init_all(0u, (uint16_t)(hal_display_text_rows() - 1u), 0x0fu);
    shared_services_tty = tty_active();
    if (shared_services_tty == 0) {
        return 0;
    }

    tty_clear(shared_services_tty);
    kprint_init();
    kprint_set_tty(shared_services_tty);
    process_scheduler_set_console_handle(shared_services_tty);
    boot_services_log("kernel: entered");
    shared_services_log_boot_info();
    kernel_boot_log_memmap_summary(shared_services_memmap_usable_total(), shared_services_memmap_count);
    boot_services_log("kernel: paging init");
    kernel_boot_log_paging_root(hal_paging_current_root(), 0u, 0u);
    boot_services_log("kernel: pmm init");
    if (shared_services_fb_info.present != 0u) {
        uint64_t framebuffer_size =
            (uint64_t)shared_services_fb_info.pitch * shared_services_fb_info.height;

        kernel_boot_log_framebuffer(shared_services_fb_info.addr, framebuffer_size, 0, 0u);
    }
    boot_services_log("kernel: framebuffer backbuffer skip");
    kernel_boot_log_pmm(pmm_total_pages(),
                        pmm_free_pages(),
                        pmm_used_pages(),
                        0u);

    vfs_init(&shared_services_vfs);
    syscall_common_request_core_query_state_init(
        &shared_services_vfs,
        shared_services_boot_info,
        (const struct bootx_memmap_entry *)(uintptr_t)shared_services_memmap,
        shared_services_memmap_count);
    boot_services_log("kernel: block devices");
    ramdisk_init_from_boot_modules(shared_services_boot_info);
    shared_services_log_pci_ide_info();
    (void)acpi_init();
    (void)lapic_init_from_acpi();
    (void)lapic_enable();
    (void)ioapic_init_from_acpi();
    if (ioapic_configure_from_cmdline(shared_services_cmdline) > 0) {
        uint32_t ioapic_mask = ioapic_enabled_irq_mask();

        for (uint8_t irq = 0u; irq < 16u; irq++) {
            if ((ioapic_mask & (1u << irq)) != 0u) {
                hal_irq_set_mask(irq, 0);
            }
        }
    }
    shared_services_log_ac97_info();
    shared_services_log_hda_info();
    shared_services_log_rtl8139_info();
    shared_services_log_ata_info();
    shared_services_log_block_devices();
    boot_services_log("kernel: pci/ide/ata");
    boot_disk = blockdev_get(0u);
    if (boot_disk == 0 ||
        blockdev_partition_get(boot_disk, 0u, &partition) != 0 ||
        fat32_mount(&shared_services_vfs.fat32,
                    boot_disk,
                    (uint32_t)partition.start_lba) != 0) {
        tty_write_str(shared_services_tty, "VFS: FAT32 root mount failed\n", 0x0cu);
        return 0;
    }

    shared_services_vfs.root_kind = VFS_MOUNT_FAT32;
    shared_services_vfs.root_slot = 0u;
    blockdev_set_rootfs_protected(boot_disk, 1u);
    if (shared_services_mount_nxfs_root(boot_disk)) {
        boot_services_log("root: NXFS / mounted");
    }
    shared_services_mount_ramdisk();
    boot_services_log("kernel: tty/process/vfs");
    if (dev_selftest) {
        if (!tty_selftest_input()) {
            tty_write_str(shared_services_tty, "TTY input self-test failed\n", 0x0cu);
            return 0;
        }
        if (!tty_selftest_utf8_edit()) {
            tty_write_str(shared_services_tty, "TTY UTF-8 edit self-test failed\n", 0x0cu);
            return 0;
        }
        boot_services_log("tty: utf8/hangul edit selftest OK");
    }
    if (shared_services_vfs.root_kind == VFS_MOUNT_FAT32) {
        boot_services_log("root: FAT32 / mounted");
    }
    if (!kernel_init_flow_run(shared_services_init_steps,
                              (uint32_t)(sizeof(shared_services_init_steps) /
                                         sizeof(shared_services_init_steps[0])),
                              &shared_services_vfs)) {
        return 0;
    }
    if (dev_selftest || boot_flags_full_smoke_enabled()) {
        if (!smoke_services_run_test32_selftest()) {
            return 0;
        }
        if (!smoke_services_run_nexbox32_full()) {
            return 0;
        }
        boot_services_log(dev_selftest ? "kernel: i386 dev selftests complete"
                                   : "kernel: i386 full smoke complete");
    }
    if (boot_flags_strict_mm_smoke_enabled()) {
        if (!smoke_services_run_test32_strict_mm()) {
            return 0;
        }
        boot_services_log("kernel: i386 strict MM smoke complete");
    }
    return 1;
}
