#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

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

struct nex_action_entry {
    const char *name;
    const char *group;
    const char *command;
    const char *input_schema;
    const char *output_schema;
    uint32_t cap_flags;
    const char *caps;
    const char *summary;
};

struct nex_mapper_entry {
    const char *command;
    const char *action;
    const char *params;
    const char *format;
};

enum nex_action_capability {
    NEX_ACTION_CAP_FS_READ = 1u << 0,
    NEX_ACTION_CAP_FS_WRITE = 1u << 1,
    NEX_ACTION_CAP_FS_INSPECT = 1u << 2,
    NEX_ACTION_CAP_BLOCK_INSPECT = 1u << 3,
    NEX_ACTION_CAP_PROC_READ = 1u << 4,
    NEX_ACTION_CAP_PROC_SIGNAL = 1u << 5,
    NEX_ACTION_CAP_NET_INSPECT = 1u << 6,
    NEX_ACTION_CAP_NET_CLIENT = 1u << 7,
    NEX_ACTION_CAP_NET_CONFIGURE = 1u << 8,
    NEX_ACTION_CAP_NET_RAW = 1u << 9,
    NEX_ACTION_CAP_AUDIO_INSPECT = 1u << 10,
    NEX_ACTION_CAP_AUDIO_PLAY = 1u << 11,
    NEX_ACTION_CAP_DEBUG_READ = 1u << 12,
    NEX_ACTION_CAP_HW_INSPECT = 1u << 13,
    NEX_ACTION_CAP_SYSTEM_INSPECT = 1u << 14
};

enum {
    NEX_ACTION_CAP_ALL = (1u << 15) - 1u,
    NEX_ACTION_ARG_MAX = 15u,
    NEX_ACTION_ARG_NAME_MAX = 32u,
    NEX_ACTION_ARG_TYPE_MAX = 16u,
    NEX_ACTION_ARG_VALUE_MAX = 64u,
    NEX_ACTION_POLICY_RULE_MAX = 12u,
    NEX_ACTION_POLICY_FILE_MAX = 768u
};

struct nex_action_cap_name {
    const char *name;
    uint32_t flag;
};

struct nex_action_policy {
    uint32_t allow_mask;
    uint32_t allow_action_count;
    uint32_t deny_action_count;
    char allow_actions[NEX_ACTION_POLICY_RULE_MAX][NEX_ACTION_ARG_NAME_MAX];
    char deny_actions[NEX_ACTION_POLICY_RULE_MAX][NEX_ACTION_ARG_NAME_MAX];
};

static const char *g_nex_action_caps_path = "/HOME/ACTION.CAPS";

static const struct nex_action_cap_name g_nex_action_cap_names[] = {
    {"fs.read", NEX_ACTION_CAP_FS_READ},
    {"fs.write", NEX_ACTION_CAP_FS_WRITE},
    {"fs.inspect", NEX_ACTION_CAP_FS_INSPECT},
    {"block.inspect", NEX_ACTION_CAP_BLOCK_INSPECT},
    {"proc.read", NEX_ACTION_CAP_PROC_READ},
    {"proc.signal", NEX_ACTION_CAP_PROC_SIGNAL},
    {"net.inspect", NEX_ACTION_CAP_NET_INSPECT},
    {"net.client", NEX_ACTION_CAP_NET_CLIENT},
    {"net.configure", NEX_ACTION_CAP_NET_CONFIGURE},
    {"net.raw", NEX_ACTION_CAP_NET_RAW},
    {"audio.inspect", NEX_ACTION_CAP_AUDIO_INSPECT},
    {"audio.play", NEX_ACTION_CAP_AUDIO_PLAY},
    {"debug.read", NEX_ACTION_CAP_DEBUG_READ},
    {"hw.inspect", NEX_ACTION_CAP_HW_INSPECT},
    {"system.inspect", NEX_ACTION_CAP_SYSTEM_INSPECT},
};

static int cmd_wrap_actions(int argc, char **argv);
static int cmd_wrap_action(int argc, char **argv);
static int cmd_wrap_mapper(int argc, char **argv);

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
    {"touch", cmd_touch},
    {"mv", cmd_mv},
    {"cp", cmd_cp},
    {"mkdir", cmd_mkdir},
    {"rmdir", cmd_rmdir},
    {"rm", cmd_rm},
    {"fasm", cmd_fasm},
    {"run", cmd_wrap_run},
    {"runelf", cmd_wrap_runelf},
    {"runbg", cmd_wrap_runbg},
    {"blk", cmd_wrap_blk},
    {"parts", cmd_parts},
    {"fdisk", cmd_fdisk},
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
    {"meminfo", cmd_wrap_meminfo},
    {"minfo", cmd_wrap_minfo},
    {"uname", cmd_uname},
    {"cpuinfo", cmd_wrap_cpuinfo},
    {"dbg", cmd_dbg},
};

