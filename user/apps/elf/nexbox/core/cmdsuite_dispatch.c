#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"
#include "user/apps/elf/nexbox/core/cmdsuite_action.h"

static const char *command_basename(const char *path) {
    const char *last = path;
    uint32_t i = 0;

    if (path == NULL) {
        return "";
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            last = path + i + 1u;
        }
        i++;
    }
    return last;
}

static int cmdsuite_is_multicall_name(const char *name) {
    return streq_ignore_case_local(name, "cmdsuite") ||
           streq_ignore_case_local(name, "cmdsuite.elf") ||
           streq_ignore_case_local(name, "nexbox") ||
           streq_ignore_case_local(name, "nexbox.elf");
}

typedef int (*cmdsuite_handler_fn)(int argc, char **argv);

struct cmdsuite_entry {
    const char *name;
    cmdsuite_handler_fn handler;
};

static int cmd_wrap_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_help();
}

static int cmd_wrap_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_clear();
}

static int cmd_wrap_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_pwd();
}

static int cmd_wrap_blk(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_blk();
}

static int cmd_wrap_mounts(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_mounts();
}

static int cmd_wrap_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_ps();
}

static int cmd_wrap_jobs(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_jobs();
}

static int cmd_wrap_progs(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_progs();
}

static int cmd_wrap_fatls(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_fatls();
}

static int cmd_wrap_dmesg(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_dmesg();
}

static int cmd_wrap_lspci(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_lspci();
}

static int cmd_wrap_ac97(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_ac97();
}

static int cmd_wrap_rtl8139(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_rtl8139();
}

static int cmd_wrap_rtl8139tx(int argc, char **argv) {
    return cmd_rtl8139tx(argc, argv);
}

static int cmd_wrap_rtl8139rx(int argc, char **argv) {
    return cmd_rtl8139rx(argc, argv);
}

static int cmd_wrap_ping(int argc, char **argv) {
    return cmd_ping(argc, argv);
}

static int cmd_wrap_dns(int argc, char **argv) {
    return cmd_dns(argc, argv);
}

static int cmd_wrap_dhcp(int argc, char **argv) {
    return cmd_dhcp(argc, argv);
}

static int cmd_wrap_http(int argc, char **argv) {
    return cmd_http(argc, argv);
}

static int cmd_wrap_wget(int argc, char **argv) {
    return cmd_wget(argc, argv);
}

static int cmd_wrap_nc(int argc, char **argv) {
    return cmd_nc(argc, argv);
}

static int cmd_wrap_audio(int argc, char **argv) {
    return cmd_audio(argc, argv);
}

static int cmd_wrap_tone(int argc, char **argv) {
    return cmd_tone(argc, argv);
}

static int cmd_wrap_wav(int argc, char **argv) {
    return cmd_wav(argc, argv);
}

static int cmd_wrap_mplay(int argc, char **argv) {
    return cmd_mplay(argc, argv);
}

static int cmd_wrap_meminfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_meminfo();
}

static int cmd_wrap_minfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_minfo();
}

static int cmd_wrap_cpuinfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_cpuinfo();
}

static int cmd_wrap_which(int argc, char **argv) {
    return cmd_which_like(argc, argv, "which");
}

static int cmd_wrap_type(int argc, char **argv) {
    return cmd_which_like(argc, argv, "type");
}

static int cmd_wrap_run(int argc, char **argv) {
    return cmd_run_like(argc, argv, "run", 0, 0, 1);
}

static int cmd_wrap_runelf(int argc, char **argv) {
    return cmd_run_like(argc, argv, "runelf", SYS_SPAWN_ELF, 0, 0);
}

static int cmd_wrap_runbg(int argc, char **argv) {
    return cmd_run_like(argc, argv, "runbg", SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND, 0);
}

static int cmd_wrap_kill(int argc, char **argv) {
    return cmd_kill_like(argc, argv, "kill");
}

static int cmd_wrap_fg(int argc, char **argv) {
    return cmd_kill_like(argc, argv, "fg");
}

static int cmd_wrap_bg(int argc, char **argv) {
    return cmd_kill_like(argc, argv, "bg");
}

static int cmd_wrap_reboot(int argc, char **argv) {
    if (argc > 1 &&
        (streq_local(argv[1], "-h") || streq_local(argv[1], "--help"))) {
        write_err_usage("reboot", "\n");
        return 0;
    }
    if (argc != 1) {
        write_err_usage("reboot", "\n");
        return 1;
    }
    write_str("rebooting...\n");
    return reboot();
}

