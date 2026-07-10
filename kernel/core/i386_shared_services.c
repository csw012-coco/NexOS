#include "block/blockdev.h"
#include "arch/x86/i386/keyboard.h"
#include "arch/x86/i386/paging.h"
#include "arch/x86/i386/pmm.h"
#include "arch/x86/i386/scheduler.h"
#include "drivers/input/keyboard.h"
#include "drivers/bus/pci.h"
#include "drivers/storage/ata.h"
#include "drivers/storage/ramdisk.h"
#include "fs/fat32.h"
#include "fs/vfs_internal.h"
#include "hal/hal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/internal/core/boot_log_internal.h"
#include "kernel/internal/core/boot_state_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/public/core/early_console.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/driver/driver.h"
#include "kernel/public/proc/boot_user_init.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/syscall_compat32.h"
#include "lib/string.h"

static struct vfs i386_vfs;
static struct tty *i386_tty;
static char i386_syscall_io_buffer[4096];

struct i386_query_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

static const struct i386_query_memmap_entry *i386_query_memmap;
static uint32_t i386_query_memmap_count;
static const struct bootx_boot_info *i386_boot_info;
static struct syscall_boot_info i386_query_boot_info;
static struct syscall_framebuffer_info i386_query_fb_info;
static const char *i386_query_cmdline;
static int i386_pop_keyboard_event(struct keyboard_event *event);
static void i386_boot_log(const char *text);

static int i386_builtin_driver_active(void) {
    return 1;
}

static const struct kernel_driver i386_ata_driver = {
    "ata",
    KERNEL_DRIVER_KIND_STORAGE,
    i386_builtin_driver_active,
    0
};

static const struct kernel_driver i386_ramdisk_driver = {
    "ramdisk",
    KERNEL_DRIVER_KIND_STORAGE,
    i386_builtin_driver_active,
    0
};

static const struct kernel_driver i386_devfs_driver = {
    "devfs",
    KERNEL_DRIVER_KIND_UNKNOWN,
    i386_builtin_driver_active,
    0
};

static const struct kernel_driver i386_procfs_driver = {
    "procfs",
    KERNEL_DRIVER_KIND_UNKNOWN,
    i386_builtin_driver_active,
    0
};

static const struct kernel_driver i386_eventfs_driver = {
    "eventfs",
    KERNEL_DRIVER_KIND_UNKNOWN,
    i386_builtin_driver_active,
    0
};

static void i386_driver_model_register_builtins(void) {
    driver_manager_init();
    (void)driver_register(&i386_ata_driver);
    (void)driver_register(&i386_ramdisk_driver);
    (void)driver_register(&i386_devfs_driver);
    (void)driver_register(&i386_procfs_driver);
    (void)driver_register(&i386_eventfs_driver);
}

static void i386_driver_model_init(struct vfs *vfs) {
    uint32_t active;
    uint32_t discovered;

    i386_boot_log("kernel: discover drivers");
    discovered = driver_discover_root(vfs, "/DRIVERS");
    discovered += driver_discover_root(vfs, "/ram/DRIVERS");
    (void)discovered;
    (void)driver_load_all(vfs);
    active = driver_init_all();
    kprint("driver: builtin active=%u total=%u files=%u\n",
           active,
           driver_count(),
           driver_file_count());
}

extern int kernel_i386_run_test32(struct process_snapshot *process0,
                                  struct process_snapshot *process1);
extern int kernel_i386_run_command(const char *command,
                                   struct process_snapshot *process);
extern int32_t kernel_i386_spawn_command(const char *command,
                                         uint32_t mode,
                                         uint32_t flags);

static void i386_fill_machine_info(struct syscall_machine_info *info);
static void i386_fill_block_info(struct syscall_block_info *info,
                                 uint32_t index,
                                 struct block_device *dev);
static void i386_fill_part_info(struct syscall_partition_info *info,
                                uint32_t disk_index,
                                uint32_t part_index,
                                const struct blockdev_partition *part);
static int i386_fill_mount_info(struct syscall_mount_info *info,
                                uint32_t index);
static void i386_boot_user_log(const char *text);
static void i386_boot_user_early_log(const char *text);