static const struct nex_action_entry g_nex_actions[] = {
    {"audio.list", "audio", "audio", "none", "table/audio-devices", NEX_ACTION_CAP_AUDIO_INSPECT, "audio.inspect", "list audio devices"},
    {"audio.tone", "audio", "tone", "hz:int ms:int device?:int", "none", NEX_ACTION_CAP_AUDIO_PLAY, "audio.play", "play a simple tone"},
    {"audio.play_wav", "audio", "mplay", "path:path", "none", NEX_ACTION_CAP_AUDIO_PLAY | NEX_ACTION_CAP_FS_READ, "audio.play fs.read", "play a WAV file"},
    {"debug.kmsg", "debug", "dmesg", "none", "text/kmsg", NEX_ACTION_CAP_DEBUG_READ, "debug.read", "read kernel log"},
    {"debug.pci", "debug", "lspci", "none", "table/pci", NEX_ACTION_CAP_HW_INSPECT, "hw.inspect", "inspect PCI devices"},
    {"file.read", "file", "cat", "path:path", "text", NEX_ACTION_CAP_FS_READ, "fs.read", "read a file"},
    {"fs.list", "storage", "ls", "path?:path", "dirent-list", NEX_ACTION_CAP_FS_READ, "fs.read", "list directory contents"},
    {"fs.copy", "storage", "cp", "src:path dst:path", "none", NEX_ACTION_CAP_FS_READ | NEX_ACTION_CAP_FS_WRITE, "fs.read fs.write", "copy a file"},
    {"fs.mounts", "storage", "mounts", "none", "table/mounts", NEX_ACTION_CAP_FS_INSPECT, "fs.inspect", "list mounted filesystems"},
    {"fs.block_devices", "storage", "blk", "none", "table/block-devices", NEX_ACTION_CAP_BLOCK_INSPECT, "block.inspect", "list block devices"},
    {"device.hotplug", "storage", "hotplug", "op?:word disk?:int part?:int", "table/hotplug", NEX_ACTION_CAP_BLOCK_INSPECT | NEX_ACTION_CAP_FS_INSPECT, "block.inspect fs.inspect", "scan or automount discovered partitions"},
    {"net.config", "net", "ifconfig", "none", "record/net-config", NEX_ACTION_CAP_NET_INSPECT, "net.inspect", "show network config"},
    {"net.dhcp", "net", "dhcp", "none", "record/net-config", NEX_ACTION_CAP_NET_CONFIGURE, "net.configure", "request DHCP configuration"},
    {"net.dns", "net", "dns", "host:host type?:word", "dns-answer", NEX_ACTION_CAP_NET_CLIENT, "net.client", "resolve a DNS name"},
    {"net.http_get", "net", "wget", "url:host output?:path", "file|stdout", NEX_ACTION_CAP_NET_CLIENT | NEX_ACTION_CAP_FS_WRITE, "net.client fs.write", "fetch HTTP content"},
    {"net.ping", "net", "ping", "host?:host", "icmp-result", NEX_ACTION_CAP_NET_RAW, "net.raw", "send an ICMP echo request"},
    {"event.file_change", "event", "on", "event:word path:path action:word", "none", NEX_ACTION_CAP_FS_READ, "fs.read", "run a command when a file changes"},
    {"event.timer", "event", "on", "event:word interval?:word action:word", "event/timer", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "run a command on timer ticks"},
    {"event.input.keyboard", "event", "on", "event:word key:word interval?:word action:word", "event/input/keyboard", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "run a command on keyboard events"},
    {"event.input.mouse", "event", "on", "event:word button?:word interval?:word action:word", "event/input/mouse", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "run a command on mouse events"},
    {"event.net.status", "event", "on", "event:word state?:word interval?:word action:word", "event/net/status", NEX_ACTION_CAP_NET_INSPECT, "net.inspect", "run a command on network status changes"},
    {"event.block.change", "event", "on", "event:word op?:word interval?:word action:word", "event/block/change", NEX_ACTION_CAP_BLOCK_INSPECT, "block.inspect", "run a command on block device changes"},
    {"event.jobs", "event", "events", "op?:word id?:word", "table/event-jobs", NEX_ACTION_CAP_PROC_READ | NEX_ACTION_CAP_SYSTEM_INSPECT, "proc.read system.inspect", "manage background event jobs"},
    {"event.as_table", "event", "as", "type:word", "table/events", 0, "none", "convert EventFS text events to a typed table"},
    {"proc.jobs", "process", "jobs", "none", "table/jobs", NEX_ACTION_CAP_PROC_READ, "proc.read", "list shell jobs"},
    {"proc.list", "process", "ps", "none", "table/processes", NEX_ACTION_CAP_PROC_READ, "proc.read", "list processes"},
    {"proc.kill", "process", "kill", "pid:int", "none", NEX_ACTION_CAP_PROC_SIGNAL, "proc.signal", "kill a process"},
    {"system.cpu", "system", "cpuinfo", "none", "record/cpu", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "show CPU information"},
    {"system.clock", "system", "hwclock", "flags?:word", "record/rtc", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "read CMOS RTC clock"},
    {"system.mem", "system", "meminfo", "none", "record/memory", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "show memory information"},
    {"session.image", "system", "session", "op:word name?:word", "session-image", NEX_ACTION_CAP_FS_READ | NEX_ACTION_CAP_FS_WRITE | NEX_ACTION_CAP_SYSTEM_INSPECT, "fs.read fs.write system.inspect", "save or inspect a session image"},
    {"system.service", "system", "service", "op:word name?:word command?:text", "table/services", NEX_ACTION_CAP_FS_READ | NEX_ACTION_CAP_FS_WRITE | NEX_ACTION_CAP_PROC_READ | NEX_ACTION_CAP_PROC_SIGNAL | NEX_ACTION_CAP_SYSTEM_INSPECT, "fs.read fs.write proc.read proc.signal system.inspect", "define and manage boot services"},
    {"system.uname", "system", "uname", "flags?:word", "record/system", NEX_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "show system identity"},
    {"text.grep", "text", "grep", "pattern:text file?:path", "text", NEX_ACTION_CAP_FS_READ, "fs.read", "filter text"},
    {"text.view", "text", "cat", "path?:path", "text", NEX_ACTION_CAP_FS_READ, "fs.read", "print file contents"},
    {"table.as", "table", "as", "type:word", "typed-stream", 0, "none", "mark stream with a NexOS type"},
    {"table.pick", "table", "pick", "column:word", "typed-stream", 0, "none", "filter a typed table by column"},
    {"table.select", "table", "select", "columns...:word", "typed-stream", 0, "none", "project table columns"},
    {"table.sort", "table", "sort-by", "column:word", "typed-stream", 0, "none", "sort table rows by a column"},
    {"table.count", "table", "count-by", "column:word", "typed-stream", 0, "none", "count rows by column value"},
    {"table.json", "table", "to", "format:word", "json", 0, "none", "convert a typed table to JSON"},
    {"table.view", "table", "view", "format:word", "text/table", 0, "none", "render a typed table for humans"},
};

