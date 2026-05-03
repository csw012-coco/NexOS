#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static const char *sysinfo_skip_spaces_local(const char *text) {
    while (text != NULL && (*text == ' ' || *text == '\t')) {
        text++;
    }
    return text != NULL ? text : "";
}

static void sysinfo_write_date_time_local(const struct syscall_rtc_info *rtc) {
    write_dec(rtc->year);
    write_str("-");
    if (rtc->month < 10u) {
        write_str("0");
    }
    write_dec(rtc->month);
    write_str("-");
    if (rtc->day < 10u) {
        write_str("0");
    }
    write_dec(rtc->day);
    write_str(" ");
    if (rtc->hour < 10u) {
        write_str("0");
    }
    write_dec(rtc->hour);
    write_str(":");
    if (rtc->minute < 10u) {
        write_str("0");
    }
    write_dec(rtc->minute);
    write_str(":");
    if (rtc->second < 10u) {
        write_str("0");
    }
    write_dec(rtc->second);
}

static int sysinfo_memory_totals_local(uint64_t *total_out,
                                       uint64_t *usable_out,
                                       uint64_t *reserved_out,
                                       uint32_t *entries_out) {
    struct syscall_memmap_info mem;
    uint64_t total = 0;
    uint64_t usable = 0;
    uint64_t reserved = 0;
    uint32_t entries = 0;

    while (memmap_query(entries, &mem) > 0) {
        total += mem.length;
        if (mem.type == BOOTX_MEMMAP_USABLE ||
            mem.type == BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            usable += mem.length;
        } else {
            reserved += mem.length;
        }
        entries++;
    }
    if (total_out != NULL) {
        *total_out = total;
    }
    if (usable_out != NULL) {
        *usable_out = usable;
    }
    if (reserved_out != NULL) {
        *reserved_out = reserved;
    }
    if (entries_out != NULL) {
        *entries_out = entries;
    }
    return entries != 0u;
}

static void sysinfo_write_help_local(void) {
    write_str("usage: sysinfo [-a]\n");
    write_str("show NexOS system, kernel, CPU, memory, and boot information\n");
    write_str("  -a, --all   include CPUID and memory-map counters\n");
}

int cmd_sysinfo(int argc, char **argv) {
    struct syscall_machine_info machine;
    struct syscall_pmm_info pmm;
    struct syscall_boot_info boot;
    struct syscall_rtc_info rtc;
    uint64_t mem_total = 0;
    uint64_t mem_usable = 0;
    uint64_t mem_reserved = 0;
    uint32_t mem_entries = 0;
    int verbose = 0;

    if (argc > 2) {
        write_err_usage("sysinfo", " [-a]\n");
        return 1;
    }
    if (argc == 2) {
        if (streq_local(argv[1], "-h") || streq_local(argv[1], "--help")) {
            sysinfo_write_help_local();
            return 0;
        }
        if (streq_local(argv[1], "-a") || streq_local(argv[1], "--all")) {
            verbose = 1;
        } else {
            write_err_usage("sysinfo", " [-a]\n");
            return 1;
        }
    }

    if (machine_info_query(&machine) <= 0) {
        write_err_str("sysinfo: machine info query failed\n");
        return 1;
    }

    write_str("NexOS system information\n");
    write_str("os:       ");
    write_str(machine.os_name);
    write_str("\nkernel:   ");
    write_str(machine.kernel_name);
    write_str(" ");
    write_str(machine.kernel_version);
    write_str("\narch:     ");
    write_str(machine.arch_name);
    write_str("\nbuild:    ");
    write_str(machine.build_date);
    write_str("\nconsole:  ");
    write_dec(machine.text_columns);
    write_str("x");
    write_dec(machine.text_rows);
    write_str("\nuptime:   ");
    write_dec(ticks());
    write_str(" ticks\n");

    write_str("cpu:      ");
    write_str(sysinfo_skip_spaces_local(machine.cpu_brand));
    write_str("\nvendor:   ");
    write_str(machine.cpu_vendor);
    write_str("\n");

    if (pmm_query(&pmm) > 0) {
        write_str("memory:   ");
        write_human_size((uint64_t)pmm.free_pages * NOS_PAGE_SIZE);
        write_str(" free / ");
        write_human_size((uint64_t)pmm.total_pages * NOS_PAGE_SIZE);
        write_str(" total");
        write_str(" (");
        write_dec(pmm.used_pages);
        write_str(" pages used)\n");
    } else {
        write_str("memory:   unavailable\n");
    }

    if (rtc_query(&rtc) > 0 && rtc.present && rtc.valid) {
        write_str("rtc:      ");
        sysinfo_write_date_time_local(&rtc);
        write_str("\n");
    }

    if (boot_info_query(&boot) > 0) {
        write_str("boot:     drive=0x");
        write_hex_u32(boot.boot_drive);
        write_str(" lba=");
        write_dec(boot.partition_lba);
        write_str(" sectors=");
        write_dec(boot.partition_sectors);
        write_str(" modules=");
        write_dec(boot.module_count);
        write_str("\n");
    }

    if (verbose && sysinfo_memory_totals_local(&mem_total, &mem_usable, &mem_reserved, &mem_entries)) {
        write_str("memmap:   entries=");
        write_dec(mem_entries);
        write_str(" total=");
        write_human_size(mem_total);
        write_str(" usable=");
        write_human_size(mem_usable);
        write_str(" reserved=");
        write_human_size(mem_reserved);
        write_str("\ncpuid0:   eax=");
        write_hex_u32(machine.cpuid_leaf0_eax);
        write_str(" ebx=");
        write_hex_u32(machine.cpuid_leaf0_ebx);
        write_str(" ecx=");
        write_hex_u32(machine.cpuid_leaf0_ecx);
        write_str(" edx=");
        write_hex_u32(machine.cpuid_leaf0_edx);
        write_str("\ncpuid1:   eax=");
        write_hex_u32(machine.cpuid_leaf1_eax);
        write_str(" ebx=");
        write_hex_u32(machine.cpuid_leaf1_ebx);
        write_str(" ecx=");
        write_hex_u32(machine.cpuid_leaf1_ecx);
        write_str(" edx=");
        write_hex_u32(machine.cpuid_leaf1_edx);
        write_str("\n");
    }
    return 0;
}
