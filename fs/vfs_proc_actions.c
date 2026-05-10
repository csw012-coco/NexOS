#include "fs/vfs_internal.h"

enum vfs_proc_action_capability {
    VFS_PROC_ACTION_CAP_FS_READ = 1u << 0,
    VFS_PROC_ACTION_CAP_FS_WRITE = 1u << 1,
    VFS_PROC_ACTION_CAP_FS_INSPECT = 1u << 2,
    VFS_PROC_ACTION_CAP_BLOCK_INSPECT = 1u << 3,
    VFS_PROC_ACTION_CAP_PROC_READ = 1u << 4,
    VFS_PROC_ACTION_CAP_PROC_SIGNAL = 1u << 5,
    VFS_PROC_ACTION_CAP_NET_INSPECT = 1u << 6,
    VFS_PROC_ACTION_CAP_NET_CLIENT = 1u << 7,
    VFS_PROC_ACTION_CAP_NET_CONFIGURE = 1u << 8,
    VFS_PROC_ACTION_CAP_NET_RAW = 1u << 9,
    VFS_PROC_ACTION_CAP_AUDIO_INSPECT = 1u << 10,
    VFS_PROC_ACTION_CAP_AUDIO_PLAY = 1u << 11,
    VFS_PROC_ACTION_CAP_DEBUG_READ = 1u << 12,
    VFS_PROC_ACTION_CAP_HW_INSPECT = 1u << 13,
    VFS_PROC_ACTION_CAP_SYSTEM_INSPECT = 1u << 14
};