static const struct nex_mapper_entry g_nex_mapper_entries[] = {
    {"audio", "audio.list", "none", "table/audio-devices"},
    {"blk", "fs.block_devices", "none", "table/block-devices"},
    {"cat", "file.read", "path:path", "text"},
    {"count-by", "table.count", "column:word", "typed-stream"},
    {"cp", "fs.copy", "src:path dst:path", "none"},
    {"cpuinfo", "system.cpu", "none", "record/cpu"},
    {"date", "system.clock", "flags?:word", "record/rtc"},
    {"dhcp", "net.dhcp", "none", "record/net-config"},
    {"dmesg", "debug.kmsg", "none", "text/kmsg"},
    {"dns", "net.dns", "host:host type?:word", "dns-answer"},
    {"grep", "text.grep", "pattern:text file?:path", "text"},
    {"ifconfig", "net.config", "none", "record/net-config"},
    {"hotplug", "device.hotplug", "op?:word disk?:int part?:int", "table/hotplug"},
    {"hwclock", "system.clock", "flags?:word", "record/rtc"},
    {"jobs", "proc.jobs", "none", "table/jobs"},
    {"kill", "proc.kill", "pid:int", "none"},
    {"ls", "fs.list", "path?:path", "dirent-list"},
    {"lspci", "debug.pci", "none", "table/pci"},
    {"meminfo", "system.mem", "none", "record/memory"},
    {"mplay", "audio.play_wav", "path:path", "none"},
    {"mounts", "fs.mounts", "none", "table/mounts"},
    {"on", "event.file_change|event.timer|event.input.keyboard|event.input.mouse|event.net.status|event.block.change", "event:word args...:word", "none"},
    {"events", "event.jobs", "op?:word id?:word", "table/event-jobs"},
    {"as", "table.as|event.as_table", "type:word", "typed-stream"},
    {"pick", "table.pick", "column:word", "typed-stream"},
    {"ping", "net.ping", "host?:host", "icmp-result"},
    {"ps", "proc.list", "none", "table/processes"},
    {"select", "table.select", "columns...:word", "typed-stream"},
    {"session", "session.image", "op:word name?:word", "session-image"},
    {"service", "system.service", "op:word name?:word command?:text", "table/services"},
    {"sort-by", "table.sort", "column:word", "typed-stream"},
    {"to", "table.json", "format:word", "json"},
    {"tone", "audio.tone", "hz:int ms:int device?:int", "none"},
    {"uname", "system.uname", "flags?:word", "record/system"},
    {"view", "table.view", "format:word", "text/table"},
    {"wget", "net.http_get", "url:host output?:path", "file|stdout"},
};

static uint32_t nex_action_count(void) {
    return (uint32_t)(sizeof(g_nex_actions) / sizeof(g_nex_actions[0]));
}

static const struct nex_action_entry *nex_action_find(const char *name) {
    uint32_t i;

    for (i = 0; i < nex_action_count(); i++) {
        if (streq_ignore_case_local(name, g_nex_actions[i].name)) {
            return &g_nex_actions[i];
        }
    }
    return NULL;
}

static const struct nex_action_entry *nex_action_find_by_backing_command(const char *command) {
    uint32_t i;

    for (i = 0; i < nex_action_count(); i++) {
        if (streq_ignore_case_local(command, g_nex_actions[i].command)) {
            return &g_nex_actions[i];
        }
    }
    return NULL;
}

static uint32_t nex_mapper_count(void) {
    return (uint32_t)(sizeof(g_nex_mapper_entries) / sizeof(g_nex_mapper_entries[0]));
}

static const struct nex_mapper_entry *nex_mapper_find(const char *command) {
    uint32_t i;

    for (i = 0; i < nex_mapper_count(); i++) {
        if (streq_ignore_case_local(command, g_nex_mapper_entries[i].command)) {
            return &g_nex_mapper_entries[i];
        }
    }
    return NULL;
}

static const struct nex_action_entry *nex_action_find_for_friendly_command(const char *command) {
    const struct nex_mapper_entry *mapper = nex_mapper_find(command);

    if (mapper != NULL) {
        return nex_action_find(mapper->action);
    }
    return nex_action_find_by_backing_command(command);
}

static const struct cmdsuite_entry *cmdsuite_find_entry(const char *name) {
    uint32_t i;

    for (i = 0; i < sizeof(g_cmdsuite_entries) / sizeof(g_cmdsuite_entries[0]); i++) {
        if (streq_ignore_case_local(name, g_cmdsuite_entries[i].name)) {
            return &g_cmdsuite_entries[i];
        }
    }
    return NULL;
}

static int nex_action_print_list(void) {
    uint32_t i;

    write_str("NexOS actions\n");
    for (i = 0; i < nex_action_count(); i++) {
        write_text_padded(g_nex_actions[i].name, 18u);
        write_text_padded(g_nex_actions[i].group, 10u);
        write_str(g_nex_actions[i].summary);
        write_str("\n");
    }
    return 0;
}

static void nex_action_print_table_token(const char *text) {
    uint32_t i = 0;
    int quote = 0;

    if (text == NULL || text[0] == '\0') {
        write_str("-");
        return;
    }
    while (text[i] != '\0') {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '"' || text[i] == '\\') {
            quote = 1;
            break;
        }
        i++;
    }
    i = 0;
    if (quote) {
        write_str("\"");
    }
    while (text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            write_str("\\");
        }
        write_stdout(&text[i], 1u);

        i++;
    }
    if (quote) {
        write_str("\"");
    }
}

static int nex_action_print_table(void) {
    uint32_t i;

    write_str("# nex/type: table\n");
    write_str("# nex/columns: name group command input output cap_flags caps summary\n");
    write_str("name group command input output cap_flags caps summary\n");
    for (i = 0; i < nex_action_count(); i++) {
        write_str(g_nex_actions[i].name);
        write_str(" ");
        write_str(g_nex_actions[i].group);
        write_str(" ");
        write_str(g_nex_actions[i].command);
        write_str(" ");
        nex_action_print_table_token(g_nex_actions[i].input_schema);
        write_str(" ");
        nex_action_print_table_token(g_nex_actions[i].output_schema);
        write_str(" 0x");
        write_hex_u32(g_nex_actions[i].cap_flags);
        write_str(" ");
        nex_action_print_table_token(g_nex_actions[i].caps);
        write_str(" ");
        nex_action_print_table_token(g_nex_actions[i].summary);
        write_str("\n");
    }
    return 0;
}

static int nex_action_print_info(const struct nex_action_entry *action) {
    if (action == NULL) {
        return 1;
    }
    write_str("name: ");
    write_str(action->name);
    write_str("\ngroup: ");
    write_str(action->group);
    write_str("\ncommand: ");
    write_str(action->command);
    write_str("\ninput: ");
    write_str(action->input_schema);
    write_str("\noutput: ");
    write_str(action->output_schema);
    write_str("\ncap_flags: 0x");
    write_hex_u32(action->cap_flags);
    write_str("\ncaps: ");
    write_str(action->caps);
    write_str("\nsummary: ");
    write_str(action->summary);
    write_str("\n");
    return 0;
}

static int nex_action_print_caps(void) {
    write_str("NexOS action capability flags\n");
    write_str("0x00000001 fs.read\n");
    write_str("0x00000002 fs.write\n");
    write_str("0x00000004 fs.inspect\n");
    write_str("0x00000008 block.inspect\n");
    write_str("0x00000010 proc.read\n");
    write_str("0x00000020 proc.signal\n");
    write_str("0x00000040 net.inspect\n");
    write_str("0x00000080 net.client\n");
    write_str("0x00000100 net.configure\n");
    write_str("0x00000200 net.raw\n");
    write_str("0x00000400 audio.inspect\n");
    write_str("0x00000800 audio.play\n");
    write_str("0x00001000 debug.read\n");
    write_str("0x00002000 hw.inspect\n");
    write_str("0x00004000 system.inspect\n");
    return 0;
}

