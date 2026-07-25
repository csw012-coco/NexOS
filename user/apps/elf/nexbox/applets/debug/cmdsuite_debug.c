#include "user/apps/elf/nexbox/applets/debug/cmdsuite_debug_common.h"

static const char *memmap_type_name_local(uint32_t type) {
    switch (type) {
        case BOOTX_MEMMAP_USABLE: return "usable";
        case BOOTX_MEMMAP_RESERVED: return "reserved";
        case BOOTX_MEMMAP_ACPI_RECLAIMABLE: return "acpi reclaim";
        case BOOTX_MEMMAP_ACPI_NVS: return "acpi nvs";
        case BOOTX_MEMMAP_BAD: return "bad";
        case BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE: return "bootloader";
        default: return "unknown";
    }
}

static void dump_bytes_local(const uint8_t *data, uint32_t count) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t row;

    for (row = 0; row < count; row += 16u) {
        uint32_t i;

        write_hex_u32(row);
        write_str(": ");
        for (i = 0; i < 16u && row + i < count; i++) {
            char text[3];
            uint8_t byte = data[row + i];

            text[0] = hex[(byte >> 4) & 0x0fu];
            text[1] = hex[byte & 0x0fu];
            text[2] = ' ';
            write_stdout(text, 3);
        }
        write_str("\n");
    }
}