void kernel_i386_query_init(const struct syscall_boot_info *boot_info,
                            const struct syscall_framebuffer_info *fb_info,
                            const struct bootx_boot_info *raw_boot_info,
                            uint32_t cmdline,
                            uint32_t memmap,
                            uint32_t memmap_count) {
    i386_boot_info = raw_boot_info;
    if (boot_info != 0) {
        i386_query_boot_info = *boot_info;
    } else {
        for (uint32_t i = 0u; i < sizeof(i386_query_boot_info); i++) {
            ((uint8_t *)&i386_query_boot_info)[i] = 0u;
        }
    }
    if (fb_info != 0) {
        i386_query_fb_info = *fb_info;
    } else {
        for (uint32_t i = 0u; i < sizeof(i386_query_fb_info); i++) {
            ((uint8_t *)&i386_query_fb_info)[i] = 0u;
        }
    }
    if (raw_boot_info != 0) {
        kernel_gfx_init(&raw_boot_info->console);
    }
    if (memmap != 0u && memmap_count != 0u) {
        i386_query_memmap = (const struct i386_query_memmap_entry *)memmap;
        i386_query_memmap_count = memmap_count;
    } else {
        i386_query_memmap = 0;
        i386_query_memmap_count = 0u;
    }
    i386_query_cmdline = cmdline != 0u ? (const char *)(uintptr_t)cmdline : "";
    kernel_boot_state_init(i386_query_cmdline, "kernel-i386", "i386", "i386");
}

static void i386_boot_log(const char *text) {
    kprint("%s\n", text);
}