static int nex_mapper_print_table(void) {
    uint32_t i;

    write_str("# nex/type: table\n");
    write_str("# nex/columns: command action params format caps summary\n");
    write_str("command action params format caps summary\n");
    for (i = 0; i < nex_mapper_count(); i++) {
        const struct nex_action_entry *action = nex_action_find(g_nex_mapper_entries[i].action);

        write_str(g_nex_mapper_entries[i].command);
        write_str(" ");
        write_str(g_nex_mapper_entries[i].action);
        write_str(" ");
        nex_action_print_table_token(g_nex_mapper_entries[i].params);
        write_str(" ");
        nex_action_print_table_token(g_nex_mapper_entries[i].format);
        write_str(" ");
        nex_action_print_table_token(action != NULL ? action->caps : "unknown");
        write_str(" ");
        nex_action_print_table_token(action != NULL ? action->summary : "missing action");
        write_str("\n");
    }
    return 0;
}

static int nex_mapper_print_list(void) {
    uint32_t i;

    write_str("NexOS mapper layer\n");
    for (i = 0; i < nex_mapper_count(); i++) {
        write_text_padded(g_nex_mapper_entries[i].command, 12u);
        write_str(" -> ");
        write_text_padded(g_nex_mapper_entries[i].action, 18u);
        write_str(g_nex_mapper_entries[i].params);
        write_str("\n");
    }
    return 0;
}

static int nex_mapper_print_info(const struct nex_mapper_entry *mapper) {
    const struct nex_action_entry *action;

    if (mapper == NULL) {
        return 1;
    }
    action = nex_action_find(mapper->action);
    write_str("command: ");
    write_str(mapper->command);
    write_str("\naction: ");
    write_str(mapper->action);
    write_str("\nparams: ");
    write_str(mapper->params);
    write_str("\nformat: ");
    write_str(mapper->format);
    if (action != NULL) {
        write_str("\ncap_flags: 0x");
        write_hex_u32(action->cap_flags);
        write_str("\ncaps: ");
        write_str(action->caps);
        write_str("\nsummary: ");
        write_str(action->summary);
    }
    write_str("\n");
    return 0;
}

static int nex_action_parse_cap_mask(const char *text, uint32_t *mask_out) {
    char *end = NULL;
    unsigned long value;

    if (text == NULL || mask_out == NULL || text[0] == '\0') {
        return 0;
    }
    if (streq_ignore_case_local(text, "all")) {
        *mask_out = NEX_ACTION_CAP_ALL;
        return 1;
    }
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_nex_action_cap_names) / sizeof(g_nex_action_cap_names[0])); i++) {
        if (streq_ignore_case_local(text, g_nex_action_cap_names[i].name)) {
            *mask_out = g_nex_action_cap_names[i].flag;
            return 1;
        }
    }
    value = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return 0;
    }
    *mask_out = (uint32_t)value;
    return 1;
}

static void nex_action_policy_init(struct nex_action_policy *policy) {
    if (policy == NULL) {
        return;
    }
    policy->allow_mask = NEX_ACTION_CAP_ALL;
    policy->allow_action_count = 0;
    policy->deny_action_count = 0;
}

static int nex_action_policy_add_name(char names[NEX_ACTION_POLICY_RULE_MAX][NEX_ACTION_ARG_NAME_MAX],
                                      uint32_t *count_io,
                                      const char *name) {
    if (names == NULL || count_io == NULL || name == NULL || name[0] == '\0' ||
        *count_io >= NEX_ACTION_POLICY_RULE_MAX) {
        return 0;
    }
    copy_line_local(names[*count_io], name, NEX_ACTION_ARG_NAME_MAX);
    (*count_io)++;
    return 1;
}

static int nex_action_policy_name_matches(char names[NEX_ACTION_POLICY_RULE_MAX][NEX_ACTION_ARG_NAME_MAX],
                                          uint32_t count,
                                          const char *name) {
    if (name == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (streq_ignore_case_local(names[i], name)) {
            return 1;
        }
    }
    return 0;
}

static void nex_action_policy_read_word(const char **cursor_io, char *out, uint32_t out_size) {
    const char *cursor;
    uint32_t pos = 0;

    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (cursor_io == NULL || *cursor_io == NULL) {
        return;
    }
    cursor = *cursor_io;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' &&
           *cursor != ' ' && *cursor != '\t') {
        if (pos + 1u < out_size) {
            out[pos++] = *cursor;
        }
        cursor++;
    }
    out[pos] = '\0';
    *cursor_io = cursor;
}

static void nex_action_policy_apply_rule(struct nex_action_policy *policy, const char *line) {
    char verb[16];
    char kind[16];
    char value[NEX_ACTION_ARG_NAME_MAX];
    uint32_t cap = 0;
    const char *cursor = line;

    if (policy == NULL || line == NULL) {
        return;
    }
    nex_action_policy_read_word(&cursor, verb, sizeof(verb));
    if (verb[0] == '\0' || verb[0] == '#') {
        return;
    }
    if ((verb[0] >= '0' && verb[0] <= '9')) {
        char *end = NULL;
        unsigned long legacy = strtoul(verb, &end, 0);

        if (end != verb) {
            policy->allow_mask = (uint32_t)legacy & NEX_ACTION_CAP_ALL;
        }
        return;
    }
    nex_action_policy_read_word(&cursor, kind, sizeof(kind));
    nex_action_policy_read_word(&cursor, value, sizeof(value));
    if (streq_ignore_case_local(verb, "mask")) {
        if (nex_action_parse_cap_mask(kind, &cap)) {
            policy->allow_mask = cap & NEX_ACTION_CAP_ALL;
        }
        return;
    }
    if (streq_ignore_case_local(verb, "allow") && streq_ignore_case_local(kind, "cap")) {
        if (nex_action_parse_cap_mask(value, &cap)) {
            policy->allow_mask |= cap;
        }
        return;
    }
    if (streq_ignore_case_local(verb, "deny") && streq_ignore_case_local(kind, "cap")) {
        if (nex_action_parse_cap_mask(value, &cap)) {
            policy->allow_mask &= ~cap;
        }
        return;
    }
    if (streq_ignore_case_local(verb, "allow") && streq_ignore_case_local(kind, "action")) {
        (void)nex_action_policy_add_name(policy->allow_actions, &policy->allow_action_count, value);
        return;
    }
    if (streq_ignore_case_local(verb, "deny") && streq_ignore_case_local(kind, "action")) {
        (void)nex_action_policy_add_name(policy->deny_actions, &policy->deny_action_count, value);
    }
}

