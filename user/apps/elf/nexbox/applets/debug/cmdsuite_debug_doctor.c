#include "user/apps/elf/nexbox/applets/debug/cmdsuite_debug_common.h"

static void doctor_prefix_local(const char *name, const char *status) {
    write_text_padded(name, 12u);
    write_text_padded(status, 7u);
}

static void doctor_table_escape_local(const char *text) {
    uint32_t i = 0u;

    while (text != 0 && text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            write_str("\\");
        }
        write_stdout(&text[i], 1u);
        i++;
    }
}

static void doctor_table_begin_local(const char *name, const char *status) {
    write_str(name);
    write_str(" ");
    write_str(status);
    write_str(" \"");
}

static void doctor_table_end_local(void) {
    write_str("\"\n");
}

static void doctor_count_mounts_local(uint32_t *mounts_out,
                                      uint32_t *procfs_out,
                                      uint32_t *eventfs_out) {
    struct syscall_mount_info info;
    uint32_t index;

    *mounts_out = 0u;
    *procfs_out = 0u;
    *eventfs_out = 0u;
    for (index = 0u; mount_query(index, &info) > 0; index++) {
        *mounts_out += 1u;
        if (info.kind == NEX_MOUNT_INFO_PROCFS) {
            *procfs_out += 1u;
        } else if (info.kind == NEX_MOUNT_INFO_EVENTFS) {
            *eventfs_out += 1u;
        }
    }
}

static int doctor_dir_exists_local(const char *path) {
    int fd = opendir(path);

    if (fd < 0) {
        return 0;
    }
    close((uint32_t)fd);
    return 1;
}

static void doctor_count_pseudo_fs_local(uint32_t *procfs_out,
                                         uint32_t *eventfs_out) {
    if (procfs_out != 0 && *procfs_out == 0u &&
        doctor_dir_exists_local("/proc")) {
        *procfs_out = 1u;
    }
    if (eventfs_out != 0 && *eventfs_out == 0u &&
        doctor_dir_exists_local("/event")) {
        *eventfs_out = 1u;
    }
}

static uint32_t doctor_count_blocks_local(void) {
    struct syscall_block_info info;
    uint32_t index;

    for (index = 0u; block_query(index, &info) > 0; index++) {
    }
    return index;
}

static uint32_t doctor_count_processes_local(void) {
    struct syscall_process_info info;
    uint32_t index;
    uint32_t count = 0u;

    for (index = 0u; index < NEX_PROC_SLOTS_MAX; index++) {
        if (proc_query(NEX_PROC_QUERY_ALL, index, &info) <= 0) {
            continue;
        }
        if (info.state != NEX_PROC_STATE_FREE) {
            count++;
        }
    }
    return count;
}

struct doctor_driver_counts {
    uint32_t records;
    uint32_t active;
    uint32_t inactive;
    uint32_t failed;
    uint32_t files;
    uint32_t loaded_files;
    uint32_t invalid_files;
};