static const struct cmdsuite_entry g_cmdsuite_entries[] = {
    {"help", cmd_wrap_help},
    {"actions", cmd_wrap_actions},
    {"action", cmd_wrap_action},
    {"mapper", cmd_wrap_mapper},
    {"echo", cmd_echo},
    {"clear", cmd_wrap_clear},
    {"pwd", cmd_wrap_pwd},
    {"env", cmd_env},
    {"which", cmd_wrap_which},
    {"type", cmd_wrap_type},
    {"ls", cmd_ls},
    {"cat", cmd_cat},
    {"less", cmd_less},
    {"hexdump", cmd_hexdump},
    {"grep", cmd_grep},
    {"date", cmd_date},
    {"hwclock", cmd_hwclock},
    {"sleep", cmd_sleep},
    {"watch", cmd_watch},
    {"on", cmd_on},
    {"events", cmd_events},
    {"wc", cmd_wc},
    {"head", cmd_head},
    {"tail", cmd_tail},
    {"find", cmd_find},
    {"as", cmd_as},
    {"pick", cmd_pick},
    {"select", cmd_select},
    {"sort-by", cmd_sort_by},
    {"count-by", cmd_count_by},
    {"to", cmd_to},
    {"view", cmd_view},
    {"ed", cmd_ed},
    {"vi", cmd_vi},
    {"vim", cmd_vi},
    {"touch", cmd_touch},
    {"mv", cmd_mv},
    {"cp", cmd_cp},
    {"mkdir", cmd_mkdir},
    {"rmdir", cmd_rmdir},
    {"rm", cmd_rm},
    {"asm", cmd_asm},
    {"stat", cmd_stat},
    {"du", cmd_du},
    {"tree", cmd_tree},
    {"file", cmd_file},
    {"run", cmd_wrap_run},
    {"runelf", cmd_wrap_runelf},
    {"runbg", cmd_wrap_runbg},
    {"blk", cmd_wrap_blk},
    {"parts", cmd_parts},
    {"fdisk", cmd_fdisk},
    {"df", cmd_df},
    {"mounts", cmd_wrap_mounts},
    {"mount", cmd_mount},
    {"umount", cmd_umount},
    {"hotplug", cmd_hotplug},
    {"switch_root", cmd_switch_root},
    {"ps", cmd_wrap_ps},
    {"session", cmd_session},
    {"service", cmd_service},
    {"jobs", cmd_wrap_jobs},
    {"wait", cmd_wait},
    {"alarm", cmd_alarm},
    {"timeout", cmd_timeout},
    {"kill", cmd_wrap_kill},
    {"fg", cmd_wrap_fg},
    {"bg", cmd_wrap_bg},
    {"reboot", cmd_wrap_reboot},
    {"progs", cmd_wrap_progs},
    {"fatls", cmd_wrap_fatls},
    {"fatfind", cmd_fatfind},
    {"fatread", cmd_fatread},
    {"cpio", cmd_cpio},
    {"dmesg", cmd_wrap_dmesg},
    {"lspci", cmd_wrap_lspci},
    {"ac97", cmd_wrap_ac97},
    {"rtl8139", cmd_wrap_rtl8139},
    {"rtl8139tx", cmd_wrap_rtl8139tx},
    {"rtl8139rx", cmd_wrap_rtl8139rx},
    {"arp", cmd_arp},
    {"route", cmd_route},
    {"netstat", cmd_netstat},
    {"ping", cmd_wrap_ping},
    {"dns", cmd_wrap_dns},
    {"dhcp", cmd_wrap_dhcp},
    {"ifconfig", cmd_ifconfig},
    {"http", cmd_wrap_http},
    {"wget", cmd_wrap_wget},
    {"nc", cmd_wrap_nc},
    {"audio", cmd_wrap_audio},
    {"tone", cmd_wrap_tone},
    {"wav", cmd_wrap_wav},
    {"mplay", cmd_wrap_mplay},
    {"doctor", cmd_doctor},
    {"nexctl", cmd_nexctl},
    {"sysinfo", cmd_sysinfo},
    {"meminfo", cmd_wrap_meminfo},
    {"minfo", cmd_wrap_minfo},
    {"uname", cmd_uname},
    {"cpuinfo", cmd_wrap_cpuinfo},
    {"dbg", cmd_dbg},
};

static const struct cmdsuite_entry *cmdsuite_find_entry(const char *name) {
    uint32_t i;

    for (i = 0; i < sizeof(g_cmdsuite_entries) / sizeof(g_cmdsuite_entries[0]); i++) {
        if (streq_ignore_case_local(name, g_cmdsuite_entries[i].name)) {
            return &g_cmdsuite_entries[i];
        }
    }
    return NULL;
}

int cmdsuite_run_backing_command(const char *command, int argc, char **argv) {
    const struct cmdsuite_entry *entry = cmdsuite_find_entry(command);

    if (entry == NULL) {
        write_err_str("action: backing command not found: ");
        write_err_str(command != NULL ? command : "");
        write_err_str("\n");
        return 1;
    }
    return entry->handler(argc, argv);
}

int cmdsuite_dispatch_main(int argc, char **argv) {
    const char *name = command_basename(argc > 0 ? argv[0] : "");

    if (cmdsuite_is_multicall_name(name)) {
        if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
            write_str("NexBox multicall userland\n");
            write_str("usage: nexbox <applet> [args]\n");
            write_str("hint: run `help` for the applet list\n");
            return 0;
        }
        return cmdsuite_dispatch_main(argc - 1, argv + 1);
    }

    const struct cmdsuite_entry *entry = cmdsuite_find_entry(name);

    if (entry != NULL) {
        const struct nex_action_entry *action = nex_action_find_for_invocation(name, argc, argv);

        if (action != NULL && !nex_action_check_allowed(action, name)) {
            return 1;
        }
        return entry->handler(argc, argv);
    }

    write_err_str("nexbox: unsupported command name: ");
    write_err_str(name);
    write_err_str("\n");
    return 1;
}

int main(int argc, char **argv) {
    return cmdsuite_dispatch_main(argc, argv);
}