static void nex_action_policy_parse(struct nex_action_policy *policy, const char *text) {
    char line[96];
    uint32_t pos = 0;
    uint32_t line_pos = 0;

    while (text != NULL && text[pos] != '\0') {
        line_pos = 0;
        while (text[pos] != '\0' && text[pos] != '\n' && line_pos + 1u < sizeof(line)) {
            line[line_pos++] = text[pos++];
        }
        while (text[pos] != '\0' && text[pos] != '\n') {
            pos++;
        }
        if (text[pos] == '\n') {
            pos++;
        }
        line[line_pos] = '\0';
        nex_action_policy_apply_rule(policy, line);
    }
}

static void nex_action_policy_load_full(struct nex_action_policy *policy) {
    char buffer[NEX_ACTION_POLICY_FILE_MAX];
    int fd;
    uint32_t got;

    nex_action_policy_init(policy);
    fd = open(g_nex_action_caps_path, 0);

    if (fd < 0) {
        return;
    }
    got = (uint32_t)read(fd, buffer, sizeof(buffer) - 1u);
    close((uint32_t)fd);
    if (got == 0u) {
        return;
    }
    buffer[got] = '\0';
    nex_action_policy_parse(policy, buffer);
}

static uint32_t nex_action_policy_load(void) {
    struct nex_action_policy policy;

    nex_action_policy_load_full(&policy);
    return policy.allow_mask & NEX_ACTION_CAP_ALL;
}

static int nex_action_policy_save_full(const struct nex_action_policy *policy) {
    int fd;

    fd = open(g_nex_action_caps_path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        write_err_str("action: cannot write policy file\n");
        return 0;
    }
    fdprintf((uint32_t)fd, "# NexOS Action Permission Policy\n");
    fdprintf((uint32_t)fd, "# format: mask <cap-mask>\n");
    fdprintf((uint32_t)fd, "# format: allow|deny cap <cap|mask|all>\n");
    fdprintf((uint32_t)fd, "# format: allow|deny action <action-id>\n");
    fdprintf((uint32_t)fd, "mask 0x%08X\n", policy != NULL ? policy->allow_mask & NEX_ACTION_CAP_ALL : NEX_ACTION_CAP_ALL);
    if (policy != NULL) {
        for (uint32_t i = 0; i < policy->allow_action_count; i++) {
            fdprintf((uint32_t)fd, "allow action %s\n", policy->allow_actions[i]);
        }
        for (uint32_t i = 0; i < policy->deny_action_count; i++) {
            fdprintf((uint32_t)fd, "deny action %s\n", policy->deny_actions[i]);
        }
    }
    close((uint32_t)fd);
    return 1;
}

static int nex_action_policy_save(uint32_t mask) {
    struct nex_action_policy policy;

    nex_action_policy_load_full(&policy);
    policy.allow_mask = mask & NEX_ACTION_CAP_ALL;
    return nex_action_policy_save_full(&policy);
}

static void nex_action_policy_print(uint32_t mask) {
    write_str("allowed: 0x");
    write_hex_u32(mask & NEX_ACTION_CAP_ALL);
    write_str("\n");
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_nex_action_cap_names) / sizeof(g_nex_action_cap_names[0])); i++) {
        write_str((mask & g_nex_action_cap_names[i].flag) != 0u ? "+ " : "- ");
        write_str(g_nex_action_cap_names[i].name);
        write_str("\n");
    }
}

static void nex_action_policy_print_full(const struct nex_action_policy *policy) {
    if (policy == NULL) {
        return;
    }
    nex_action_policy_print(policy->allow_mask);
    write_str("allow actions:\n");
    if (policy->allow_action_count == 0u) {
        write_str("- none\n");
    }
    for (uint32_t i = 0; i < policy->allow_action_count; i++) {
        write_str("+ ");
        write_str(policy->allow_actions[i]);
        write_str("\n");
    }
    write_str("deny actions:\n");
    if (policy->deny_action_count == 0u) {
        write_str("- none\n");
    }
    for (uint32_t i = 0; i < policy->deny_action_count; i++) {
        write_str("- ");
        write_str(policy->deny_actions[i]);
        write_str("\n");
    }
    write_str("policy file: ");
    write_str(g_nex_action_caps_path);
    write_str("\n");
}

static int nex_action_policy_change(const char *cap_text, int allow) {
    uint32_t change = 0;
    uint32_t mask;

    if (!nex_action_parse_cap_mask(cap_text, &change)) {
        write_err_str("action: unknown capability: ");
        write_err_str(cap_text != NULL ? cap_text : "");
        write_err_str("\n");
        return 1;
    }
    mask = nex_action_policy_load();
    if (allow) {
        mask |= change;
    } else {
        mask &= ~change;
    }
    if (!nex_action_policy_save(mask)) {
        return 1;
    }
    nex_action_policy_print(mask);
    return 0;
}

static int nex_action_policy_change_action(const char *name, int allow) {
    struct nex_action_policy policy;

    if (name == NULL || nex_action_find(name) == NULL) {
        write_err_str("action: unknown action: ");
        write_err_str(name != NULL ? name : "");
        write_err_str("\n");
        return 1;
    }
    nex_action_policy_load_full(&policy);
    if (allow) {
        if (!nex_action_policy_name_matches(policy.allow_actions, policy.allow_action_count, name) &&
            !nex_action_policy_add_name(policy.allow_actions, &policy.allow_action_count, name)) {
            write_err_str("action: too many allow action rules\n");
            return 1;
        }
    } else {
        if (!nex_action_policy_name_matches(policy.deny_actions, policy.deny_action_count, name) &&
            !nex_action_policy_add_name(policy.deny_actions, &policy.deny_action_count, name)) {
            write_err_str("action: too many deny action rules\n");
            return 1;
        }
    }
    if (!nex_action_policy_save_full(&policy)) {
        return 1;
    }
    nex_action_policy_print_full(&policy);
    return 0;
}