static const struct vfs_proc_action_entry g_vfs_proc_actions[] = {
    {"audio.list", "audio", "audio", "none", "table/audio-devices", VFS_PROC_ACTION_CAP_AUDIO_INSPECT, "audio.inspect", "list audio devices"},
    {"audio.tone", "audio", "tone", "hz:int ms:int device?:int", "none", VFS_PROC_ACTION_CAP_AUDIO_PLAY, "audio.play", "play a simple tone"},
    {"audio.play_wav", "audio", "mplay", "path:path", "none", VFS_PROC_ACTION_CAP_AUDIO_PLAY | VFS_PROC_ACTION_CAP_FS_READ, "audio.play fs.read", "play a WAV file"},
    {"debug.kmsg", "debug", "dmesg", "none", "text/kmsg", VFS_PROC_ACTION_CAP_DEBUG_READ, "debug.read", "read kernel log"},
    {"debug.pci", "debug", "lspci", "none", "table/pci", VFS_PROC_ACTION_CAP_HW_INSPECT, "hw.inspect", "inspect PCI devices"},
    {"fd.read", "fd", "cat", "path:path", "fd-stream", VFS_PROC_ACTION_CAP_FS_READ | VFS_PROC_ACTION_CAP_PROC_READ | VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "fs.read proc.read system.inspect", "read an fd-backed resource"},
    {"proc.read_node", "fd", "cat", "path:path", "procfs-node", VFS_PROC_ACTION_CAP_PROC_READ | VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "proc.read system.inspect", "read a procfs node through fd"},
    {"event.read", "fd", "cat", "path:path flags?:word", "event-stream|json", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "read an EventFS stream through fd"},
    {"device.read", "fd", "cat", "path:path", "device-stream", VFS_PROC_ACTION_CAP_HW_INSPECT | VFS_PROC_ACTION_CAP_BLOCK_INSPECT, "hw.inspect block.inspect", "read a devfs node through fd"},
    {"file.read", "file", "cat", "path:path", "text", VFS_PROC_ACTION_CAP_FS_READ, "fs.read", "read a file"},
    {"fs.list", "storage", "ls", "path?:path", "dirent-list", VFS_PROC_ACTION_CAP_FS_READ, "fs.read", "list directory contents"},
    {"fs.copy", "storage", "cp", "src:path dst:path", "none", VFS_PROC_ACTION_CAP_FS_READ | VFS_PROC_ACTION_CAP_FS_WRITE, "fs.read fs.write", "copy a file"},
    {"fs.mounts", "storage", "mounts", "none", "table/mounts", VFS_PROC_ACTION_CAP_FS_INSPECT, "fs.inspect", "list mounted filesystems"},
    {"fs.block_devices", "storage", "blk", "none", "table/block-devices", VFS_PROC_ACTION_CAP_BLOCK_INSPECT, "block.inspect", "list block devices"},
    {"device.hotplug", "storage", "hotplug", "op?:word disk?:int part?:int", "table/hotplug", VFS_PROC_ACTION_CAP_BLOCK_INSPECT | VFS_PROC_ACTION_CAP_FS_INSPECT, "block.inspect fs.inspect", "scan or automount discovered partitions"},
    {"net.config", "net", "ifconfig", "none", "record/net-config", VFS_PROC_ACTION_CAP_NET_INSPECT, "net.inspect", "show network config"},
    {"net.dhcp", "net", "dhcp", "none", "record/net-config", VFS_PROC_ACTION_CAP_NET_CONFIGURE, "net.configure", "request DHCP configuration"},
    {"net.dns", "net", "dns", "host:host type?:word", "dns-answer", VFS_PROC_ACTION_CAP_NET_CLIENT, "net.client", "resolve a DNS name"},
    {"net.http_get", "net", "wget", "url:host output?:path", "file|stdout", VFS_PROC_ACTION_CAP_NET_CLIENT | VFS_PROC_ACTION_CAP_FS_WRITE, "net.client fs.write", "fetch HTTP content"},
    {"net.ping", "net", "ping", "host?:host", "icmp-result", VFS_PROC_ACTION_CAP_NET_RAW, "net.raw", "send an ICMP echo request"},
    {"event.file_change", "event", "on", "event:word path:path action:word", "none", VFS_PROC_ACTION_CAP_FS_READ, "fs.read", "run a command when a file changes"},
    {"event.timer", "event", "on", "event:word interval?:word action:word", "event/timer", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "run a command on timer ticks"},
    {"event.input.keyboard", "event", "on", "event:word key:word interval?:word action:word", "event/input/keyboard", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "run a command on keyboard events"},
    {"event.input.mouse", "event", "on", "event:word button?:word interval?:word action:word", "event/input/mouse", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "run a command on mouse events"},
    {"event.net.status", "event", "on", "event:word state?:word interval?:word action:word", "event/net/status", VFS_PROC_ACTION_CAP_NET_INSPECT, "net.inspect", "run a command on network status changes"},
    {"event.block.change", "event", "on", "event:word op?:word interval?:word action:word", "event/block/change", VFS_PROC_ACTION_CAP_BLOCK_INSPECT, "block.inspect", "run a command on block device changes"},
    {"event.jobs", "event", "events", "op?:word id?:word", "table/event-jobs", VFS_PROC_ACTION_CAP_PROC_READ | VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "proc.read system.inspect", "manage background event jobs"},
    {"event.as_table", "event", "as", "type:word", "table/events", 0, "none", "convert EventFS text events to a typed table"},
    {"proc.jobs", "process", "jobs", "none", "table/jobs", VFS_PROC_ACTION_CAP_PROC_READ, "proc.read", "list shell jobs"},
    {"proc.list", "process", "ps", "none", "table/processes", VFS_PROC_ACTION_CAP_PROC_READ, "proc.read", "list processes"},
    {"proc.kill", "process", "kill", "pid:int", "none", VFS_PROC_ACTION_CAP_PROC_SIGNAL, "proc.signal", "kill a process"},
    {"system.cpu", "system", "cpuinfo", "none", "record/cpu", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "show CPU information"},
    {"system.clock", "system", "hwclock", "flags?:word", "record/rtc", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "read CMOS RTC clock"},
    {"system.mem", "system", "meminfo", "none", "record/memory", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "show memory information"},
    {"session.image", "system", "session", "op:word name?:word", "session-image", VFS_PROC_ACTION_CAP_FS_READ | VFS_PROC_ACTION_CAP_FS_WRITE | VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "fs.read fs.write system.inspect", "save or inspect a session image"},
    {"system.service", "system", "service", "op:word name?:word command?:text", "table/services", VFS_PROC_ACTION_CAP_FS_READ | VFS_PROC_ACTION_CAP_FS_WRITE | VFS_PROC_ACTION_CAP_PROC_READ | VFS_PROC_ACTION_CAP_PROC_SIGNAL | VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "fs.read fs.write proc.read proc.signal system.inspect", "define and manage boot services"},
    {"system.uname", "system", "uname", "flags?:word", "record/system", VFS_PROC_ACTION_CAP_SYSTEM_INSPECT, "system.inspect", "show system identity"},
    {"text.grep", "text", "grep", "pattern:text file?:path", "text", VFS_PROC_ACTION_CAP_FS_READ, "fs.read", "filter text"},
    {"text.view", "text", "cat", "path?:path", "text", VFS_PROC_ACTION_CAP_FS_READ, "fs.read", "print file contents"},
    {"table.as", "table", "as", "type:word", "typed-stream", 0, "none", "mark stream with a NexOS type"},
    {"table.pick", "table", "pick", "column:word", "typed-stream", 0, "none", "filter a typed table by column"},
    {"table.select", "table", "select", "columns...:word", "typed-stream", 0, "none", "project table columns"},
    {"table.sort", "table", "sort-by", "column:word", "typed-stream", 0, "none", "sort table rows by a column"},
    {"table.count", "table", "count-by", "column:word", "typed-stream", 0, "none", "count rows by column value"},
    {"table.json", "table", "to", "format:word", "json", 0, "none", "convert a typed table to JSON"},
    {"table.view", "table", "view", "format:word", "text/table", 0, "none", "render a typed table for humans"},
};

uint32_t vfs_proc_action_count(void) {
    return (uint32_t)(sizeof(g_vfs_proc_actions) / sizeof(g_vfs_proc_actions[0]));
}

const struct vfs_proc_action_entry *vfs_proc_action_at(uint32_t index) {
    if (index >= vfs_proc_action_count()) {
        return 0;
    }
    return &g_vfs_proc_actions[index];
}
