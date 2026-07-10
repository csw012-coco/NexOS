#include "user/apps/elf/nexbox/applets/debug/cmdsuite_debug_common.h"

int cmd_meminfo(void) {
    struct syscall_memmap_info meminfo;
    struct syscall_pmm_info pmminfo;
    struct syscall_boot_info bootinfo;
    uint64_t total = 0;
    uint64_t usable = 0;
    uint64_t reclaimable = 0;
    uint64_t reserved = 0;
    uint32_t entries = 0;

    while (memmap_query(entries, &meminfo) > 0) {
        total += meminfo.length;
        if (meminfo.type == BOOTX_MEMMAP_USABLE) {
            usable += meminfo.length;
        } else if (meminfo.type == BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            reclaimable += meminfo.length;
        } else {
            reserved += meminfo.length;
        }
        entries++;
    }
    if (pmm_query(&pmminfo) <= 0) {
        write_err_str("meminfo: pmm query failed\n");
        return 1;
    }
    if (boot_info_query(&bootinfo) <= 0) {
        write_err_str("meminfo: boot info query failed\n");
        return 1;
    }
    write_str("memory summary\n");
    write_str("map entries=");
    write_dec(entries);
    write_str(" total=");
    write_human_size(total);
    write_str(" usable=");
    write_human_size(usable);
    write_str(" reclaim=");
    write_human_size(reclaimable);
    write_str(" reserved=");
    write_human_size(reserved);
    write_str("\n");
    write_str("pmm total=");
    write_dec(pmminfo.total_pages);
    write_str(" free=");
    write_dec(pmminfo.free_pages);
    write_str(" used=");
    write_dec(pmminfo.used_pages);
    write_str(" dropped=");
    write_dec(pmminfo.dropped_pages);
    write_str("\n");
    write_str("boot drive=0x");
    write_hex_u32(bootinfo.boot_drive);
    write_str(" part_lba=");
    write_dec(bootinfo.partition_lba);
    write_str(" sectors=");
    write_dec(bootinfo.partition_sectors);
    write_str(" modules=");
    write_dec(bootinfo.module_count);
    write_str("\n");
    return 0;
}

int cmd_minfo(void) {
    struct syscall_machine_info info;
    const char *brand;

    if (!cmdsuite_debug_query_machine_info(&info)) {
        write_err_str("minfo: machine info query failed\n");
        return 1;
    }
    brand = cmdsuite_debug_skip_spaces(info.cpu_brand);

    write_str("machine info\n");
    write_str("system: ");
    write_str(info.os_name);
    write_str(" ");
    write_str(info.kernel_name);
    write_str(" ");
    write_str(info.kernel_version);
    write_str("\narch:   ");
    write_str(info.arch_name);
    write_str("\nbuild:  ");
    write_str(info.build_date);
    write_str("\ncpu:    ");
    write_str(brand);
    write_str("\nvendor: ");
    write_str(info.cpu_vendor);
    write_str("\n");
    return 0;
}

int cmd_uname(int argc, char **argv) {
    struct syscall_machine_info info;

    if (!cmdsuite_debug_query_machine_info(&info)) {
        write_err_str("uname: machine info query failed\n");
        return 1;
    }
    if (argc > 2) {
        write_err_usage("uname", " [-a]\n");
        return 1;
    }
    if (argc == 2) {
        if (!streq_local(argv[1], "-a")) {
            write_err_usage("uname", " [-a]\n");
            return 1;
        }
        write_str(info.os_name);
        write_str(" ");
        write_str(info.kernel_name);
        write_str(" ");
        write_str(info.kernel_version);
        write_str(" ");
        write_str(info.build_date);
        write_str(" ");
        write_str(info.arch_name);
        write_str("\n");
        return 0;
    }
    write_str(info.os_name);
    write_str("\n");
    return 0;
}

int cmd_fb(int argc, char **argv) {
    struct syscall_framebuffer_info info;

    (void)argv;
    if (argc != 1) {
        write_err_usage("fb", "\n");
        return 1;
    }
    if (framebuffer_query(&info) <= 0) {
        write_err_str("fb: framebuffer query failed\n");
        return 1;
    }
    write_str("framebuffer info\n");
    write_str("console: ");
    if (info.type == 2u) {
        write_str("framebuffer\n");
    } else if (info.type == 1u) {
        write_str("text\n");
    } else {
        write_str("none\n");
    }
    write_str("present: ");
    write_dec(info.present);
    write_str("\naddr:    0x");
    write_hex_u64(info.addr);
    write_str("\nsize:    ");
    write_dec(info.width);
    write_str("x");
    write_dec(info.height);
    write_str(" pitch=");
    write_dec(info.pitch);
    write_str(" bpp=");
    write_dec(info.bpp);
    write_str("\nrgb:     r");
    write_dec(info.red_mask_size);
    write_str("@");
    write_dec(info.red_mask_shift);
    write_str(" g");
    write_dec(info.green_mask_size);
    write_str("@");
    write_dec(info.green_mask_shift);
    write_str(" b");
    write_dec(info.blue_mask_size);
    write_str("@");
    write_dec(info.blue_mask_shift);
    write_str("\ntext:    ");
    write_dec(info.text_columns);
    write_str("x");
    write_dec(info.text_rows);
    write_str(" color=0x");
    write_hex_u32(info.text_color);
    write_str("\n");
    return 0;
}

int cmd_drivers(int argc, char **argv) {
    char buffer[256];
    int fd;
    int printed = 0;

    if (argc > 1 &&
        (streq_local(argv[1], "-h") || streq_local(argv[1], "--help"))) {
        write_err_usage("drivers", "\n");
        return 0;
    }
    if (argc != 1) {
        write_err_usage("drivers", "\n");
        return 1;
    }
    fd = open("/proc/drivers", O_RDONLY);
    if (fd < 0) {
        write_err_str("drivers: cannot open /proc/drivers\n");
        return 1;
    }
    for (;;) {
        ssize_t got = read(fd, buffer, sizeof(buffer));

        if (got < 0) {
            close(fd);
            write_err_str("drivers: read failed\n");
            return 1;
        }
        if (got == 0) {
            break;
        }
        write_stdout(buffer, (uint32_t)got);
        printed = 1;
    }
    close(fd);
    if (!printed) {
        write_str("(no drivers)\n");
    }
    return 0;
}