static int nex_action_policy_explain(const struct nex_action_entry *action) {
    struct nex_action_policy policy;
    uint32_t missing;

    if (action == NULL) {
        return 1;
    }
    nex_action_policy_load_full(&policy);
    write_str("action: ");
    write_str(action->name);
    write_str("\nrequires: ");
    write_str(action->caps);
    write_str("\ncap_flags: 0x");
    write_hex_u32(action->cap_flags);
    write_str("\n");
    if (nex_action_policy_name_matches(policy.deny_actions, policy.deny_action_count, action->name)) {
        write_str("decision: deny\nreason: explicit action deny rule\n");
        return 0;
    }
    if (nex_action_policy_name_matches(policy.allow_actions, policy.allow_action_count, action->name)) {
        write_str("decision: allow\nreason: explicit action allow rule\n");
        return 0;
    }
    missing = action->cap_flags & ~policy.allow_mask;
    if (missing == 0u) {
        write_str("decision: allow\nreason: required capabilities are allowed\n");
        return 0;
    }
    write_str("decision: deny\nmissing cap_flags: 0x");
    write_hex_u32(missing);
    write_str("\n");
    return 0;
}

static int nex_action_check_allowed(const struct nex_action_entry *action, const char *source_name) {
    struct nex_action_policy policy;
    uint32_t missing;

    if (action == NULL) {
        return 1;
    }
    nex_action_policy_load_full(&policy);
    if (nex_action_policy_name_matches(policy.deny_actions, policy.deny_action_count, action->name)) {
        write_err_str("action: denied: ");
        if (source_name != NULL && !streq_ignore_case_local(source_name, action->name)) {
            write_err_str(source_name);
            write_err_str(" -> ");
        }
        write_err_str(action->name);
        write_err_str(" rule=deny action\n");
        return 0;
    }
    if (nex_action_policy_name_matches(policy.allow_actions, policy.allow_action_count, action->name)) {
        return 1;
    }
    missing = action->cap_flags & ~policy.allow_mask;
    if (missing == 0u) {
        return 1;
    }
    write_err_str("action: denied: ");
    if (source_name != NULL && !streq_ignore_case_local(source_name, action->name)) {
        write_err_str(source_name);
        write_err_str(" -> ");
    }
    write_err_str(action->name);
    write_err_str(" missing cap_flags=0x");
    write_hex_u32(missing);
    write_err_str("\n");
    return 0;
}

static int nex_action_arg_is_named(const char *arg) {
    const char *eq;

    if (arg == NULL) {
        return 0;
    }
    eq = strchr(arg, '=');
    return eq != NULL && eq != arg;
}

static int nex_action_read_token(const char **cursor_io, char *out, uint32_t out_size) {
    const char *cursor;
    uint32_t len = 0;

    if (cursor_io == NULL || *cursor_io == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    cursor = *cursor_io;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor == '\0') {
        *cursor_io = cursor;
        out[0] = '\0';
        return 0;
    }
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
        if (len + 1u >= out_size) {
            return 0;
        }
        out[len++] = *cursor++;
    }
    out[len] = '\0';
    *cursor_io = cursor;
    return 1;
}