static int i386_cmdline_has(const char *needle) {
    const char *cmdline = i386_query_cmdline;
    uint32_t len = 0u;

    if (needle == 0 || needle[0] == '\0') {
        return 0;
    }
    while (needle[len] != '\0') {
        len++;
    }
    while (cmdline != 0 && *cmdline != '\0') {
        while (*cmdline == ' ') {
            cmdline++;
        }
        if (cmdline[len] == '\0' || cmdline[len] == ' ') {
            int matched = 1;

            for (uint32_t i = 0u; i < len; i++) {
                if (cmdline[i] != needle[i]) {
                    matched = 0;
                    break;
                }
            }
            if (matched) {
                return 1;
            }
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    return 0;
}

static int i386_boot_dev_selftest_enabled(void) {
    return i386_cmdline_has("selftest=1") ||
           i386_cmdline_has("i386.dev=1") ||
           i386_cmdline_has("i386.selftest=1");
}

int kernel_i386_selftest_verbose(void) {
    return i386_boot_dev_selftest_enabled();
}

static void i386_boot_user_log(const char *text) {
    i386_boot_log(text);
}

static void i386_boot_user_early_log(const char *text) {
    kprint("%s", text);
}

static void i386_boot_user_init_config(struct boot_user_init_config *config) {
    static const struct boot_user_init_ops ops = {
        .run_test_pair = kernel_i386_run_test32,
        .run_command = kernel_i386_run_command,
        .boot_log = i386_boot_user_log,
        .early_log = i386_boot_user_early_log,
    };

    config->tty = i386_tty;
    config->ops = &ops;
    config->test_name = "/BOOT/TEST32.ELF";
    config->test_log_prefix = "test32";
    config->test_pass_log = "test32: PASS\n";
    config->test_fail_log = "test32: FAILED\n";
    config->test_boot_pass_log = "selftest: test32 PASS";
    config->shell_command = "/BOOT/USH32.ELF";
    config->shell_log_prefix = "init";
    config->shell_start_log = "kernel: init starting /BOOT/USH32.ELF\n";
    config->shell_fail_log = "kernel: init /BOOT/USH32.ELF failed\n";
    config->shell_exit_log = "kernel: init /BOOT/USH32.ELF exited\n";
    config->verbose_selftest = i386_boot_dev_selftest_enabled();
}

void kernel_i386_compat32_context(struct syscall_compat32_context *ctx) {
    ctx->vfs = &i386_vfs;
    ctx->tty = i386_tty;
    ctx->io_buffer = i386_syscall_io_buffer;
    ctx->io_buffer_size = sizeof(i386_syscall_io_buffer);
    ctx->boot_info = &i386_query_boot_info;
    ctx->fb_info = &i386_query_fb_info;
    ctx->memmap =
        (const struct syscall_compat32_memmap_entry *)i386_query_memmap;
    ctx->memmap_count = i386_query_memmap_count;
    ctx->pop_keyboard_event = i386_pop_keyboard_event;
    ctx->open = i386_scheduler_open;
    ctx->read = i386_scheduler_read;
    ctx->write = i386_scheduler_write;
    ctx->close = i386_scheduler_close;
    ctx->seek = i386_scheduler_seek;
    ctx->page_alloc = i386_scheduler_page_alloc;
    ctx->page_alloc_prot = i386_scheduler_page_alloc_with_prot;
    ctx->page_alloc_at = i386_scheduler_page_alloc_at;
    ctx->page_protect = i386_scheduler_page_protect;
    ctx->page_free = i386_scheduler_page_free;
    ctx->shared_page_alloc = i386_scheduler_shared_page_alloc;
    ctx->shared_page_free = i386_scheduler_shared_page_free;
    ctx->shared_page_map = i386_scheduler_shared_page_map;
    ctx->shared_page_unmap = i386_scheduler_shared_page_unmap;
    ctx->spawn_command = kernel_i386_spawn_command;
    ctx->wait = i386_scheduler_wait;
    ctx->exit = i386_scheduler_exit;
    ctx->yield = i386_scheduler_yield;
    ctx->sleep = i386_scheduler_sleep;
    ctx->chdir = i386_scheduler_chdir;
    ctx->getcwd = i386_scheduler_getcwd;
    ctx->opendir = i386_scheduler_opendir;
    ctx->readdir = i386_scheduler_readdir;
    ctx->mkdir = i386_scheduler_mkdir;
    ctx->rmdir = i386_scheduler_rmdir;
    ctx->remove = i386_scheduler_remove;
    ctx->pipe = i386_scheduler_pipe;
    ctx->dup2 = i386_scheduler_dup2;
    ctx->kill = i386_scheduler_kill;
    ctx->process_snapshot = i386_scheduler_process_snapshot;
    ctx->fd_kind = i386_scheduler_fd_kind;
    ctx->fd_query = i386_scheduler_fd_query;
    ctx->fill_machine_info = i386_fill_machine_info;
    ctx->fill_block_info = i386_fill_block_info;
    ctx->fill_part_info = i386_fill_part_info;
    ctx->fill_mount_info = i386_fill_mount_info;
    ctx->pmm_total_pages = i386_pmm_total_pages;
    ctx->pmm_free_pages = i386_pmm_free_pages;
    ctx->pmm_reserved_pages = i386_pmm_reserved_pages;
}

static uint64_t i386_memmap_usable_total(void) {
    uint64_t total = 0u;

    for (uint32_t i = 0u; i386_query_memmap != 0 && i < i386_query_memmap_count; i++) {
        if (i386_query_memmap[i].type == 1u) {
            total += i386_query_memmap[i].length;
        }
    }
    return total;
}

static uint32_t i386_block_device_count(void) {
    uint32_t count = 0u;

    while (blockdev_get(count) != 0) {
        count++;
    }
    return count;
}

static int i386_find_free_mount_slot(uint32_t *slot_out) {
    if (slot_out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (!i386_vfs.mounts[i].used) {
            *slot_out = i;
            return 1;
        }
    }
    return 0;
}

static void i386_mount_ramdisk(void) {
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
        !i386_find_free_mount_slot(&slot) ||
        fat32_mount(&i386_vfs.mounts[slot].fat32,
                    disk,
                    (uint32_t)partition.start_lba) != 0) {
        return;
    }

    i386_vfs.mounts[slot].used = 1u;
    i386_vfs.mounts[slot].kind = VFS_MOUNT_FAT32;
    i386_vfs.mounts[slot].disk_index = ramdisk_index;
    i386_vfs.mounts[slot].part_index = 0u;
    vfs_copy_name(i386_vfs.mounts[slot].name,
                  sizeof(i386_vfs.mounts[slot].name),
                  "ram");
    i386_boot_log("ramdisk: FAT32 /ram mounted");
}

static void i386_log_boot_info(void) {
    kernel_boot_log_system("i386");
    if (i386_boot_info != 0) {
        kernel_boot_log_boot_info_common(i386_boot_info);
        kernel_boot_log_console(&i386_boot_info->console);
        return;
    }
    kprint("boot: drive=0x%x part_lba=%u part_sectors=%u modules=%u\n",
           i386_query_boot_info.boot_drive,
           (uint32_t)i386_query_boot_info.partition_lba,
           (uint32_t)i386_query_boot_info.partition_sectors,
           i386_query_boot_info.module_count);
    if (i386_query_fb_info.present != 0u) {
        kprint("boot: console=framebuffer %ux%u pitch=%u bpp=%u text=%ux%u\n",
               i386_query_fb_info.width,
               i386_query_fb_info.height,
               i386_query_fb_info.pitch,
               i386_query_fb_info.bpp,
               i386_query_fb_info.text_columns,
               i386_query_fb_info.text_rows);
    } else {
        kprint("boot: console=text %ux%u color=%u\n",
               i386_query_fb_info.text_columns,
               i386_query_fb_info.text_rows,
               i386_query_fb_info.text_color);
    }
}

static void i386_log_pci_ide_info(void) {
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

static void i386_log_ata_info(void) {
    struct ata_device *primary = ata_get_primary_master();

    if (primary != 0 && primary->present != 0u) {
        kprint("ata: primary master sectors=%u model=%s\n",
               primary->sector_count,
               primary->model);
    } else {
        kprint("ata: primary master not found\n");
    }
}

static void i386_log_block_devices(void) {
    struct blockdev_info info;
    uint32_t count = i386_block_device_count();

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

static void i386_copy_text(char *dst, uint32_t size, const char *src) {
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

static void i386_cpuid(uint32_t leaf,
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

static void i386_fill_machine_info(struct syscall_machine_info *info) {
    if (info == 0) {
        return;
    }
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    i386_copy_text(info->os_name, sizeof(info->os_name), "NexOS");
    i386_copy_text(info->kernel_name, sizeof(info->kernel_name), "kernel-i386");
    i386_copy_text(info->kernel_version, sizeof(info->kernel_version), "i386");
    i386_copy_text(info->build_date, sizeof(info->build_date), __DATE__ " " __TIME__);
    i386_copy_text(info->arch_name, sizeof(info->arch_name), "i386");
    i386_cpuid(0u,
               &info->cpuid_leaf0_eax,
               &info->cpuid_leaf0_ebx,
               &info->cpuid_leaf0_ecx,
               &info->cpuid_leaf0_edx);
    ((uint32_t *)info->cpu_vendor)[0] = info->cpuid_leaf0_ebx;
    ((uint32_t *)info->cpu_vendor)[1] = info->cpuid_leaf0_edx;
    ((uint32_t *)info->cpu_vendor)[2] = info->cpuid_leaf0_ecx;
    info->cpu_vendor[12] = '\0';
    i386_cpuid(1u,
               &info->cpuid_leaf1_eax,
               &info->cpuid_leaf1_ebx,
               &info->cpuid_leaf1_ecx,
               &info->cpuid_leaf1_edx);
    i386_copy_text(info->cpu_brand, sizeof(info->cpu_brand), info->cpu_vendor);
    info->text_columns = hal_display_text_columns();
    info->text_rows = hal_display_text_rows();
    info->text_cell_width = 8u;
    info->text_cell_height = hal_display_cell_height();
}

static void i386_fill_block_info(struct syscall_block_info *info,
                                 uint32_t index,
                                 struct block_device *dev) {
    struct blockdev_info block_info;

    if (info == 0 || dev == 0 || blockdev_get_info(index, &block_info) != 0) {
        return;
    }
    info->index = index;
    info->block_size = block_info.block_size;
    info->partition_count = blockdev_partition_count(dev);
    info->writable = block_info.writable;
    info->block_count = block_info.block_count;
    i386_copy_text(info->name, sizeof(info->name), block_info.name);
}

static void i386_fill_part_info(struct syscall_partition_info *info,
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

static void i386_mount_info_init(struct syscall_mount_info *info) {
    if (info == 0) {
        return;
    }
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    info->kind = SYS_MOUNT_INFO_NONE;
}

static void i386_mount_fill_space(struct syscall_mount_info *info,
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
            ? &i386_vfs.fat32
            : &i386_vfs.mounts[slot - 1u].fat32;

        if (fat32_space_info(fat32,
                             &block_size,
                             &total_blocks,
                             &free_blocks) != 0) {
            return;
        }
    } else if (kind == VFS_MOUNT_NXFS) {
        struct nxfs_volume *nxfs = slot == 0u
            ? &i386_vfs.nxfs
            : &i386_vfs.mounts[slot - 1u].nxfs;

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

static int i386_fill_mount_info(struct syscall_mount_info *info,
                                uint32_t index) {
    uint32_t visible = 0u;

    i386_mount_info_init(info);
    if (index == visible && i386_vfs.fat32.mounted) {
        info->kind = SYS_MOUNT_INFO_FAT32;
        info->disk_index = 0u;
        info->part_index = 0u;
        info->source_known = 1u;
        i386_copy_text(info->target, sizeof(info->target), "fat");
        i386_mount_fill_space(info, VFS_MOUNT_FAT32, 0u);
        return 1;
    }
    if (i386_vfs.fat32.mounted) {
        visible++;
    }
    if (index == visible && i386_vfs.nxfs.mounted) {
        info->kind = SYS_MOUNT_INFO_NXFS;
        info->disk_index = 0u;
        info->part_index = 0u;
        info->source_known = 1u;
        i386_copy_text(info->target, sizeof(info->target), "nxfs");
        i386_mount_fill_space(info, VFS_MOUNT_NXFS, 0u);
        return 1;
    }
    if (i386_vfs.nxfs.mounted) {
        visible++;
    }
    for (uint32_t slot = 0u; slot < VFS_MOUNT_SLOT_MAX; slot++) {
        if (!i386_vfs.mounts[slot].used) {
            continue;
        }
        if (index == visible) {
            if (i386_vfs.mounts[slot].kind == VFS_MOUNT_FAT32) {
                info->kind = SYS_MOUNT_INFO_FAT32;
            } else if (i386_vfs.mounts[slot].kind == VFS_MOUNT_NXFS) {
                info->kind = SYS_MOUNT_INFO_NXFS;
            } else {
                info->kind = SYS_MOUNT_INFO_NONE;
            }
            info->disk_index = i386_vfs.mounts[slot].disk_index;
            info->part_index = i386_vfs.mounts[slot].part_index;
            info->source_known = 1u;
            i386_copy_text(info->target,
                           sizeof(info->target),
                           i386_vfs.mounts[slot].name);
            i386_mount_fill_space(info,
                                  i386_vfs.mounts[slot].kind,
                                  slot + 1u);
            return 1;
        }
        visible++;
    }
    return 0;
}

static int command_starts_with(const char *line, const char *command) {
    while (*command != '\0') {
        if (*line++ != *command++) {
            return 0;
        }
    }
    return *line == '\0' || *line == ' ';
}

static const char *command_argument(const char *line) {
    while (*line != '\0' && *line != ' ') {
        line++;
    }
    while (*line == ' ') {
        line++;
    }
    return line;
}

static void i386_prompt(void) {
    tty_write_str(i386_tty, "i386> ", 0x0bu);
    tty_show_prompt(i386_tty);
}

static int i386_pop_keyboard_event(struct keyboard_event *event) {
    struct i386_key_event raw;

    if (!i386_keyboard_pop(&raw)) {
        return 0;
    }
    *event = keyboard_handle_scancode(raw.scancode);
    keyboard_event_queue_push(event, i386_scheduler_ticks());
    return 1;
}

static int i386_inject_and_feed(uint8_t scancode) {
    struct keyboard_event event;

    if (!i386_keyboard_inject_scancode(scancode)) {
        return 0;
    }
    for (uint32_t ticks = 0; ticks < 100u; ticks++) {
        __asm__ volatile("sti; hlt" : : : "memory");
        if (i386_pop_keyboard_event(&event)) {
            tty_feed_key_event(i386_tty, &event);
            return 1;
        }
    }
    return 0;
}

static int i386_tty_input_self_test(void) {
    static const uint8_t scancodes[] = {
        0x1eu, 0x30u, 0x0eu, 0x2eu, 0x1cu
    };
    char line[TTY_LINE_MAX + 1u];

    tty_clear(i386_tty);
    i386_prompt();
    for (uint32_t i = 0; i < sizeof(scancodes); i++) {
        if (!i386_inject_and_feed(scancodes[i])) {
            return 0;
        }
    }
    return tty_has_line(i386_tty) &&
           tty_read(i386_tty, line, sizeof(line), TTY_READ_LINE) != 0u &&
           streq(line, "ac");
}

static void i386_command_ls(const char *path) {
    struct vfs_node directory;
    struct vfs_dirent entry;
    uint32_t index = 0;
    int64_t result;

    if (path == 0 || path[0] == '\0') {
        path = "/";
    }
    if (vfs_opendir(&i386_vfs, path, &directory) != 0) {
        tty_write_str(i386_tty, "ls: directory not found\n", 0x0cu);
        return;
    }

    while ((result = vfs_readdir(&i386_vfs,
                                 &directory,
                                 &index,
                                 &entry)) > 0) {
        tty_write_str(i386_tty, entry.name, 0x0fu);
        if ((entry.attributes & VFS_ATTR_DIR) != 0u) {
            tty_write_str(i386_tty, "/\n", 0x0au);
        } else {
            tty_write_str(i386_tty, "\n", 0x0fu);
        }
    }
    if (result < 0) {
        tty_write_str(i386_tty, "ls: read error\n", 0x0cu);
    }
}

static void i386_command_cat(const char *path) {
    struct vfs_node node;
    uint32_t offset = 0;
    char buffer[129];
    int64_t count;

    if (path == 0 || path[0] == '\0') {
        tty_write_str(i386_tty, "usage: cat <path>\n", 0x0eu);
        return;
    }
    if (vfs_open(&i386_vfs, path, 0u, &node) != 0) {
        tty_write_str(i386_tty, "cat: file not found\n", 0x0cu);
        return;
    }

    do {
        count = vfs_read(&i386_vfs,
                         &node,
                         &offset,
                         buffer,
                         sizeof(buffer) - 1u,
                         VFS_READ_BLOCKING);
        if (count > 0) {
            buffer[count] = '\0';
            tty_write(i386_tty, buffer, (uint32_t)count, 0x0fu);
        }
    } while (count > 0);
    if (count < 0) {
        tty_write_str(i386_tty, "\ncat: read error\n", 0x0cu);
    } else {
        tty_write_str(i386_tty, "\n", 0x0fu);
    }
}

static void i386_command_ps(void) {
    for (uint32_t slot = 0; slot < 8u; slot++) {
        struct process_snapshot snapshot;

        if (!i386_scheduler_process_snapshot(slot, &snapshot)) {
            continue;
        }
        tty_write_str(i386_tty, "pid=", 0x0fu);
        tty_write_dec(i386_tty, snapshot.pid, 0x0fu);
        tty_write_str(i386_tty, " state=", 0x0fu);
        tty_write_dec(i386_tty, snapshot.state, 0x0fu);
        tty_write_str(i386_tty, " exit=", 0x0fu);
        tty_write_dec(i386_tty, (uint32_t)snapshot.exit_code, 0x0fu);
        tty_write_str(i386_tty, " name=", 0x0fu);
        tty_write_str(i386_tty, snapshot.name, 0x0au);
        tty_write_str(i386_tty, "\n", 0x0fu);
    }
}

static int i386_run_test32_selftest(void) {
    struct boot_user_init_config config;

    i386_boot_user_init_config(&config);
    return boot_user_init_run_selftest(&config);
}

static int i386_run_nexbox32_full_smoke(void) {
    static const char *const commands[] = {
        "/BOOT/NEXBOX32.ELF echo nexbox32",
        "/BOOT/NEXBOX32.ELF help",
        "/BOOT/NEXBOX32.ELF uname -a",
        "/BOOT/NEXBOX32.ELF sysinfo",
        "/BOOT/NEXBOX32.ELF meminfo",
        "/BOOT/NEXBOX32.ELF minfo",
        "/BOOT/NEXBOX32.ELF parts",
        "/BOOT/NEXBOX32.ELF mounts",
        "/BOOT/NEXBOX32.ELF df",
        "/BOOT/NEXBOX32.ELF blk",
        "/BOOT/NEXBOX32.ELF ps",
        "/BOOT/NEXBOX32.ELF jobs",
        "/BOOT/NEXBOX32.ELF drivers",
        "/BOOT/NEXBOX32.ELF cat /proc/devices",
        "/BOOT/NEXBOX32.ELF cat /proc/mounts",
        "/BOOT/NEXBOX32.ELF cat /proc/drivers",
        "/BOOT/NEXBOX32.ELF dmesg",
        "/BOOT/NEXBOX32.ELF lspci",
        "/BOOT/NEXBOX32.ELF ac97",
        "/BOOT/NEXBOX32.ELF hda",
        "/BOOT/NEXBOX32.ELF rtl8139",
        "/BOOT/NEXBOX32.ELF audio",
    };
    struct boot_user_init_config config;
    struct process_snapshot process;

    i386_boot_user_init_config(&config);
    if (config.ops == 0 || config.ops->run_command == 0) {
        i386_boot_log("nexbox32: applet smoke unavailable");
        return 0;
    }
    for (uint32_t i = 0u; i < sizeof(commands) / sizeof(commands[0]); i++) {
        kprint("nexbox32: RUN %s\n", commands[i]);
        if (!config.ops->run_command(commands[i], &process) ||
            process.state != PROCESS_STATE_EXITED ||
            process.exit_code != 0) {
            kprint("nexbox32: FAIL %s status=%d\n",
                   commands[i],
                   process.exit_code);
            return 0;
        }
    }
    i386_boot_log("nexbox32: full applet smoke PASS");
    return 1;
}

static void i386_command_test32(void) {
    (void)i386_run_test32_selftest();
}

static void i386_command_run(const char *command) {
    struct boot_user_init_config config;

    if (command == 0 || command[0] == '\0') {
        tty_write_str(i386_tty,
                      "usage: run <path> [args...]\n",
                      0x0eu);
        return;
    }
    i386_boot_user_init_config(&config);
    (void)boot_user_init_run_command(&config, command);
}

static int i386_autostart_shell(void) {
    struct boot_user_init_config config;

    i386_boot_user_init_config(&config);
    return boot_user_init_autostart_shell(&config);
}

static int i386_has_shell_operator(const char *line) {
    while (*line != '\0') {
        if (*line == '|' || *line == '<' || *line == '>') {
            return 1;
        }
        line++;
    }
    return 0;
}

static void i386_command_shell(const char *line) {
    char command[512];
    static const char prefix[] = "/BOOT/NEXBOX32.ELF sh ";
    uint32_t used = 0u;

    for (uint32_t i = 0u; prefix[i] != '\0'; i++) {
        command[used++] = prefix[i];
    }
    while (*line != '\0' && used + 1u < sizeof(command)) {
        command[used++] = *line++;
    }
    if (*line != '\0') {
        tty_write_str(i386_tty, "sh: command too long\n", 0x0cu);
        return;
    }
    command[used] = '\0';
    i386_command_run(command);
}

static void i386_execute_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }
    if (i386_has_shell_operator(line)) {
        i386_command_shell(line);
    } else if (streq(line, "help")) {
        tty_write_str(i386_tty,
                      "commands: help clear ps test32 run <elf> [args] ls [path] cat <path>\n"
                      "shell: cmd1 | cmd2, cmd > file, cmd < file\n",
                      0x0fu);
    } else if (streq(line, "clear")) {
        tty_clear(i386_tty);
    } else if (command_starts_with(line, "ls")) {
        i386_command_ls(command_argument(line));
    } else if (command_starts_with(line, "cat")) {
        i386_command_cat(command_argument(line));
    } else if (streq(line, "ps")) {
        i386_command_ps();
    } else if (streq(line, "test32")) {
        i386_command_test32();
    } else if (command_starts_with(line, "run")) {
        i386_command_run(command_argument(line));
    } else {
        tty_write_str(i386_tty, "unknown command: ", 0x0cu);
        tty_write_str(i386_tty, line, 0x0fu);
        tty_write_str(i386_tty, "\n", 0x0fu);
    }
}

int kernel_i386_shared_services_init(void) {
    struct block_device *boot_disk;
    struct blockdev_partition partition;
    int dev_selftest = i386_boot_dev_selftest_enabled();

    tty_virtual_init_all(0u, (uint16_t)(hal_display_text_rows() - 1u), 0x0fu);
    i386_tty = tty_active();
    if (i386_tty == 0) {
        return 0;
    }

    tty_clear(i386_tty);
    kprint_init();
    kprint_set_tty(i386_tty);
    i386_boot_log("kernel: entered");
    i386_log_boot_info();
    kernel_boot_log_memmap_summary(i386_memmap_usable_total(), i386_query_memmap_count);
    i386_boot_log("kernel: paging init");
    kernel_boot_log_paging_root(i386_paging_kernel_root(), 0u, 0u);
    i386_boot_log("kernel: pmm init");
    kernel_boot_log_pmm(i386_pmm_total_pages(), i386_pmm_free_pages(), 0u, 0u);
    if (i386_query_fb_info.present != 0u) {
        uint64_t framebuffer_size =
            (uint64_t)i386_query_fb_info.pitch * i386_query_fb_info.height;

        kernel_boot_log_framebuffer(i386_query_fb_info.addr, framebuffer_size, 0, 0u);
    }
    i386_boot_log("kernel: framebuffer backbuffer skip");

    vfs_init(&i386_vfs);
    i386_boot_log("kernel: block devices");
    ramdisk_init_from_boot_modules(i386_boot_info);
    i386_log_block_devices();
    i386_boot_log("kernel: pci/ide/ata");
    i386_log_pci_ide_info();
    i386_log_ata_info();
    boot_disk = blockdev_get(0u);
    if (boot_disk == 0 ||
        blockdev_partition_get(boot_disk, 0u, &partition) != 0 ||
        fat32_mount(&i386_vfs.fat32,
                    boot_disk,
                    (uint32_t)partition.start_lba) != 0) {
        tty_write_str(i386_tty, "VFS: FAT32 root mount failed\n", 0x0cu);
        return 0;
    }

    i386_vfs.root_kind = VFS_MOUNT_FAT32;
    i386_vfs.root_slot = 0u;
    i386_mount_ramdisk();
    i386_boot_log("kernel: tty/process/vfs");
    if (dev_selftest && !i386_tty_input_self_test()) {
        tty_write_str(i386_tty, "TTY input self-test failed\n", 0x0cu);
        return 0;
    }
    i386_boot_log("root: FAT32 / mounted");
    i386_driver_model_register_builtins();
    i386_driver_model_init(&i386_vfs);
    i386_boot_log("pseudo fs: devfs procfs eventfs ready");
    i386_boot_log("kernel: system/init");
    if (dev_selftest) {
        if (!i386_run_test32_selftest()) {
            return 0;
        }
        if (!i386_run_nexbox32_full_smoke()) {
            return 0;
        }
        i386_boot_log("kernel: i386 dev selftests complete");
    }
    return 1;
}

void kernel_i386_shared_services_run(void) {
    char line[TTY_LINE_MAX + 1u];

    if (i386_autostart_shell()) {
        for (;;) {
            __asm__ volatile("sti; hlt" : : : "memory");
        }
    }
    i386_prompt();
    for (;;) {
        struct keyboard_event event;

        __asm__ volatile("sti; hlt" : : : "memory");
        while (i386_pop_keyboard_event(&event)) {
            tty_feed_key_event(i386_tty, &event);
        }
        if (tty_has_line(i386_tty)) {
            (void)tty_read(i386_tty, line, sizeof(line), TTY_READ_LINE);
            i386_execute_command(line);
            i386_prompt();
        }
    }
}