static void doctor_parse_driver_line_local(char *line, struct doctor_driver_counts *counts) {
    char *tokens[10];
    uint32_t token_count = 0u;
    char *p = line;

    if (line == NULL || counts == NULL) {
        return;
    }
    while (*p != '\0' && token_count < 10u) {
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        tokens[token_count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        *p++ = '\0';
    }
    if (token_count < 4u || streq_local(tokens[0], "name")) {
        return;
    }
    if (streq_local(tokens[1], "file")) {
        counts->files++;
        if (streq_local(tokens[3], "loaded")) {
            counts->loaded_files++;
        } else if (streq_local(tokens[3], "elf-invalid")) {
            counts->invalid_files++;
        }
        return;
    }
    counts->records++;
    if (streq_local(tokens[3], "active")) {
        counts->active++;
    } else if (streq_local(tokens[3], "inactive")) {
        counts->inactive++;
    } else if (streq_local(tokens[3], "failed")) {
        counts->failed++;
    }
}

static int doctor_count_drivers_local(struct doctor_driver_counts *counts) {
    int fd;
    char line[256];
    uint32_t line_len = 0u;

    if (counts == NULL) {
        return 0;
    }
    memset(counts, 0, sizeof(*counts));
    fd = open("/proc/drivers", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    for (;;) {
        char ch;
        ssize_t got = read(fd, &ch, 1u);

        if (got < 0) {
            close((uint32_t)fd);
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
        doctor_parse_driver_line_local(line, counts);
        line_len = 0u;
        if (got == 0) {
            break;
        }
    }
    close((uint32_t)fd);
    return 1;
}

static uint32_t doctor_count_pci_local(void) {
    struct syscall_pci_info info;
    uint32_t index;

    for (index = 0u; pci_query_at(index, &info) > 0 && info.present; index++) {
    }
    return index;
}

static int doctor_kmsg_available_local(uint32_t *bytes_out) {
    struct syscall_kmsg_info info;

    if (bytes_out != NULL) {
        *bytes_out = 0u;
    }
    if (kmsg_query(0u, &info) <= 0) {
        return 0;
    }
    if (bytes_out != NULL) {
        *bytes_out = info.total_size;
    }
    return 1;
}

int cmd_doctor(int argc, char **argv) {
    struct syscall_machine_info machine;
    struct syscall_pmm_info pmm;
    struct syscall_rtc_info rtc;
    struct syscall_rtl8139_info nic;
    struct syscall_audio_info audio;
    struct doctor_driver_counts driver_counts;
    uint32_t mounts;
    uint32_t procfs_mounts;
    uint32_t eventfs_mounts;
    uint32_t blocks;
    uint32_t procs;
    uint32_t pci_devices;
    uint32_t kmsg_bytes;
    uint32_t warnings = 0u;
    uint32_t failures = 0u;
    int table = 0;
    const char *brand;

    if (argc == 2 && streq_local(argv[1], "--table")) {
        table = 1;
    } else if (argc != 1) {
        write_err_usage("doctor", " [--table]\n");
        return 1;
    }

    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: check status detail\n");
        write_str("check status detail\n");
    } else {
        write_str("NexOS doctor\n\n");
    }

    if (cmdsuite_debug_query_machine_info(&machine)) {
        brand = cmdsuite_debug_skip_spaces(machine.cpu_brand);
        if (table) {
            doctor_table_begin_local("kernel", "ok");
            doctor_table_escape_local(machine.os_name);
            write_str(" ");
            doctor_table_escape_local(machine.kernel_version);
            write_str(" ");
            doctor_table_escape_local(machine.arch_name);
            if (brand[0] != '\0') {
                write_str(", ");
                doctor_table_escape_local(brand);
            }
            doctor_table_end_local();
        } else {
            doctor_prefix_local("kernel", "ok");
            write_str(machine.os_name);
            write_str(" ");
            write_str(machine.kernel_version);
            write_str(" ");
            write_str(machine.arch_name);
            if (brand[0] != '\0') {
                write_str(", ");
                write_str(brand);
            }
            write_str("\n");
        }
    } else {
        if (table) {
            doctor_table_begin_local("kernel", "fail");
            write_str("machine info query failed");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("kernel", "fail");
            write_str("machine info query failed\n");
        }
        failures++;
    }

    if (pmm_query(&pmm) > 0) {
        if (table) {
            doctor_table_begin_local("memory", "ok");
            write_dec(pmm.free_pages);
            write_str("/");
            write_dec(pmm.total_pages);
            write_str(" pages free, used=");
            write_dec(pmm.used_pages);
            write_str(" dropped=");
            write_dec(pmm.dropped_pages);
            doctor_table_end_local();
        } else {
            doctor_prefix_local("memory", "ok");
            write_dec(pmm.free_pages);
            write_str("/");
            write_dec(pmm.total_pages);
            write_str(" pages free, used=");
            write_dec(pmm.used_pages);
            write_str(" dropped=");
            write_dec(pmm.dropped_pages);
            write_str("\n");
        }
    } else {
        if (table) {
            doctor_table_begin_local("memory", "fail");
            write_str("pmm query failed");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("memory", "fail");
            write_str("pmm query failed\n");
        }
        failures++;
    }

    doctor_count_mounts_local(&mounts, &procfs_mounts, &eventfs_mounts);
    doctor_count_pseudo_fs_local(&procfs_mounts, &eventfs_mounts);
    if (procfs_mounts > 0u) {
        if (table) {
            doctor_table_begin_local("procfs", "ok");
            write_dec(procfs_mounts);
            write_str(" procfs mount");
            write_str(procfs_mounts == 1u ? "" : "s");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("procfs", "ok");
            write_dec(procfs_mounts);
            write_str(" procfs mount");
            write_str(procfs_mounts == 1u ? "" : "s");
            write_str("\n");
        }
    } else {
        if (table) {
            doctor_table_begin_local("procfs", "fail");
            write_str("not mounted");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("procfs", "fail");
            write_str("not mounted\n");
        }
        failures++;
    }

    if (eventfs_mounts > 0u) {
        if (table) {
            doctor_table_begin_local("eventfs", "ok");
            write_dec(eventfs_mounts);
            write_str(" eventfs mount");
            write_str(eventfs_mounts == 1u ? "" : "s");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("eventfs", "ok");
            write_dec(eventfs_mounts);
            write_str(" eventfs mount");
            write_str(eventfs_mounts == 1u ? "" : "s");
            write_str("\n");
        }
    } else {
        if (table) {
            doctor_table_begin_local("eventfs", "warn");
            write_str("not mounted");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("eventfs", "warn");
            write_str("not mounted\n");
        }
        warnings++;
    }

    blocks = doctor_count_blocks_local();
    if (table) {
        doctor_table_begin_local("storage", blocks > 0u ? "ok" : "warn");
        write_dec(blocks);
        write_str(" block device");
        write_str(blocks == 1u ? "" : "s");
        write_str(", ");
        write_dec(mounts);
        write_str(" mount");
        write_str(mounts == 1u ? "" : "s");
        doctor_table_end_local();
    } else {
        doctor_prefix_local("storage", blocks > 0u ? "ok" : "warn");
        write_dec(blocks);
        write_str(" block device");
        write_str(blocks == 1u ? "" : "s");
        write_str(", ");
        write_dec(mounts);
        write_str(" mount");
        write_str(mounts == 1u ? "" : "s");
        write_str("\n");
    }
    if (blocks == 0u) {
        warnings++;
    }

    if (doctor_kmsg_available_local(&kmsg_bytes)) {
        if (table) {
            doctor_table_begin_local("kmsg", "ok");
            write_dec(kmsg_bytes);
            write_str(" bytes buffered");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("kmsg", "ok");
            write_dec(kmsg_bytes);
            write_str(" bytes buffered\n");
        }
    } else {
        if (table) {
            doctor_table_begin_local("kmsg", "warn");
            write_str("query unavailable");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("kmsg", "warn");
            write_str("query unavailable\n");
        }
        warnings++;
    }

    pci_devices = doctor_count_pci_local();
    if (table) {
        doctor_table_begin_local("pci", pci_devices > 0u ? "ok" : "warn");
        write_dec(pci_devices);
        write_str(" PCI device");
        write_str(pci_devices == 1u ? "" : "s");
        doctor_table_end_local();
    } else {
        doctor_prefix_local("pci", pci_devices > 0u ? "ok" : "warn");
        write_dec(pci_devices);
        write_str(" PCI device");
        write_str(pci_devices == 1u ? "" : "s");
        write_str("\n");
    }
    if (pci_devices == 0u) {
        warnings++;
    }

    if (doctor_count_drivers_local(&driver_counts)) {
        const char *driver_status =
            driver_counts.records == 0u ? "warn" :
            driver_counts.failed > 0u ? "fail" :
            driver_counts.active > 0u ? "ok" : "warn";

        if (table) {
            doctor_table_begin_local("drivers", driver_status);
            write_dec(driver_counts.active);
            write_str("/");
            write_dec(driver_counts.records);
            write_str(" active, inactive=");
            write_dec(driver_counts.inactive);
            write_str(", failed=");
            write_dec(driver_counts.failed);
            write_str(", files=");
            write_dec(driver_counts.loaded_files);
            write_str("/");
            write_dec(driver_counts.files);
            write_str(" loaded");
            if (driver_counts.invalid_files != 0u) {
                write_str(", invalid=");
                write_dec(driver_counts.invalid_files);
            }
            doctor_table_end_local();
        } else {
            doctor_prefix_local("drivers", driver_status);
            write_dec(driver_counts.active);
            write_str("/");
            write_dec(driver_counts.records);
            write_str(" active, inactive=");
            write_dec(driver_counts.inactive);
            write_str(", failed=");
            write_dec(driver_counts.failed);
            write_str(", files=");
            write_dec(driver_counts.loaded_files);
            write_str("/");
            write_dec(driver_counts.files);
            write_str(" loaded");
            if (driver_counts.invalid_files != 0u) {
                write_str(", invalid=");
                write_dec(driver_counts.invalid_files);
            }
            write_str("\n");
        }
        if (driver_counts.failed > 0u) {
            failures++;
        } else if (driver_counts.records == 0u || driver_counts.active == 0u) {
            warnings++;
        }
    } else {
        if (table) {
            doctor_table_begin_local("drivers", "warn");
            write_str("/proc/drivers unavailable");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("drivers", "warn");
            write_str("/proc/drivers unavailable\n");
        }
        warnings++;
    }

    procs = doctor_count_processes_local();
    if (table) {
        doctor_table_begin_local("processes", procs > 0u ? "ok" : "warn");
        write_dec(procs);
        write_str(" active process");
        write_str(procs == 1u ? "" : "es");
        doctor_table_end_local();
    } else {
        doctor_prefix_local("processes", procs > 0u ? "ok" : "warn");
        write_dec(procs);
        write_str(" active process");
        write_str(procs == 1u ? "" : "es");
        write_str("\n");
    }
    if (procs == 0u) {
        warnings++;
    }

    if (rtc_query(&rtc) > 0 && rtc.present) {
        if (table) {
            doctor_table_begin_local("rtc", rtc.valid ? "ok" : "warn");
        } else {
            doctor_prefix_local("rtc", rtc.valid ? "ok" : "warn");
        }
        write_dec(rtc.year);
        write_str("-");
        if (rtc.month < 10u) {
            write_str("0");
        }
        write_dec(rtc.month);
        write_str("-");
        if (rtc.day < 10u) {
            write_str("0");
        }
        write_dec(rtc.day);
        write_str(" ");
        if (rtc.hour < 10u) {
            write_str("0");
        }
        write_dec(rtc.hour);
        write_str(":");
        if (rtc.minute < 10u) {
            write_str("0");
        }
        write_dec(rtc.minute);
        if (table) {
            if (!rtc.valid) {
                write_str(" invalid");
            }
            doctor_table_end_local();
        } else {
            write_str(rtc.valid ? "\n" : " invalid\n");
        }
        if (!rtc.valid) {
            warnings++;
        }
    } else {
        if (table) {
            doctor_table_begin_local("rtc", "warn");
            write_str("not available");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("rtc", "warn");
            write_str("not available\n");
        }
        warnings++;
    }

    if (rtl8139_query(&nic) > 0 && nic.present) {
        if (table) {
            doctor_table_begin_local("network", nic.initialized && nic.link_up ? "ok" : "warn");
        } else {
            doctor_prefix_local("network", nic.initialized && nic.link_up ? "ok" : "warn");
        }
        write_str("rtl8139 ");
        write_str(nic.initialized ? "initialized" : "not initialized");
        write_str(", link ");
        write_str(nic.link_up ? "up" : "down");
        if (nic.link_up) {
            write_str(", ");
            write_dec(nic.speed_mbps);
            write_str("Mbps");
        }
        if (table) {
            doctor_table_end_local();
        } else {
            write_str("\n");
        }
        if (!nic.initialized || !nic.link_up) {
            warnings++;
        }
    } else {
        if (table) {
            doctor_table_begin_local("network", "warn");
            write_str("rtl8139 not present");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("network", "warn");
            write_str("rtl8139 not present\n");
        }
        warnings++;
    }

    if (audio_query(0u, &audio) > 0 && audio.present) {
        if (table) {
            doctor_table_begin_local("audio", audio.initialized ? "ok" : "warn");
            doctor_table_escape_local(audio.name[0] != '\0' ? audio.name : "audio");
        } else {
            doctor_prefix_local("audio", audio.initialized ? "ok" : "warn");
            write_str(audio.name[0] != '\0' ? audio.name : "audio");
        }
        write_str(" ");
        write_dec(audio.sample_rate);
        write_str("Hz ");
        write_dec(audio.channels);
        write_str("ch");
        if (table) {
            if (!audio.initialized) {
                write_str(" not initialized");
            }
            doctor_table_end_local();
        } else {
            write_str(audio.initialized ? "\n" : " not initialized\n");
        }
        if (!audio.initialized) {
            warnings++;
        }
    } else {
        if (table) {
            doctor_table_begin_local("audio", "warn");
            write_str("no audio device");
            doctor_table_end_local();
        } else {
            doctor_prefix_local("audio", "warn");
            write_str("no audio device\n");
        }
        warnings++;
    }

    if (table) {
        doctor_table_begin_local("summary", failures > 0u ? "fail" : (warnings > 0u ? "warn" : "ok"));
    } else {
        write_str("\nsummary     ");
    }
    if (failures > 0u) {
        if (!table) {
            write_str("fail  ");
        }
    } else if (warnings > 0u) {
        if (!table) {
            write_str("warn  ");
        }
    } else {
        if (!table) {
            write_str("ok    ");
        }
    }
    write_dec(failures);
    write_str(" failure");
    write_str(failures == 1u ? "" : "s");
    write_str(", ");
    write_dec(warnings);
    write_str(" warning");
    write_str(warnings == 1u ? "" : "s");
    if (table) {
        doctor_table_end_local();
    } else {
        write_str("\n");
    }
    return failures == 0u ? 0 : 1;
}