static uint32_t nex_action_schema_token_name(const char *token, char *out, uint32_t out_size) {
    uint32_t i = 0;

    if (token == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    while (token[i] != '\0' && token[i] != '?' && token[i] != ':' && token[i] != '/' &&
           token[i] != '|' && token[i] != '.') {
        if (i + 1u >= out_size) {
            return 0;
        }
        out[i] = token[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

static void nex_action_schema_token_type(const char *token, char *out, uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t out_pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (token == NULL) {
        return;
    }
    while (token[pos] != '\0' && token[pos] != ':') {
        pos++;
    }
    if (token[pos] != ':') {
        return;
    }
    pos++;
    while (token[pos] != '\0' && token[pos] != '?' && token[pos] != '/' &&
           token[pos] != '|' && token[pos] != '.') {
        if (out_pos + 1u >= out_size) {
            out[0] = '\0';
            return;
        }
        out[out_pos++] = token[pos++];
    }
    out[out_pos] = '\0';
}

static int nex_action_schema_token_optional(const char *token) {
    uint32_t i = 0;

    while (token != NULL && token[i] != '\0') {
        if (token[i] == '?' || (token[i] == '.' && token[i + 1u] == '.' && token[i + 2u] == '.')) {
            return 1;
        }
        i++;
    }
    return 0;
}

static int nex_action_read_schema_fields(const char *schema,
                                         char names[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_NAME_MAX],
                                         char types[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_TYPE_MAX],
                                         uint8_t optional[NEX_ACTION_ARG_MAX],
                                         uint32_t *count_out) {
    const char *cursor = schema;
    char token[64];
    uint32_t count = 0;

    if (count_out == NULL) {
        return 0;
    }
    *count_out = 0;
    if (schema == NULL || schema[0] == '\0' || streq_ignore_case_local(schema, "none")) {
        return 1;
    }
    while (nex_action_read_token(&cursor, token, sizeof(token))) {
        if (count >= NEX_ACTION_ARG_MAX) {
            write_err_str("action: too many schema fields\n");
            return 0;
        }
        if (nex_action_schema_token_name(token, names[count], NEX_ACTION_ARG_NAME_MAX) == 0u) {
            write_err_str("action: invalid schema field: ");
            write_err_str(token);
            write_err_str("\n");
            return 0;
        }
        nex_action_schema_token_type(token, types[count], NEX_ACTION_ARG_TYPE_MAX);
        optional[count] = (uint8_t)nex_action_schema_token_optional(token);
        count++;
    }
    *count_out = count;
    return 1;
}

static int nex_action_validate_int(const char *value) {
    uint32_t i = 0;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    if (value[0] == '-') {
        i = 1;
    }
    if (value[i] == '\0') {
        return 0;
    }
    while (value[i] != '\0') {
        if (value[i] < '0' || value[i] > '9') {
            return 0;
        }
        i++;
    }
    return 1;
}

static int nex_action_validate_word(const char *value) {
    uint32_t i = 0;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    while (value[i] != '\0') {
        if (value[i] == ' ' || value[i] == '\t' || value[i] == '\r' || value[i] == '\n') {
            return 0;
        }
        i++;
    }
    return 1;
}

static int nex_action_validate_host(const char *value) {
    uint32_t i = 0;

    if (!nex_action_validate_word(value)) {
        return 0;
    }
    while (value[i] != '\0') {
        if (value[i] == '/' || value[i] == '\\') {
            return 0;
        }
        i++;
    }
    return 1;
}

static int nex_action_validate_value(const char *action_name,
                                     const char *field_name,
                                     const char *type,
                                     const char *value) {
    int ok = 1;

    if (value == NULL || value[0] == '\0') {
        ok = 0;
    } else if (type != NULL && type[0] != '\0') {
        if (streq_ignore_case_local(type, "int")) {
            ok = nex_action_validate_int(value);
        } else if (streq_ignore_case_local(type, "path")) {
            ok = value[0] != '\0';
        } else if (streq_ignore_case_local(type, "host")) {
            ok = nex_action_validate_host(value);
        } else if (streq_ignore_case_local(type, "word")) {
            ok = nex_action_validate_word(value);
        } else if (streq_ignore_case_local(type, "text")) {
            ok = value[0] != '\0';
        }
    }
    if (ok) {
        return 1;
    }
    write_err_str("action: invalid ");
    write_err_str(action_name != NULL ? action_name : "action");
    write_err_str(" argument ");
    write_err_str(field_name != NULL ? field_name : "?");
    if (type != NULL && type[0] != '\0') {
        write_err_str(": expected ");
        write_err_str(type);
    }
    write_err_str("\n");
    return 0;
}

static int nex_action_schema_field_index(char names[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_NAME_MAX],
                                         uint32_t count,
                                         const char *name,
                                         uint32_t *index_out) {
    uint32_t i;

    if (index_out == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (streq_ignore_case_local(name, names[i])) {
            *index_out = i;
            return 1;
        }
    }
    return 0;
}

static int nex_action_build_run_argv(const struct nex_action_entry *action,
                                     int argc,
                                     char **argv,
                                     char **run_argv,
                                     char arg_storage[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_VALUE_MAX],
                                     int *run_argc_out) {
    char names[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_NAME_MAX];
    char types[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_TYPE_MAX];
    uint8_t optional[NEX_ACTION_ARG_MAX];
    uint8_t have[NEX_ACTION_ARG_MAX];
    char positional[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_VALUE_MAX];
    uint32_t field_count = 0;
    uint32_t positional_count = 0;
    uint32_t positional_read = 0;
    uint32_t run_argc = 1;
    int has_named = 0;
    int i;

    if (action == NULL || run_argv == NULL || run_argc_out == NULL) {
        return 0;
    }
    if (argc < 0 || argc > (int)NEX_ACTION_ARG_MAX) {
        write_err_str("action: too many arguments\n");
        return 0;
    }
    run_argv[0] = (char *)action->command;
    for (i = 0; i < argc; i++) {
        if (nex_action_arg_is_named(argv[i])) {
            has_named = 1;
            break;
        }
    }
    if (!has_named) {
        for (i = 0; i < argc; i++) {
            run_argv[i + 1] = argv[i];
        }
        *run_argc_out = argc + 1;
        return 1;
    }
    if (!nex_action_read_schema_fields(action->input_schema, names, types, optional, &field_count)) {
        return 0;
    }
    for (uint32_t f = 0; f < NEX_ACTION_ARG_MAX; f++) {
        have[f] = 0;
    }
    for (i = 0; i < argc; i++) {
        char *eq = strchr(argv[i], '=');

        if (nex_action_arg_is_named(argv[i])) {
            char name[NEX_ACTION_ARG_NAME_MAX];
            uint32_t name_len = (uint32_t)(eq - argv[i]);
            uint32_t field;

            if (name_len == 0u || name_len >= sizeof(name)) {
                write_err_str("action: invalid argument name\n");
                return 0;
            }
            for (uint32_t n = 0; n < name_len; n++) {
                name[n] = argv[i][n];
            }
            name[name_len] = '\0';
            if (!nex_action_schema_field_index(names, field_count, name, &field)) {
                write_err_str("action: unknown argument for ");
                write_err_str(action->name);
                write_err_str(": ");
                write_err_str(name);
                write_err_str("\n");
                return 0;
            }
            copy_line_local(arg_storage[field], eq + 1, NEX_ACTION_ARG_VALUE_MAX);
            have[field] = 1;
        } else {
            if (positional_count >= NEX_ACTION_ARG_MAX) {
                write_err_str("action: too many positional arguments\n");
                return 0;
            }
            copy_line_local(positional[positional_count], argv[i], sizeof(positional[positional_count]));
            positional_count++;
        }
    }
    for (uint32_t f = 0; f < field_count && positional_read < positional_count; f++) {
        if (!have[f]) {
            copy_line_local(arg_storage[f], positional[positional_read], NEX_ACTION_ARG_VALUE_MAX);
            have[f] = 1;
            positional_read++;
        }
    }
    for (uint32_t f = 0; f < field_count; f++) {
        if (!have[f] && !optional[f]) {
            write_err_str("action: missing argument for ");
            write_err_str(action->name);
            write_err_str(": ");
            write_err_str(names[f]);
            write_err_str("\n");
            return 0;
        }
        if (have[f]) {
            if (!nex_action_validate_value(action->name, names[f], types[f], arg_storage[f])) {
                return 0;
            }
            run_argv[run_argc++] = arg_storage[f];
        }
    }
    while (positional_read < positional_count && run_argc < NEX_ACTION_ARG_MAX + 1u) {
        copy_line_local(arg_storage[run_argc - 1u], positional[positional_read], NEX_ACTION_ARG_VALUE_MAX);
        run_argv[run_argc] = arg_storage[run_argc - 1u];
        run_argc++;
        positional_read++;
    }
    if (positional_read < positional_count) {
        write_err_str("action: too many arguments\n");
        return 0;
    }
    *run_argc_out = (int)run_argc;
    return 1;
}

static int nex_action_run(const struct nex_action_entry *action, int argc, char **argv) {
    const struct cmdsuite_entry *command;
    char *run_argv[NEX_ACTION_ARG_MAX + 1u];
    char arg_storage[NEX_ACTION_ARG_MAX][NEX_ACTION_ARG_VALUE_MAX];
    int run_argc = 0;

    if (action == NULL) {
        return 1;
    }
    if (!nex_action_check_allowed(action, action->name)) {
        return 1;
    }
    command = cmdsuite_find_entry(action->command);
    if (command == NULL) {
        write_err_str("action: backing command not found: ");
        write_err_str(action->command);
        write_err_str("\n");
        return 1;
    }
    if (!nex_action_build_run_argv(action, argc, argv, run_argv, arg_storage, &run_argc)) {
        return 1;
    }
    return command->handler(run_argc, run_argv);
}

static int cmd_wrap_actions(int argc, char **argv) {
    if (argc == 2 && streq_ignore_case_local(argv[1], "--table")) {
        return nex_action_print_table();
    }
    if (argc != 1) {
        write_err_usage("actions", " [--table]\n");
        return 1;
    }
    return nex_action_print_list();
}

static int cmd_wrap_action(int argc, char **argv) {
    const struct nex_action_entry *action;

    if (argc < 2 || argv[1] == NULL) {
        write_err_usage("action", " <list|caps|policy|allowed|allow|deny|reset|info|run> [name] [args]\n");
        return 1;
    }
    if (streq_ignore_case_local(argv[1], "list")) {
        if (argc == 3 && streq_ignore_case_local(argv[2], "--table")) {
            return nex_action_print_table();
        }
        if (argc != 2) {
            write_err_usage("action list", " [--table]\n");
            return 1;
        }
        return nex_action_print_list();
    }
    if (streq_ignore_case_local(argv[1], "caps")) {
        return nex_action_print_caps();
    }
    if (streq_ignore_case_local(argv[1], "map") || streq_ignore_case_local(argv[1], "mapper")) {
        if (argc == 3 && streq_ignore_case_local(argv[2], "--table")) {
            return nex_mapper_print_table();
        }
        if (argc == 4 && streq_ignore_case_local(argv[2], "info")) {
            const struct nex_mapper_entry *mapper = nex_mapper_find(argv[3]);

            if (mapper == NULL) {
                write_err_str("mapper: not found: ");
                write_err_str(argv[3]);
                write_err_str("\n");
                return 1;
            }
            return nex_mapper_print_info(mapper);
        }
        if (argc != 2) {
            write_err_usage("action map", " [--table|info <command>]\n");
            return 1;
        }
        return nex_mapper_print_list();
    }
    if (streq_ignore_case_local(argv[1], "allowed")) {
        struct nex_action_policy policy;

        nex_action_policy_load_full(&policy);
        nex_action_policy_print_full(&policy);
        return 0;
    }
    if (streq_ignore_case_local(argv[1], "policy")) {
        struct nex_action_policy policy;

        if (argc == 2 || (argc == 3 && streq_ignore_case_local(argv[2], "show"))) {
            nex_action_policy_load_full(&policy);
            nex_action_policy_print_full(&policy);
            return 0;
        }
        if (argc == 3 && streq_ignore_case_local(argv[2], "path")) {
            write_str(g_nex_action_caps_path);
            write_str("\n");
            return 0;
        }
        if (argc == 3 && streq_ignore_case_local(argv[2], "reset")) {
            nex_action_policy_init(&policy);
            if (!nex_action_policy_save_full(&policy)) {
                return 1;
            }
            nex_action_policy_print_full(&policy);
            return 0;
        }
        if (argc == 4 && streq_ignore_case_local(argv[2], "explain")) {
            action = nex_action_find(argv[3]);
            if (action == NULL) {
                write_err_str("action: not found: ");
                write_err_str(argv[3]);
                write_err_str("\n");
                return 1;
            }
            return nex_action_policy_explain(action);
        }
        if (argc == 5 && streq_ignore_case_local(argv[2], "allow") &&
            streq_ignore_case_local(argv[3], "cap")) {
            return nex_action_policy_change(argv[4], 1);
        }
        if (argc == 5 && streq_ignore_case_local(argv[2], "deny") &&
            streq_ignore_case_local(argv[3], "cap")) {
            return nex_action_policy_change(argv[4], 0);
        }
        if (argc == 5 && streq_ignore_case_local(argv[2], "allow") &&
            streq_ignore_case_local(argv[3], "action")) {
            return nex_action_policy_change_action(argv[4], 1);
        }
        if (argc == 5 && streq_ignore_case_local(argv[2], "deny") &&
            streq_ignore_case_local(argv[3], "action")) {
            return nex_action_policy_change_action(argv[4], 0);
        }
        write_err_usage("action policy", " [show|path|reset|explain <action>|allow cap <cap>|deny cap <cap>|allow action <name>|deny action <name>]\n");
        return 1;
    }
    if (streq_ignore_case_local(argv[1], "allow")) {
        if (argc != 3) {
            write_err_usage("action allow", " <cap|mask|all>\n");
            return 1;
        }
        return nex_action_policy_change(argv[2], 1);
    }
    if (streq_ignore_case_local(argv[1], "deny")) {
        if (argc != 3) {
            write_err_usage("action deny", " <cap|mask|all>\n");
            return 1;
        }
        return nex_action_policy_change(argv[2], 0);
    }
    if (streq_ignore_case_local(argv[1], "reset")) {
        struct nex_action_policy policy;

        nex_action_policy_init(&policy);
        if (!nex_action_policy_save_full(&policy)) {
            return 1;
        }
        nex_action_policy_print_full(&policy);
        return 0;
    }
    if (streq_ignore_case_local(argv[1], "info")) {
        if (argc != 3) {
            write_err_usage("action info", " <name>\n");
            return 1;
        }
        action = nex_action_find(argv[2]);
        if (action == NULL) {
            write_err_str("action: not found: ");
            write_err_str(argv[2]);
            write_err_str("\n");
            return 1;
        }
        return nex_action_print_info(action);
    }
    if (streq_ignore_case_local(argv[1], "run")) {
        if (argc < 3) {
            write_err_usage("action run", " <name> [args]\n");
            return 1;
        }
        action = nex_action_find(argv[2]);
        if (action == NULL) {
            write_err_str("action: not found: ");
            write_err_str(argv[2]);
            write_err_str("\n");
            return 1;
        }
        return nex_action_run(action, argc - 3, argv + 3);
    }
    write_err_usage("action", " <list|caps|policy|allowed|allow|deny|reset|info|run> [name] [args]\n");
    return 1;
}

static int cmd_wrap_mapper(int argc, char **argv) {
    const struct nex_mapper_entry *mapper;

    if (argc == 2 && streq_ignore_case_local(argv[1], "--table")) {
        return nex_mapper_print_table();
    }
    if (argc == 3 && streq_ignore_case_local(argv[1], "info")) {
        mapper = nex_mapper_find(argv[2]);
        if (mapper == NULL) {
            write_err_str("mapper: not found: ");
            write_err_str(argv[2]);
            write_err_str("\n");
            return 1;
        }
        return nex_mapper_print_info(mapper);
    }
    if (argc != 1) {
        write_err_usage("mapper", " [--table|info <command>]\n");
        return 1;
    }
    return nex_mapper_print_list();
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
        const struct nex_action_entry *action = nex_action_find_for_friendly_command(name);

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
