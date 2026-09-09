#include "user/apps/elf/ush/ush_exec_internal.h"

int ush_is_nexbox32_applet_name(const char *name) {
    static const char *const applets[] = {
        "help", "actions", "action", "mapper", "echo", "yes", "clear",
        "pwd", "tty", "env", "font", "which", "type", "ls", "cat",
        "less", "hexdump", "grep", "date", "hwclock", "sleep", "watch",
        "on", "events", "clipboard", "wc", "head", "tail", "find",
        "as", "pick", "select", "sort-by", "count-by", "to", "view",
        "ed", "vi", "vim", "touch", "mv", "cp", "mkdir", "rmdir",
        "rm", "asm", "stat", "du", "tree", "file", "blk", "parts",
        "fdisk", "dd", "mkfs", "df", "mounts", "progs", "fatls",
        "fatfind", "fatread", "cpio", "mount", "umount", "hotplug",
        "run", "runelf", "runbg", "ps", "id", "whoami", "su", "sudo",
        "login", "getty", "session", "service", "jobs",
        "wait", "alarm", "timeout", "kill", "fg", "bg", "switch_root",
        "reboot", "dmesg", "lspci", "ac97", "hda", "rtl8139",
        "rtl8139tx", "rtl8139rx", "arp", "route", "netstat", "ping",
        "dns", "dhcp", "ifconfig", "http", "wget", "nc", "audio",
        "tone", "wav", "mplay", "doctor", "nexctl", "sysinfo",
        "meminfo", "minfo", "uname", "cpuinfo", "config", "dbg",
        "nexbox", "nexbox32"
    };

    for (uint32_t i = 0u; i < sizeof(applets) / sizeof(applets[0]); i++) {
        if (streq_local(name, applets[i])) {
            return 1;
        }
    }
    return 0;
}