static void write_u64_dec_local(uint64_t value) {
    char text[24];
    uint32_t pos = sizeof(text);

    text[--pos] = '\0';
    do {
        text[--pos] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    write_str(text + pos);
}

static int cmd_dbg_profile_local(int argc, char **argv) {
    struct syscall_profile_info info;
    uint32_t index = 0u;
    uint32_t flags = 0u;

    if (argc > 3 ||
        (argc == 3 && !streq_local(argv[2], "reset"))) {
        write_err_usage("dbg profile", " [reset]\n");
        return 1;
    }
    if (argc == 3) {
        flags = SYS_PROFILE_QUERY_RESET;
    }
    write_str("profile counters (units are counter-specific)\n");
    while (profile_query(index, flags, &info) > 0) {
        uint64_t average = info.calls != 0u ? info.cycles / info.calls : 0u;

        write_dec(index);
        write_str(" ");
        write_text_padded(info.name, SYS_PROFILE_NAME_MAX - 1u);
        write_str(" calls=");
        write_u64_dec_local(info.calls);
        write_str(" cycles=");
        write_u64_dec_local(info.cycles);
        write_str(" avg=");
        write_u64_dec_local(average);
        write_str(" units=");
        write_u64_dec_local(info.units);
        write_str("\n");
        flags = 0u;
        index++;
    }
    if (index == 0u) {
        write_str("(no profile counters)\n");
    } else if (argc == 3) {
        write_str("profile counters reset\n");
    }
    return 0;
}

struct cmd_driver_status {
    int found;
    char state[24];
    char reason[24];
    char result[16];
    char source[24];
    char device[32];
    char path[96];
};

static void cmd_copy_token_local(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0u;

    if (dst == NULL || dst_size == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t cmd_split_driver_line_local(char *line, char **tokens, uint32_t max_tokens) {
    uint32_t count = 0u;
    char *p = line;

    while (p != NULL && *p != '\0' && count < max_tokens) {
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        tokens[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        *p++ = '\0';
    }
    return count;
}

static void cmd_driver_status_from_tokens_local(struct cmd_driver_status *status,
                                                char **tokens,
                                                uint32_t token_count) {
    if (status == NULL || tokens == NULL || token_count < 8u) {
        return;
    }
    status->found = 1;
    cmd_copy_token_local(status->state, sizeof(status->state), tokens[3]);
    cmd_copy_token_local(status->result, sizeof(status->result), tokens[4]);
    cmd_copy_token_local(status->source, sizeof(status->source), tokens[5]);
    cmd_copy_token_local(status->reason, sizeof(status->reason), tokens[7]);
    cmd_copy_token_local(status->device,
                         sizeof(status->device),
                         token_count > 8u ? tokens[8] : "-");
    cmd_copy_token_local(status->path,
                         sizeof(status->path),
                         token_count > 9u ? tokens[9] : "-");
}

static int cmd_query_driver_status_local(const char *driver_name, struct cmd_driver_status *status) {
    int fd;
    struct cmd_driver_status file_status;
    char line[256];
    uint32_t line_len = 0u;

    if (driver_name == NULL || status == NULL) {
        return 0;
    }
    memset(status, 0, sizeof(*status));
    memset(&file_status, 0, sizeof(file_status));
    fd = open("/proc/drivers", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    for (;;) {
        char ch;
        char *tokens[12];
        uint32_t token_count;
        ssize_t got = read(fd, &ch, 1u);

        if (got < 0) {
            close(fd);
            return 0;
        }
        if (got > 0 && ch != '\n') {
            if (line_len + 1u < sizeof(line)) {
                line[line_len++] = ch;
            }
            continue;
        }
        if (got == 0 && line_len == 0u) {
            break;
        }
        line[line_len] = '\0';
        token_count = cmd_split_driver_line_local(line, tokens, 12u);
        if (token_count >= 8u && !streq_local(tokens[0], "name")) {
            int name_matches = streq_local(tokens[0], driver_name);
            int driver_matches = token_count > 2u && streq_local(tokens[2], driver_name);

            if ((name_matches || driver_matches) && !streq_local(tokens[1], "file")) {
                cmd_driver_status_from_tokens_local(status, tokens, token_count);
                close(fd);
                return 1;
            }
            if (driver_matches && streq_local(tokens[1], "file") && !file_status.found) {
                cmd_driver_status_from_tokens_local(&file_status, tokens, token_count);
            }
        }
        line_len = 0u;
        if (got == 0) {
            break;
        }
    }
    close(fd);
    if (file_status.found) {
        *status = file_status;
        return 1;
    }
    return 0;
}

static void cmd_print_driver_status_local(const char *label, const char *driver_name) {
    struct cmd_driver_status status;

    write_str(label);
    write_str(": driver ");
    write_str(driver_name);
    if (!cmd_query_driver_status_local(driver_name, &status)) {
        write_str(" not registered\n");
        return;
    }
    write_str(" state=");
    write_str(status.state[0] != '\0' ? status.state : "-");
    write_str(" reason=");
    write_str(status.reason[0] != '\0' ? status.reason : "-");
    write_str(" result=");
    write_str(status.result[0] != '\0' ? status.result : "-");
    write_str(" source=");
    write_str(status.source[0] != '\0' ? status.source : "-");
    if (status.device[0] != '\0' && !streq_local(status.device, "-")) {
        write_str(" device=");
        write_str(status.device);
    }
    if (status.path[0] != '\0' && !streq_local(status.path, "-")) {
        write_str(" path=");
        write_str(status.path);
    }
    write_str("\n");
}

int cmd_dmesg(void) {
    struct syscall_kmsg_info info;
    uint32_t offset = 0;
    int printed = 0;
    int queried = 0;

    while (kmsg_query(offset, &info) > 0) {
        queried = 1;
        if (info.bytes_copied == 0) {
            break;
        }
        write_stdout(info.data, info.bytes_copied);
        offset += info.bytes_copied;
        printed = 1;
    }
    if (!printed) {
        write_str(queried ? "(kernel log empty)\n" : "(kernel log unavailable)\n");
    }
    return 0;
}

int cmd_lspci(void) {
    struct syscall_pci_info info;
    uint32_t index = 0;
    uint32_t printed = 0;

    write_str("PCI devices\n");

    while (pci_query_at(index, &info) > 0 && info.present) {
        dprintf(STDOUT_FILENO,
                "%u %u:%u.%u class=%08X prog_if=%08X vendor=%08X device=%08X irq=%u pin=%u\n",
                index,
                info.bus,
                info.slot,
                info.function,
                (info.class_code << 8) | info.subclass,
                info.prog_if,
                info.vendor_id,
                info.device_id,
                info.irq_line,
                info.irq_pin);
        dprintf(STDOUT_FILENO,
                "  bars %08X %08X %08X %08X %08X %08X\n",
                info.bar0,
                info.bar1,
                info.bar2,
                info.bar3,
                info.bar4,
                info.bar5);
        printed = 1;
        index++;
    }

    if (!printed) {
        write_str("(no PCI devices)\n");
    }
    return 0;
}

int cmd_ac97(void) {
    struct syscall_ac97_info info;

    cmd_print_driver_status_local("ac97", "AC97");
    if (ac97_query(&info) <= 0 || !info.present) {
        write_str("ac97: controller not found\n");
        return 0;
    }
    write_str("AC97 controller\n");
    write_str("bdf ");
    write_dec(info.bus);
    write_str(":");
    write_dec(info.slot);
    write_str(".");
    write_dec(info.function);
    write_str(" vendor=");
    write_hex_u32(info.vendor_id);
    write_str(" device=");
    write_hex_u32(info.device_id);
    write_str(" irq=");
    write_dec(info.irq_line);
    write_str(" pin=");
    write_dec(info.irq_pin);
    write_str("\n");
    write_str("io nambar=");
    write_hex_u32(info.nambar);
    write_str(" nabmbar=");
    write_hex_u32(info.nabmbar);
    write_str(" prog_if=");
    write_hex_u32(info.prog_if);
    write_str("\n");
    write_str("codec id=");
    write_hex_u32(info.codec_id);
    write_str(" power=");
    write_hex_u32(info.powerdown);
    write_str(" ext_id=");
    write_hex_u32(info.ext_audio_id);
    write_str(" ext_ctrl=");
    write_hex_u32(info.ext_audio_ctrl);
    write_str("\n");
    write_str("global sta=");
    write_hex_u32(info.global_status);
    write_str(" cnt=");
    write_hex_u32(info.global_control);
    write_str(" init=");
    write_dec(info.initialized);
    write_str("\n");
    return 0;
}

int cmd_hda(void) {
    struct syscall_hda_info info;

    cmd_print_driver_status_local("hda", "HDA");
    if (hda_query(&info) <= 0 || !info.present) {
        write_str("hda: controller not found\n");
        return 0;
    }
    write_str("HD Audio controller\n");
    write_str("bdf ");
    write_dec(info.bus);
    write_str(":");
    write_dec(info.slot);
    write_str(".");
    write_dec(info.function);
    write_str(" vendor=");
    write_hex_u32(info.vendor_id);
    write_str(" device=");
    write_hex_u32(info.device_id);
    write_str(" irq=");
    write_dec(info.irq_line);
    write_str(" pin=");
    write_dec(info.irq_pin);
    write_str("\n");
    write_str("mmio hi=");
    write_hex_u32(info.mmio_base_hi);
    write_str(" lo=");
    write_hex_u32(info.mmio_base_lo);
    write_str(" pci_cmd=");
    write_hex_u32(info.pci_command);
    write_str(" prog_if=");
    write_hex_u32(info.prog_if);
    write_str("\n");
    write_str("version ");
    write_dec(info.vmaj);
    write_str(".");
    write_dec(info.vmin);
    write_str(" gcap=");
    write_hex_u32(info.gcap);
    write_str(" inpay=");
    write_hex_u32(info.inpay);
    write_str(" outpay=");
    write_hex_u32(info.outpay);
    write_str("\n");
    write_str("gctl=");
    write_hex_u32(info.gctl);
    write_str(" statests=");
    write_hex_u32(info.statests);
    write_str(" wakeen=");
    write_hex_u32(info.wakeen);
    write_str(" codec_mask=");
    write_hex_u32(info.codec_mask);
    write_str(" init=");
    write_dec(info.initialized);
    write_str("\n");
    write_str("rings corb_size=");
    write_hex_u32(info.corb_size);
    write_str(" rirb_size=");
    write_hex_u32(info.rirb_size);
    write_str("\n");
    return 0;
}

int cmd_meminfo(void) {
    struct syscall_memmap_info meminfo;
    struct syscall_pmm_info pmminfo;
    struct syscall_vm_info vminfo;
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
    if (vm_query(&vminfo) > 0) {
        write_str("vm mmap_regions=");
        write_dec(vminfo.mmap_regions);
        write_str("/");
        write_dec(vminfo.mmap_region_capacity);
        write_str(" mmap_pages=");
        write_dec(vminfo.mmap_pages);
        write_str(" shared_regions=");
        write_dec(vminfo.shared_regions);
        write_str(" shm_objects=");
        write_dec(vminfo.shm_objects);
        write_str("/");
        write_dec(vminfo.shm_object_capacity);
        write_str(" shm_pages=");
        write_dec(vminfo.shm_mapped_pages);
        write_str(" stack_pages=");
        write_dec(vminfo.user_stack_pages);
        write_str("\n");
    }
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

int cmd_cpuinfo(void) {
    struct syscall_machine_info info;
    const char *brand;

    if (!cmdsuite_debug_query_machine_info(&info)) {
        write_err_str("cpuinfo: machine info query failed\n");
        return 1;
    }
    brand = cmdsuite_debug_skip_spaces(info.cpu_brand);

    write_str("cpu info\n");
    write_str("arch:   ");
    write_str(info.arch_name);
    write_str("\nvendor: ");
    write_str(info.cpu_vendor);
    write_str("\nbrand:  ");
    write_str(brand);
    write_str("\ncpuid leaf0: eax=");
    write_hex_u32(info.cpuid_leaf0_eax);
    write_str(" ebx=");
    write_hex_u32(info.cpuid_leaf0_ebx);
    write_str(" ecx=");
    write_hex_u32(info.cpuid_leaf0_ecx);
    write_str(" edx=");
    write_hex_u32(info.cpuid_leaf0_edx);
    write_str("\ncpuid leaf1: eax=");
    write_hex_u32(info.cpuid_leaf1_eax);
    write_str(" ebx=");
    write_hex_u32(info.cpuid_leaf1_ebx);
    write_str(" ecx=");
    write_hex_u32(info.cpuid_leaf1_ecx);
    write_str(" edx=");
    write_hex_u32(info.cpuid_leaf1_edx);
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

int cmd_dbg(int argc, char **argv) {
    struct syscall_memmap_info meminfo;
    struct syscall_pmm_info pmminfo;
    struct syscall_boot_info bootinfo;
    struct syscall_block_read_info blockinfo;
    uint32_t value;
    uint32_t disk_index = 0;
    uint32_t i;

    if (argc < 2) {
        write_err_usage("dbg", " <mem|pmm|ticks|info|profile|pagealloc|pagefree|read>\n");
        return 1;
    }
    if (streq_local(argv[1], "mem")) {
        write_str("memory map\n");
        for (i = 0; memmap_query(i, &meminfo) > 0; i++) {
            write_str("#");
            write_dec(i);
            write_str(": base=");
            write_hex_u64(meminfo.base);
            write_str(" len=");
            write_hex_u64(meminfo.length);
            write_str(" ");
            write_str(memmap_type_name_local(meminfo.type));
            write_str("\n");
        }
        return 0;
    }
    if (streq_local(argv[1], "pmm")) {
        if (pmm_query(&pmminfo) <= 0) {
            write_err_str("pmm query failed\n");
            return 1;
        }
        write_str("physical memory manager\n");
        write_str("total pages: ");
        write_dec(pmminfo.total_pages);
        write_str("\nfree pages: ");
        write_dec(pmminfo.free_pages);
        write_str("\nused pages: ");
        write_dec(pmminfo.used_pages);
        write_str("\ndropped pages: ");
        write_dec(pmminfo.dropped_pages);
        write_str("\n");
        return 0;
    }
    if (streq_local(argv[1], "ticks")) {
        write_str("timer ticks: ");
        write_dec(ticks());
        write_str("\n");
        return 0;
    }
    if (streq_local(argv[1], "info")) {
        if (boot_info_query(&bootinfo) <= 0) {
            write_err_str("boot info query failed\n");
            return 1;
        }
        write_str("boot drive: 0x");
        write_hex_u32(bootinfo.boot_drive);
        write_str("\npartition lba: ");
        write_dec(bootinfo.partition_lba);
        write_str("\npartition sectors: ");
        write_dec(bootinfo.partition_sectors);
        write_str("\nmodules: ");
        write_dec(bootinfo.module_count);
        write_str("\n");
        return 0;
    }
    if (streq_local(argv[1], "profile")) {
        return cmd_dbg_profile_local(argc, argv);
    }
    if (streq_local(argv[1], "pagealloc")) {
        uint64_t page = page_alloc();

        if (page == 0) {
            write_err_str("page alloc failed\n");
            return 1;
        }
        write_str("allocated page: ");
        write_hex_u64(page);
        write_str("\n");
        return 0;
    }
    if (streq_local(argv[1], "pagefree")) {
        uint64_t page = 0;

        if (argc < 3 || !parse_u32_local(argv[2], &value)) {
            write_err_usage("dbg pagefree", " <user_page_addr>\n");
            return 1;
        }
        page = value;
        if (page_free(page) == 0) {
            write_err_str("page free failed\n");
            return 1;
        }
        write_str("freed page: ");
        write_hex_u64(page);
        write_str("\n");
        return 0;
    }
    if (streq_local(argv[1], "read")) {
        uint32_t lba;

        if (argc < 3) {
            write_err_usage("dbg read", " [disk] <lba>\n");
            return 1;
        }
        if (argc >= 4) {
            if (!parse_u32_local(argv[2], &disk_index) || !parse_u32_local(argv[3], &lba)) {
                write_err_usage("dbg read", " [disk] <lba>\n");
                return 1;
            }
        } else {
            if (!parse_u32_local(argv[2], &lba)) {
                write_err_usage("dbg read", " [disk] <lba>\n");
                return 1;
            }
        }
        if (block_read(disk_index, lba, &blockinfo) <= 0) {
            write_err_str("read failed\n");
            return 1;
        }
        write_str("read disk");
        write_dec(disk_index);
        write_str(" lba ");
        write_dec(lba);
        write_str("\n");
        dump_bytes_local(blockinfo.data, blockinfo.bytes_read < 64u ? blockinfo.bytes_read : 64u);
        return 0;
    }
    write_err_str("unknown dbg command: ");
    write_err_str(argv[1]);
    write_err_str("\n");
    return 1;
}
