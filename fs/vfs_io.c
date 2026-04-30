#include "fs/vfs_internal.h"
#include "block/block_event.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "drivers/net/net_event.h"
#include "drivers/rtc/cmos.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"
#include "kernel/public/sys/syscall.h"
#include "lib/string.h"

static uint8_t g_vfs_block_buffer[VFS_DEV_BLOCK_BUFFER_SIZE];

#define VFS_FILE_EVENT_QUEUE_SIZE 64u

struct vfs_file_event_record {
    uint32_t seq;
    uint32_t tick;
    uint32_t native_id;
    uint32_t bytes;
    uint32_t mount_slot;
    uint8_t mount_kind;
    char op[12];
    char path[NOS_PATH_BUFFER_SIZE];
};

static struct vfs_file_event_record g_file_event_queue[VFS_FILE_EVENT_QUEUE_SIZE];
static uint32_t g_file_event_head;
static uint32_t g_file_event_tail;
static uint32_t g_file_event_count;
static uint32_t g_file_event_dropped;
static uint32_t g_file_event_seq;

static void vfs_copy_bytes(void *dest, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dest;
    const uint8_t *in = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void vfs_fill_bytes(void *dest, uint8_t value, uint32_t size) {
    uint8_t *out = (uint8_t *)dest;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = value;
    }
}

static int vfs_is_decimal_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static void vfs_terminate_text(char *dst, uint32_t pos, uint32_t dst_size) {
    if (dst == 0 || dst_size == 0) {
        return;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return;
    }
    dst[pos] = '\0';
}

static uint32_t vfs_append_text(char *dst, uint32_t pos, uint32_t dst_size, const char *text) {
    if (dst == 0 || dst_size == 0) {
        return pos;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return dst_size - 1u;
    }
    while (text != 0 && *text != '\0' && pos < dst_size - 1u) {
        dst[pos++] = *text++;
    }
    vfs_terminate_text(dst, pos, dst_size);
    return pos;
}

static uint32_t vfs_append_u32_text(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    char digits[10];
    uint32_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && count < sizeof(digits));
    if (dst == 0 || dst_size == 0) {
        return pos;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return dst_size - 1u;
    }
    while (count > 0 && pos < dst_size - 1u) {
        dst[pos++] = digits[--count];
    }
    vfs_terminate_text(dst, pos, dst_size);
    return pos;
}

static uint32_t vfs_append_i32_text(char *dst, uint32_t pos, uint32_t dst_size, int32_t value) {
    if (value < 0) {
        pos = vfs_append_text(dst, pos, dst_size, "-");
        return vfs_append_u32_text(dst, pos, dst_size, (uint32_t)(-value));
    }
    return vfs_append_u32_text(dst, pos, dst_size, (uint32_t)value);
}

static uint32_t vfs_append_padded2_text(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    if (value < 10u) {
        pos = vfs_append_text(dst, pos, dst_size, "0");
    }
    return vfs_append_u32_text(dst, pos, dst_size, value);
}

static uint32_t vfs_append_hex_u32_text(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";

    pos = vfs_append_text(dst, pos, dst_size, "0x");
    for (int i = 7; i >= 0; i--) {
        char digit = hex[(value >> ((uint32_t)i * 4u)) & 0x0fu];

        if (dst != 0 && dst_size != 0 && pos < dst_size - 1u) {
            dst[pos++] = digit;
            dst[pos] = '\0';
        }
    }
    return pos;
}

static uint32_t vfs_append_bool_text(char *dst, uint32_t pos, uint32_t dst_size, uint8_t value) {
    return vfs_append_text(dst, pos, dst_size, value ? "1" : "0");
}

static uint32_t vfs_append_keyboard_ascii_text(char *dst, uint32_t pos, uint32_t dst_size, char ascii) {
    if (ascii == 0) {
        return vfs_append_text(dst, pos, dst_size, "none");
    }
    if (ascii == '\n') {
        return vfs_append_text(dst, pos, dst_size, "\\n");
    }
    if (ascii == '\t') {
        return vfs_append_text(dst, pos, dst_size, "\\t");
    }
    if (ascii == '\b') {
        return vfs_append_text(dst, pos, dst_size, "\\b");
    }
    if (ascii == ' ') {
        return vfs_append_text(dst, pos, dst_size, "space");
    }
    if ((uint8_t)ascii < 32u || (uint8_t)ascii >= 127u) {
        return vfs_append_text(dst, pos, dst_size, "control");
    }
    if (dst != 0 && dst_size != 0 && pos < dst_size - 1u) {
        dst[pos++] = ascii;
        vfs_terminate_text(dst, pos, dst_size);
    }
    return pos;
}

static uint32_t vfs_append_json_string(char *dst, uint32_t pos, uint32_t dst_size, const char *text) {
    pos = vfs_append_text(dst, pos, dst_size, "\"");
    while (text != 0 && *text != '\0' && dst != 0 && dst_size > 2u && pos < dst_size - 2u) {
        if (*text == '"' || *text == '\\') {
            if (dst_size <= 3u || pos >= dst_size - 3u) {
                break;
            }
            dst[pos++] = '\\';
            dst[pos++] = *text++;
            dst[pos] = '\0';
        } else {
            dst[pos++] = *text++;
            dst[pos] = '\0';
        }
    }
    return vfs_append_text(dst, pos, dst_size, "\"");
}

static void vfs_copy_event_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i < dst_size - 1u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t vfs_append_padded_text(char *dst,
                                       uint32_t pos,
                                       uint32_t dst_size,
                                       const char *text,
                                       uint32_t width) {
    uint32_t len = 0;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    pos = vfs_append_text(dst, pos, dst_size, text);
    if (dst == 0 || dst_size == 0) {
        return pos;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return dst_size - 1u;
    }
    while (len < width && pos < dst_size - 1u) {
        dst[pos++] = ' ';
        dst[pos] = '\0';
        len++;
    }
    return pos;
}

static int vfs_parse_u32_full(const char *text, uint32_t *out) {
    uint32_t value = 0;

    if (text == 0 || *text == '\0' || out == 0) {
        return 0;
    }
    while (*text != '\0') {
        if (!vfs_is_decimal_digit(*text)) {
            return 0;
        }
        value = value * 10u + (uint32_t)(*text - '0');
        text++;
    }
    *out = value;
    return 1;
}

struct vfs_proc_action_entry {
    const char *name;
    const char *group;
    const char *command;
    const char *input_schema;
    const char *output_schema;
    uint32_t cap_flags;
    const char *caps;
    const char *summary;
};

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

static int64_t vfs_emit_dir_entry(struct vfs_dirent *entry,
                                  uint32_t *index_io,
                                  const char *name,
                                  uint32_t size,
                                  uint8_t attributes) {
    vfs_copy_name(entry->name, sizeof(entry->name), name);
    entry->size = size;
    entry->attributes = attributes;
    if (index_io != 0) {
        (*index_io)++;
    }
    return 1;
}

static int vfs_process_get_pid(uint32_t pid, struct process_snapshot *out) {
    for (uint32_t i = 0; i < process_capacity(); i++) {
        struct process_snapshot snapshot;

        if (!process_get(i, &snapshot) || snapshot.pid != pid) {
            continue;
        }
        if (out != 0) {
            *out = snapshot;
        }
        return 1;
    }
    return 0;
}

static const char *vfs_process_state_name(uint32_t state) {
    if (state == PROCESS_STATE_READY) {
        return "ready";
    }
    if (state == PROCESS_STATE_RUNNING) {
        return "running";
    }
    if (state == PROCESS_STATE_SLEEPING) {
        return "sleeping";
    }
    if (state == PROCESS_STATE_STOPPED) {
        return "stopped";
    }
    if (state == PROCESS_STATE_EXITED) {
        return "exited";
    }
    if (state == PROCESS_STATE_WAITING) {
        return "waiting";
    }
    return "free";
}

int64_t vfs_read_from_fat32(struct vfs *vfs,
                            struct vfs_node *node,
                            uint32_t *offset_io,
                            void *buffer,
                            uint32_t size) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    uint32_t bytes_read = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_read_file_range(fat32, &node->handle.fat32_file, *offset_io, buffer, size, &bytes_read) != 0) {
        return -1;
    }
    *offset_io += bytes_read;
    return (int64_t)bytes_read;
}

int64_t vfs_read_from_nxfs(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           void *buffer,
                           uint32_t size) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    uint32_t bytes_read = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_read_file_range(nxfs, &node->handle.nxfs_inode, *offset_io, buffer, size, &bytes_read) != 0) {
        return -1;
    }
    *offset_io += bytes_read;
    return (int64_t)bytes_read;
}

static int64_t vfs_read_devfs_zero(uint32_t *offset_io, void *buffer, uint32_t size) {
    vfs_fill_bytes(buffer, 0, size);
    *offset_io += size;
    return (int64_t)size;
}

static int64_t vfs_emit_devfs_block_entry(struct vfs_dirent *entry,
                                          uint32_t *index_io,
                                          uint32_t disk_index,
                                          uint32_t part_index,
                                          struct block_device *dev) {
    if (part_index == VFS_PARTITION_RAW) {
        struct blockdev_info info;

        if (dev == 0 || blockdev_get_info(disk_index, &info) != 0) {
            return 0;
        }
        vfs_format_disk_node_name(entry->name, sizeof(entry->name), disk_index);
        return vfs_emit_dir_entry(entry,
                                  index_io,
                                  entry->name,
                                  (uint32_t)(info.block_count * info.block_size),
                                  0);
    }

    {
        struct blockdev_partition part;

        if (dev == 0 || blockdev_partition_get(dev, part_index, &part) != 0) {
            return 0;
        }
        vfs_format_partition_node_name(entry->name, sizeof(entry->name), disk_index, part_index);
        return vfs_emit_dir_entry(entry,
                                  index_io,
                                  entry->name,
                                  part.sector_count * dev->block_size,
                                  0);
    }
}

static int64_t vfs_read_from_devfs(struct vfs_node *node,
                                   uint32_t *offset_io,
                                   void *buffer,
                                   uint32_t size,
                                   uint32_t flags) {
    uint64_t base_lba;
    uint64_t block_count;
    struct block_device *dev = vfs_blockdev_from_node(node, &base_lba, &block_count);

    (void)flags;
    if (node->aux_index == VFS_DEV_TTY || node->aux_index == VFS_DEV_STDIN) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_NULL) {
        return 0;
    }
    if (node->aux_index == VFS_DEV_STDOUT || node->aux_index == VFS_DEV_STDERR) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_ZERO) {
        return vfs_read_devfs_zero(offset_io, buffer, size);
    }
    if (dev == 0) {
        return -1;
    }
    return vfs_blockdev_read_bytes(dev, base_lba, block_count, offset_io, buffer, size);
}

static int64_t vfs_read_from_generated_text(uint32_t *offset_io,
                                            void *buffer,
                                            uint32_t size,
                                            const char *text,
                                            uint32_t text_size) {
    uint32_t copied = 0;
    uint8_t *out = (uint8_t *)buffer;

    if (offset_io == 0 || buffer == 0 || text == 0) {
        return -1;
    }
    if (*offset_io >= text_size) {
        return 0;
    }
    while (copied < size && *offset_io + copied < text_size) {
        out[copied] = (uint8_t)text[*offset_io + copied];
        copied++;
    }
    *offset_io += copied;
    return (int64_t)copied;
}

static uint32_t vfs_format_proc_meminfo(char *text, uint32_t size) {
    uint32_t pos = 0;
    uint32_t total = pmm_total_pages();
    uint32_t free = pmm_free_pages();
    uint32_t used = pmm_used_pages();
    uint32_t dropped = pmm_dropped_pages();

    pos = vfs_append_text(text, pos, size, "MemTotalPages: ");
    pos = vfs_append_u32_text(text, pos, size, total);
    pos = vfs_append_text(text, pos, size, "\nMemFreePages: ");
    pos = vfs_append_u32_text(text, pos, size, free);
    pos = vfs_append_text(text, pos, size, "\nMemUsedPages: ");
    pos = vfs_append_u32_text(text, pos, size, used);
    pos = vfs_append_text(text, pos, size, "\nMemDroppedPages: ");
    pos = vfs_append_u32_text(text, pos, size, dropped);
    pos = vfs_append_text(text, pos, size, "\nMemTotalKB: ");
    pos = vfs_append_u32_text(text, pos, size, total * 4u);
    pos = vfs_append_text(text, pos, size, "\nMemFreeKB: ");
    pos = vfs_append_u32_text(text, pos, size, free * 4u);
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static const char *vfs_proc_mount_kind_name(uint8_t kind) {
    if (kind == VFS_MOUNT_DEVFS) {
        return "devfs";
    }
    if (kind == VFS_MOUNT_PROCFS) {
        return "procfs";
    }
    if (kind == VFS_MOUNT_EVENTFS) {
        return "eventfs";
    }
    if (kind == VFS_MOUNT_FAT32) {
        return "fat32";
    }
    if (kind == VFS_MOUNT_NXFS) {
        return "nxfs";
    }
    return "unknown";
}

static uint32_t vfs_format_proc_mounts(struct vfs *vfs, char *text, uint32_t size) {
    uint32_t pos = 0;

    for (uint32_t i = 0; i < vfs_builtin_mount_count(vfs); i++) {
        struct vfs_mount_info info;
        uint32_t source_known = 0;

        if (!vfs_get_builtin_mount(vfs, i, &info, &source_known)) {
            continue;
        }
        (void)source_known;
        pos = vfs_append_text(text, pos, size, vfs_proc_mount_kind_name(info.kind));
        pos = vfs_append_text(text, pos, size, " /");
        pos = vfs_append_text(text, pos, size, info.name);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    for (uint32_t i = 0; i < vfs_mount_count(vfs); i++) {
        struct vfs_mount_info info;

        if (vfs_get_mount(vfs, i, &info) != 0) {
            continue;
        }
        pos = vfs_append_text(text, pos, size, vfs_proc_mount_kind_name(info.kind));
        pos = vfs_append_text(text, pos, size, " /");
        pos = vfs_append_text(text, pos, size, info.name);
        pos = vfs_append_text(text, pos, size, " /dev/disk");
        pos = vfs_append_u32_text(text, pos, size, info.disk_index);
        if (info.part_index != VFS_PARTITION_RAW) {
            pos = vfs_append_text(text, pos, size, "p");
            pos = vfs_append_u32_text(text, pos, size, info.part_index + 1u);
        }
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_uptime(char *text, uint32_t size) {
    uint32_t ticks = sched_current_ticks();
    uint32_t pos = 0;

    pos = vfs_append_u32_text(text, pos, size, ticks);
    pos = vfs_append_text(text, pos, size, " ticks\n");
    return pos;
}

static uint32_t vfs_format_proc_rtc(char *text, uint32_t size) {
    struct cmos_rtc_info rtc;
    uint32_t pos = 0;

    if (!cmos_rtc_query(&rtc) || !rtc.present) {
        return vfs_append_text(text, pos, size, "present: 0\n");
    }
    pos = vfs_append_text(text, pos, size, "present: ");
    pos = vfs_append_bool_text(text, pos, size, rtc.present);
    pos = vfs_append_text(text, pos, size, "\nvalid: ");
    pos = vfs_append_bool_text(text, pos, size, rtc.valid);
    pos = vfs_append_text(text, pos, size, "\nupdating: ");
    pos = vfs_append_bool_text(text, pos, size, rtc.updating);
    pos = vfs_append_text(text, pos, size, "\ntime: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.year);
    pos = vfs_append_text(text, pos, size, "-");
    pos = vfs_append_padded2_text(text, pos, size, rtc.month);
    pos = vfs_append_text(text, pos, size, "-");
    pos = vfs_append_padded2_text(text, pos, size, rtc.day);
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_padded2_text(text, pos, size, rtc.hour);
    pos = vfs_append_text(text, pos, size, ":");
    pos = vfs_append_padded2_text(text, pos, size, rtc.minute);
    pos = vfs_append_text(text, pos, size, ":");
    pos = vfs_append_padded2_text(text, pos, size, rtc.second);
    pos = vfs_append_text(text, pos, size, "\nweekday: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.weekday);
    pos = vfs_append_text(text, pos, size, "\nunix_time: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.unix_time);
    pos = vfs_append_text(text, pos, size, "\nmode: ");
    pos = vfs_append_text(text, pos, size, rtc.binary_mode ? "binary" : "bcd");
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_text(text, pos, size, rtc.hour_24 ? "24h" : "12h");
    pos = vfs_append_text(text, pos, size, "\nstatus_a: ");
    pos = vfs_append_hex_u32_text(text, pos, size, rtc.status_a);
    pos = vfs_append_text(text, pos, size, "\nstatus_b: ");
    pos = vfs_append_hex_u32_text(text, pos, size, rtc.status_b);
    pos = vfs_append_text(text, pos, size, "\ncentury: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.century);
    pos = vfs_append_text(text, pos, size, "\nraw_year: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.raw_year);
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_proc_actions(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "name", 18u);
    pos = vfs_append_padded_text(text, pos, size, "group", 10u);
    pos = vfs_append_padded_text(text, pos, size, "command", 10u);
    pos = vfs_append_padded_text(text, pos, size, "input", 16u);
    pos = vfs_append_padded_text(text, pos, size, "output", 20u);
    pos = vfs_append_padded_text(text, pos, size, "cap_flags", 12u);
    pos = vfs_append_padded_text(text, pos, size, "caps", 20u);
    pos = vfs_append_text(text, pos, size, "summary\n");
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_vfs_proc_actions) / sizeof(g_vfs_proc_actions[0])); i++) {
        pos = vfs_append_padded_text(text, pos, size, g_vfs_proc_actions[i].name, 18u);
        pos = vfs_append_padded_text(text, pos, size, g_vfs_proc_actions[i].group, 10u);
        pos = vfs_append_padded_text(text, pos, size, g_vfs_proc_actions[i].command, 10u);
        pos = vfs_append_padded_text(text, pos, size, g_vfs_proc_actions[i].input_schema, 16u);
        pos = vfs_append_padded_text(text, pos, size, g_vfs_proc_actions[i].output_schema, 20u);
        pos = vfs_append_hex_u32_text(text, pos, size, g_vfs_proc_actions[i].cap_flags);
        pos = vfs_append_padded_text(text, pos, size, "", 2u);
        pos = vfs_append_padded_text(text, pos, size, g_vfs_proc_actions[i].caps, 20u);
        pos = vfs_append_text(text, pos, size, g_vfs_proc_actions[i].summary);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_pid_status(uint32_t pid, char *text, uint32_t size) {
    struct process_snapshot proc;
    uint32_t pos = 0;

    if (!vfs_process_get_pid(pid, &proc)) {
        return 0;
    }
    pos = vfs_append_text(text, pos, size, "Name:\t");
    pos = vfs_append_text(text, pos, size, proc.name[0] != '\0' ? proc.name : "(unnamed)");
    pos = vfs_append_text(text, pos, size, "\nPid:\t");
    pos = vfs_append_u32_text(text, pos, size, proc.pid);
    pos = vfs_append_text(text, pos, size, "\nSlot:\t");
    pos = vfs_append_u32_text(text, pos, size, proc.slot);
    pos = vfs_append_text(text, pos, size, "\nState:\t");
    pos = vfs_append_text(text, pos, size, vfs_process_state_name(proc.state));
    pos = vfs_append_text(text, pos, size, "\nExitCode:\t");
    pos = vfs_append_i32_text(text, pos, size, proc.exit_code);
    pos = vfs_append_text(text, pos, size, "\nWakeTick:\t");
    pos = vfs_append_u32_text(text, pos, size, proc.wake_tick);
    pos = vfs_append_text(text, pos, size, "\nImage:\t");
    pos = vfs_append_text(text, pos, size, proc.image_kind == PROCESS_IMAGE_ELF ? "elf" : "none");
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_keyboard_events(struct vfs_node *node, char *text, uint32_t size) {
    struct keyboard_event_record rec;
    uint32_t pos = 0;
    uint32_t emitted = 0;
    uint32_t dropped;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = keyboard_event_queue_latest_seq();
        return 0;
    }
    dropped = keyboard_event_queue_dropped();
    while (node != 0 && pos + 96u < size && keyboard_event_queue_get_after(&node->aux_data, &rec)) {
        pos = vfs_append_text(text, pos, size, "event input.keyboard seq=");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, " tick=");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, " state=");
        pos = vfs_append_text(text, pos, size, rec.event.pressed ? "press" : (rec.event.released ? "release" : "state"));
        pos = vfs_append_text(text, pos, size, " key=");
        pos = vfs_append_keyboard_ascii_text(text, pos, size, rec.event.ascii);
        pos = vfs_append_text(text, pos, size, " code=");
        pos = vfs_append_u32_text(text, pos, size, (uint32_t)rec.event.keycode);
        pos = vfs_append_text(text, pos, size, " shift=");
        pos = vfs_append_bool_text(text, pos, size, rec.event.shift);
        pos = vfs_append_text(text, pos, size, " ctrl=");
        pos = vfs_append_bool_text(text, pos, size, rec.event.ctrl);
        pos = vfs_append_text(text, pos, size, " caps=");
        pos = vfs_append_bool_text(text, pos, size, rec.event.caps_lock);
        pos = vfs_append_text(text, pos, size, "\n");
        emitted++;
    }
    if (emitted == 0) {
        pos = vfs_append_text(text, pos, size, "type input.keyboard\n");
        pos = vfs_append_text(text, pos, size, "status empty\npending ");
        pos = vfs_append_u32_text(text, pos, size, keyboard_event_queue_pending());
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, dropped);
        pos = vfs_append_text(text, pos, size, "\nsource /event/input/keyboard\n");
    } else {
        pos = vfs_append_text(text, pos, size, "pending ");
        pos = vfs_append_u32_text(text, pos, size, keyboard_event_queue_pending());
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, dropped);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

void vfs_event_file_change_emit(const char *op,
                                const char *path,
                                uint8_t mount_kind,
                                uint32_t mount_slot,
                                uint32_t native_id,
                                uint32_t bytes) {
    struct vfs_file_event_record *slot;
    uint32_t head;

    if (op == 0 || op[0] == '\0') {
        return;
    }
    if (g_file_event_count >= VFS_FILE_EVENT_QUEUE_SIZE) {
        g_file_event_tail = (g_file_event_tail + 1u) % VFS_FILE_EVENT_QUEUE_SIZE;
        g_file_event_count--;
        g_file_event_dropped++;
    }
    head = g_file_event_head;
    slot = &g_file_event_queue[head];
    slot->seq = ++g_file_event_seq;
    slot->tick = sched_current_ticks();
    slot->native_id = native_id;
    slot->bytes = bytes;
    slot->mount_kind = mount_kind;
    slot->mount_slot = mount_slot;
    vfs_copy_event_text(slot->op, sizeof(slot->op), op);
    vfs_copy_event_text(slot->path, sizeof(slot->path), path != 0 && path[0] != '\0' ? path : "-");
    g_file_event_head = (head + 1u) % VFS_FILE_EVENT_QUEUE_SIZE;
    g_file_event_count++;
}

static int vfs_file_event_get_after(uint32_t *cursor_io, struct vfs_file_event_record *out) {
    uint32_t index;

    if (cursor_io == 0 || out == 0 || g_file_event_count == 0) {
        return 0;
    }
    index = g_file_event_tail;
    for (uint32_t i = 0; i < g_file_event_count; i++) {
        const struct vfs_file_event_record *rec = &g_file_event_queue[index];

        if (rec->seq > *cursor_io) {
            *out = *rec;
            *cursor_io = rec->seq;
            return 1;
        }
        index = (index + 1u) % VFS_FILE_EVENT_QUEUE_SIZE;
    }
    return 0;
}

static uint32_t vfs_format_file_events(struct vfs_node *node, char *text, uint32_t size) {
    struct vfs_file_event_record rec;
    uint32_t pos = 0;
    uint32_t emitted = 0;
    uint32_t dropped = g_file_event_dropped;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = g_file_event_seq;
        return 0;
    }
    while (node != 0 && pos + 120u < size && vfs_file_event_get_after(&node->aux_data, &rec)) {
        pos = vfs_append_text(text, pos, size, "event file.change seq=");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, " tick=");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, " op=");
        pos = vfs_append_text(text, pos, size, rec.op);
        pos = vfs_append_text(text, pos, size, " path=");
        pos = vfs_append_text(text, pos, size, rec.path);
        pos = vfs_append_text(text, pos, size, " bytes=");
        pos = vfs_append_u32_text(text, pos, size, rec.bytes);
        pos = vfs_append_text(text, pos, size, " id=");
        pos = vfs_append_u32_text(text, pos, size, rec.native_id);
        pos = vfs_append_text(text, pos, size, "\n");
        emitted++;
    }
    if (emitted == 0) {
        pos = vfs_append_text(text, pos, size, "type file.change\nstatus empty\npending ");
        pos = vfs_append_u32_text(text, pos, size, g_file_event_count);
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, dropped);
        pos = vfs_append_text(text, pos, size, "\nsource /event/file/change\n");
    } else {
        pos = vfs_append_text(text, pos, size, "pending ");
        pos = vfs_append_u32_text(text, pos, size, g_file_event_count);
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, dropped);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_keyboard_events_json(struct vfs_node *node, char *text, uint32_t size) {
    struct keyboard_event_record rec;
    uint32_t pos = 0;
    int first = 1;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = keyboard_event_queue_latest_seq();
    }
    pos = vfs_append_text(text, pos, size, "[");
    while (node != 0 && pos + 128u < size && keyboard_event_queue_get_after(&node->aux_data, &rec)) {
        if (!first) {
            pos = vfs_append_text(text, pos, size, ",");
        }
        first = 0;
        pos = vfs_append_text(text, pos, size, "{\"type\":\"input.keyboard\",\"seq\":");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, ",\"tick\":");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, ",\"state\":");
        pos = vfs_append_json_string(text, pos, size, rec.event.pressed ? "press" : (rec.event.released ? "release" : "state"));
        pos = vfs_append_text(text, pos, size, ",\"key\":");
        {
            char key_text[12];
            uint32_t key_pos = vfs_append_keyboard_ascii_text(key_text, 0, sizeof(key_text), rec.event.ascii);
            (void)key_pos;
            pos = vfs_append_json_string(text, pos, size, key_text);
        }
        pos = vfs_append_text(text, pos, size, "}");
    }
    pos = vfs_append_text(text, pos, size, "]\n");
    return pos;
}

static uint32_t vfs_format_file_events_json(struct vfs_node *node, char *text, uint32_t size) {
    struct vfs_file_event_record rec;
    uint32_t pos = 0;
    int first = 1;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = g_file_event_seq;
    }
    pos = vfs_append_text(text, pos, size, "[");
    while (node != 0 && pos + 160u < size && vfs_file_event_get_after(&node->aux_data, &rec)) {
        if (!first) {
            pos = vfs_append_text(text, pos, size, ",");
        }
        first = 0;
        pos = vfs_append_text(text, pos, size, "{\"type\":\"file.change\",\"seq\":");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, ",\"tick\":");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, ",\"op\":");
        pos = vfs_append_json_string(text, pos, size, rec.op);
        pos = vfs_append_text(text, pos, size, ",\"path\":");
        pos = vfs_append_json_string(text, pos, size, rec.path);
        pos = vfs_append_text(text, pos, size, ",\"bytes\":");
        pos = vfs_append_u32_text(text, pos, size, rec.bytes);
        pos = vfs_append_text(text, pos, size, "}");
    }
    pos = vfs_append_text(text, pos, size, "]\n");
    return pos;
}

static uint32_t vfs_format_net_status_event_json(struct vfs_node *node, char *text, uint32_t size) {
    struct net_status_event_record rec;
    uint32_t pos = 0;
    int first = 1;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = net_event_status_latest_seq();
    }
    pos = vfs_append_text(text, pos, size, "[");
    while (node != 0 && pos + 128u < size && net_event_status_get_after(&node->aux_data, &rec)) {
        if (!first) {
            pos = vfs_append_text(text, pos, size, ",");
        }
        first = 0;
        pos = vfs_append_text(text, pos, size, "{\"type\":\"net.status\",\"seq\":");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, ",\"tick\":");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, ",\"dev\":\"rtl8139\",\"state\":");
        pos = vfs_append_json_string(text, pos, size, rec.link_up ? "up" : "down");
        pos = vfs_append_text(text, pos, size, ",\"speed\":");
        pos = vfs_append_u32_text(text, pos, size, rec.speed_mbps);
        pos = vfs_append_text(text, pos, size, "}");
    }
    pos = vfs_append_text(text, pos, size, "]\n");
    return pos;
}

static uint32_t vfs_format_mouse_events(struct vfs_node *node, char *text, uint32_t size) {
    struct mouse_event_record rec;
    uint32_t pos = 0;
    uint32_t emitted = 0;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = mouse_event_latest_seq();
        return 0;
    }
    while (node != 0 && pos + 96u < size && mouse_event_get_after(&node->aux_data, &rec)) {
        pos = vfs_append_text(text, pos, size, "event input.mouse seq=");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, " tick=");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, " dx=");
        pos = vfs_append_i32_text(text, pos, size, rec.dx);
        pos = vfs_append_text(text, pos, size, " dy=");
        pos = vfs_append_i32_text(text, pos, size, rec.dy);
        pos = vfs_append_text(text, pos, size, " buttons=");
        pos = vfs_append_u32_text(text, pos, size, rec.buttons);
        pos = vfs_append_text(text, pos, size, "\n");
        emitted++;
    }
    if (emitted == 0) {
        pos = vfs_append_text(text, pos, size, "type input.mouse\nstatus empty\npending ");
        pos = vfs_append_u32_text(text, pos, size, mouse_event_pending());
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, mouse_event_dropped());
        pos = vfs_append_text(text, pos, size, "\nsource /event/input/mouse\n");
    }
    return pos;
}

static uint32_t vfs_format_mouse_events_json(struct vfs_node *node, char *text, uint32_t size) {
    struct mouse_event_record rec;
    uint32_t pos = 0;
    int first = 1;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = mouse_event_latest_seq();
    }
    pos = vfs_append_text(text, pos, size, "[");
    while (node != 0 && pos + 128u < size && mouse_event_get_after(&node->aux_data, &rec)) {
        if (!first) {
            pos = vfs_append_text(text, pos, size, ",");
        }
        first = 0;
        pos = vfs_append_text(text, pos, size, "{\"type\":\"input.mouse\",\"seq\":");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, ",\"tick\":");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, ",\"dx\":");
        pos = vfs_append_i32_text(text, pos, size, rec.dx);
        pos = vfs_append_text(text, pos, size, ",\"dy\":");
        pos = vfs_append_i32_text(text, pos, size, rec.dy);
        pos = vfs_append_text(text, pos, size, ",\"buttons\":");
        pos = vfs_append_u32_text(text, pos, size, rec.buttons);
        pos = vfs_append_text(text, pos, size, "}");
    }
    pos = vfs_append_text(text, pos, size, "]\n");
    return pos;
}

static uint32_t vfs_format_block_events(struct vfs_node *node, char *text, uint32_t size) {
    struct block_change_event_record rec;
    uint32_t pos = 0;
    uint32_t emitted = 0;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = block_event_change_latest_seq();
        return 0;
    }
    while (node != 0 && pos + 120u < size && block_event_change_get_after(&node->aux_data, &rec)) {
        pos = vfs_append_text(text, pos, size, "event block.change seq=");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, " tick=");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, " op=");
        pos = vfs_append_text(text, pos, size, rec.op);
        pos = vfs_append_text(text, pos, size, " disk=");
        pos = vfs_append_u32_text(text, pos, size, rec.disk);
        pos = vfs_append_text(text, pos, size, " part=");
        pos = rec.part == 0xffffffffu ? vfs_append_text(text, pos, size, "none") : vfs_append_u32_text(text, pos, size, rec.part);
        pos = vfs_append_text(text, pos, size, " name=");
        pos = vfs_append_text(text, pos, size, rec.name);
        pos = vfs_append_text(text, pos, size, "\n");
        emitted++;
    }
    if (emitted == 0) {
        pos = vfs_append_text(text, pos, size, "type block.change\nstatus empty\npending ");
        pos = vfs_append_u32_text(text, pos, size, block_event_change_pending());
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, block_event_change_dropped());
        pos = vfs_append_text(text, pos, size, "\nsource /event/block/change\n");
    }
    return pos;
}

static uint32_t vfs_format_block_events_json(struct vfs_node *node, char *text, uint32_t size) {
    struct block_change_event_record rec;
    uint32_t pos = 0;
    int first = 1;

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = block_event_change_latest_seq();
    }
    pos = vfs_append_text(text, pos, size, "[");
    while (node != 0 && pos + 160u < size && block_event_change_get_after(&node->aux_data, &rec)) {
        if (!first) {
            pos = vfs_append_text(text, pos, size, ",");
        }
        first = 0;
        pos = vfs_append_text(text, pos, size, "{\"type\":\"block.change\",\"seq\":");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, ",\"op\":");
        pos = vfs_append_json_string(text, pos, size, rec.op);
        pos = vfs_append_text(text, pos, size, ",\"disk\":");
        pos = vfs_append_u32_text(text, pos, size, rec.disk);
        pos = vfs_append_text(text, pos, size, ",\"name\":");
        pos = vfs_append_json_string(text, pos, size, rec.name);
        pos = vfs_append_text(text, pos, size, "}");
    }
    pos = vfs_append_text(text, pos, size, "]\n");
    return pos;
}

static uint32_t vfs_format_net_status_event(struct vfs_node *node, char *text, uint32_t size) {
    struct net_status_event_record rec;
    uint32_t pos = 0;
    uint32_t emitted = 0;
    uint32_t dropped = net_event_status_dropped();

    if (node != 0 && node->aux_data == 0u) {
        node->aux_data = net_event_status_latest_seq();
        return 0;
    }
    while (node != 0 && pos + 96u < size && net_event_status_get_after(&node->aux_data, &rec)) {
        pos = vfs_append_text(text, pos, size, "event net.status seq=");
        pos = vfs_append_u32_text(text, pos, size, rec.seq);
        pos = vfs_append_text(text, pos, size, " tick=");
        pos = vfs_append_u32_text(text, pos, size, rec.tick);
        pos = vfs_append_text(text, pos, size, " dev=rtl8139 present=");
        pos = vfs_append_bool_text(text, pos, size, rec.present);
        pos = vfs_append_text(text, pos, size, " initialized=");
        pos = vfs_append_bool_text(text, pos, size, rec.initialized);
        pos = vfs_append_text(text, pos, size, " state=");
        pos = vfs_append_text(text, pos, size, rec.link_up ? "up" : "down");
        pos = vfs_append_text(text, pos, size, " speed=");
        pos = vfs_append_u32_text(text, pos, size, rec.speed_mbps);
        pos = vfs_append_text(text, pos, size, "\n");
        emitted++;
    }
    if (emitted == 0) {
        pos = vfs_append_text(text, pos, size, "type net.status\nstatus empty\npending ");
        pos = vfs_append_u32_text(text, pos, size, net_event_status_pending());
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, dropped);
        pos = vfs_append_text(text, pos, size, "\nsource /event/net/status\n");
    } else {
        pos = vfs_append_text(text, pos, size, "pending ");
        pos = vfs_append_u32_text(text, pos, size, net_event_status_pending());
        pos = vfs_append_text(text, pos, size, "\ndropped ");
        pos = vfs_append_u32_text(text, pos, size, dropped);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_eventfs(struct vfs_node *node, char *text, uint32_t size) {
    uint32_t pos = 0;

    if (node == 0) {
        return 0;
    }
    if (node->aux_index == VFS_EVENT_TIMER) {
        pos = vfs_append_text(text, pos, size, "event timer.tick tick=");
        pos = vfs_append_u32_text(text, pos, size, sched_current_ticks());
        pos = vfs_append_text(text, pos, size, "\n");
    } else if (node->aux_index == VFS_EVENT_TIMER_JSON) {
        pos = vfs_append_text(text, pos, size, "[{\"type\":\"timer.tick\",\"tick\":");
        pos = vfs_append_u32_text(text, pos, size, sched_current_ticks());
        pos = vfs_append_text(text, pos, size, "}]\n");
    } else if (node->aux_index == VFS_EVENT_INPUT_KEYBOARD) {
        pos = vfs_format_keyboard_events(node, text, size);
    } else if (node->aux_index == VFS_EVENT_INPUT_KEYBOARD_JSON) {
        pos = vfs_format_keyboard_events_json(node, text, size);
    } else if (node->aux_index == VFS_EVENT_INPUT_MOUSE) {
        pos = vfs_format_mouse_events(node, text, size);
    } else if (node->aux_index == VFS_EVENT_INPUT_MOUSE_JSON) {
        pos = vfs_format_mouse_events_json(node, text, size);
    } else if (node->aux_index == VFS_EVENT_NET_STATUS) {
        pos = vfs_format_net_status_event(node, text, size);
    } else if (node->aux_index == VFS_EVENT_NET_STATUS_JSON) {
        pos = vfs_format_net_status_event_json(node, text, size);
    } else if (node->aux_index == VFS_EVENT_FILE_CHANGE) {
        pos = vfs_format_file_events(node, text, size);
    } else if (node->aux_index == VFS_EVENT_FILE_CHANGE_JSON) {
        pos = vfs_format_file_events_json(node, text, size);
    } else if (node->aux_index == VFS_EVENT_BLOCK_CHANGE) {
        pos = vfs_format_block_events(node, text, size);
    } else if (node->aux_index == VFS_EVENT_BLOCK_CHANGE_JSON) {
        pos = vfs_format_block_events_json(node, text, size);
    }
    return pos;
}

static int64_t vfs_read_from_procfs(struct vfs *vfs,
                                    struct vfs_node *node,
                                    uint32_t *offset_io,
                                    void *buffer,
                                    uint32_t size) {
    static char proc_text[4096];
    uint32_t text_size = 0;

    if (node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    if (node->aux_index == VFS_PROC_KMSG) {
        uint32_t copied = kprint_log_read(*offset_io, (char *)buffer, size);

        *offset_io += copied;
        return (int64_t)copied;
    }
    proc_text[0] = '\0';
    if (node->aux_index == VFS_PROC_MEMINFO) {
        text_size = vfs_format_proc_meminfo(proc_text, sizeof(proc_text));
    } else if (node->aux_index == VFS_PROC_MOUNTS) {
        text_size = vfs_format_proc_mounts(vfs, proc_text, sizeof(proc_text));
    } else if (node->aux_index == VFS_PROC_UPTIME) {
        text_size = vfs_format_proc_uptime(proc_text, sizeof(proc_text));
    } else if (node->aux_index == VFS_PROC_RTC) {
        text_size = vfs_format_proc_rtc(proc_text, sizeof(proc_text));
    } else if (node->aux_index == VFS_PROC_ACTIONS) {
        text_size = vfs_format_proc_actions(proc_text, sizeof(proc_text));
    } else if (node->aux_index == VFS_PROC_PID_STATUS) {
        text_size = vfs_format_proc_pid_status(node->aux_data, proc_text, sizeof(proc_text));
    } else {
        return -1;
    }
    return vfs_read_from_generated_text(offset_io, buffer, size, proc_text, text_size);
}

static int64_t vfs_read_from_eventfs(struct vfs_node *node,
                                     uint32_t *offset_io,
                                     void *buffer,
                                     uint32_t size) {
    static char event_text[2048];
    static uint32_t event_text_size;
    static uint32_t event_text_node;

    if (node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    if (*offset_io == 0 || *offset_io >= event_text_size || event_text_node != node->aux_index) {
        *offset_io = 0;
        event_text[0] = '\0';
        event_text_size = vfs_format_eventfs(node, event_text, sizeof(event_text));
        event_text_node = node->aux_index;
    }
    if (event_text_size == 0) {
        return 0;
    }
    return vfs_read_from_generated_text(offset_io, buffer, size, event_text, event_text_size);
}

int64_t vfs_write_to_fat32(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           const void *buffer,
                           uint32_t size) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    uint32_t bytes_written = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_write_file_range(fat32, &node->handle.fat32_file, *offset_io, buffer, size, &bytes_written) != 0) {
        return -1;
    }
    *offset_io += bytes_written;
    return (int64_t)bytes_written;
}

int64_t vfs_write_to_nxfs(struct vfs *vfs,
                          struct vfs_node *node,
                          uint32_t *offset_io,
                          const void *buffer,
                          uint32_t size) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    uint32_t bytes_written = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_write_file_range(nxfs,
                              node->aux_index,
                              &node->handle.nxfs_inode,
                              *offset_io,
                              buffer,
                              size,
                              &bytes_written) != 0) {
        return -1;
    }
    *offset_io += bytes_written;
    return (int64_t)bytes_written;
}

static int64_t vfs_write_to_devfs(struct vfs_node *node,
                                  uint32_t *offset_io,
                                  const void *buffer,
                                  uint32_t size) {
    uint64_t base_lba;
    uint64_t block_count;
    struct block_device *dev = vfs_blockdev_from_node(node, &base_lba, &block_count);

    if (node->aux_index == VFS_DEV_TTY ||
        node->aux_index == VFS_DEV_STDOUT ||
        node->aux_index == VFS_DEV_STDERR) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_STDIN) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_NULL || node->aux_index == VFS_DEV_ZERO) {
        *offset_io += size;
        return (int64_t)size;
    }
    if (dev == 0) {
        return -1;
    }
    return vfs_blockdev_write_bytes(dev, base_lba, block_count, offset_io, buffer, size);
}

int vfs_prepare_fat32_opened_node(struct vfs *vfs,
                                  struct vfs_node *node,
                                  const char *path,
                                  uint32_t flags,
                                  uint32_t *offset_out) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;

    (void)path;
    if (vfs == 0 || node == 0 || offset_out == 0) {
        return -1;
    }
    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if ((flags & SYS_OPEN_TRUNC) != 0 &&
        node->handle.fat32_file.size != 0u &&
        fat32_truncate_file(fat32, &node->handle.fat32_file) != 0) {
        return -1;
    }
    if ((flags & SYS_OPEN_TRUNC) != 0) {
        vfs_event_file_change_emit("truncate",
                                   path,
                                   VFS_MOUNT_FAT32,
                                   node->mount_slot,
                                   vfs_node_native_id(node),
                                   0);
    }
    *offset_out = ((flags & SYS_OPEN_APPEND) != 0) ? node->handle.fat32_file.size : 0u;
    return 0;
}

int vfs_prepare_nxfs_opened_node(struct vfs *vfs,
                                 struct vfs_node *node,
                                 const char *path,
                                 uint32_t flags,
                                 uint32_t *offset_out) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (vfs == 0 || node == 0 || path == 0 || offset_out == 0) {
        return -1;
    }
    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if ((flags & SYS_OPEN_TRUNC) != 0 &&
        node->handle.nxfs_inode.size != 0u) {
        if (nxfs_truncate_inode(nxfs, node->aux_index, &node->handle.nxfs_inode) != 0) {
            return -1;
        }
    }
    if ((flags & SYS_OPEN_TRUNC) != 0) {
        vfs_event_file_change_emit("truncate",
                                   path,
                                   VFS_MOUNT_NXFS,
                                   node->mount_slot,
                                   vfs_node_native_id(node),
                                   0);
    }
    *offset_out = ((flags & SYS_OPEN_APPEND) != 0) ? node->handle.nxfs_inode.size : 0u;
    return 0;
}

static int64_t vfs_readdir_root(struct vfs *vfs, uint32_t *index_io, struct vfs_dirent *entry) {
    while (*index_io < (5u + vfs_mount_count(vfs))) {
        uint32_t current = *index_io;
        struct vfs_builtin_mount_info builtin;

        if (current < 2u && vfs_get_builtin_mount_info(vfs, current, &builtin)) {
            return vfs_emit_dir_entry(entry, index_io, builtin.name, 0, VFS_ATTR_DIR);
        }
        if (current == 2u) {
            return vfs_emit_dir_entry(entry, index_io, "dev", 0, VFS_ATTR_DIR);
        }
        if (current == 3u) {
            return vfs_emit_dir_entry(entry, index_io, "proc", 0, VFS_ATTR_DIR);
        }
        if (current == 4u) {
            return vfs_emit_dir_entry(entry, index_io, "event", 0, VFS_ATTR_DIR);
        }
        if (current >= 5u) {
            struct vfs_mount_info info;

            if (vfs_get_mount(vfs, current - 5u, &info) != 0) {
                return 0;
            }
            return vfs_emit_dir_entry(entry, index_io, info.name, 0, VFS_ATTR_DIR);
        }
        (*index_io)++;
    }
    return 0;
}

uint32_t vfs_node_file_size(const struct vfs_node *node) {
    if (node == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_FAT32) {
        return node->handle.fat32_file.size;
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        return node->handle.nxfs_inode.size;
    }
    return 0;
}

int64_t vfs_read_dir_fat32(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *index_io,
                           struct vfs_dirent *entry) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    struct fat32_file fat32_file;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return 0;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_get_dir_entry(fat32, &node->handle.fat32_file, *index_io, &fat32_file) == 0) {
        return vfs_emit_dir_entry(entry, index_io, fat32_file.name, fat32_file.size, fat32_file.attributes);
    }
    if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) == 0) {
        return 0;
    }
    if ((node->aux_data & VFS_NODE_FLAG_ROOT_DEV_EMITTED) == 0) {
        node->aux_data |= VFS_NODE_FLAG_ROOT_DEV_EMITTED;
        return vfs_emit_dir_entry(entry, index_io, "dev", 0, VFS_ATTR_DIR);
    }
    if ((node->aux_data & VFS_NODE_FLAG_ROOT_PROC_EMITTED) == 0) {
        node->aux_data |= VFS_NODE_FLAG_ROOT_PROC_EMITTED;
        return vfs_emit_dir_entry(entry, index_io, "proc", 0, VFS_ATTR_DIR);
    }
    if ((node->aux_data & VFS_NODE_FLAG_ROOT_EVENT_EMITTED) == 0) {
        node->aux_data |= VFS_NODE_FLAG_ROOT_EVENT_EMITTED;
        return vfs_emit_dir_entry(entry, index_io, "event", 0, VFS_ATTR_DIR);
    }
    if (*index_io < (3u + vfs_mount_count(vfs))) {
        struct vfs_mount_info info;

        if (vfs_get_mount(vfs, *index_io - 3u, &info) != 0) {
            return 0;
        }
        return vfs_emit_dir_entry(entry, index_io, info.name, 0, VFS_ATTR_DIR);
    }
    return 0;
}

int64_t vfs_read_dir_nxfs(struct vfs *vfs,
                          struct vfs_node *node,
                          uint32_t *index_io,
                          struct vfs_dirent *entry) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    struct nxfs_dir_entry nxfs_entry;
    struct nxfs_inode nxfs_inode;
    uint32_t count = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_list_dir(nxfs, node->aux_index, &node->handle.nxfs_inode, &nxfs_entry, 1, &count) != 0) {
        return -1;
    }
        if (*index_io >= count) {
            if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) != 0 &&
                (node->aux_data & VFS_NODE_FLAG_ROOT_DEV_EMITTED) == 0) {
                node->aux_data |= VFS_NODE_FLAG_ROOT_DEV_EMITTED;
                return vfs_emit_dir_entry(entry, index_io, "dev", 0, VFS_ATTR_DIR);
            }
        if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) != 0 &&
            (node->aux_data & VFS_NODE_FLAG_ROOT_PROC_EMITTED) == 0) {
            node->aux_data |= VFS_NODE_FLAG_ROOT_PROC_EMITTED;
            return vfs_emit_dir_entry(entry, index_io, "proc", 0, VFS_ATTR_DIR);
        }
        if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) != 0 &&
            (node->aux_data & VFS_NODE_FLAG_ROOT_EVENT_EMITTED) == 0) {
            node->aux_data |= VFS_NODE_FLAG_ROOT_EVENT_EMITTED;
            return vfs_emit_dir_entry(entry, index_io, "event", 0, VFS_ATTR_DIR);
        }
        if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) != 0 && *index_io < (count + 3u + vfs_mount_count(vfs))) {
            struct vfs_mount_info info;

            if (vfs_get_mount(vfs, *index_io - count - 3u, &info) != 0) {
                return 0;
            }
            return vfs_emit_dir_entry(entry, index_io, info.name, 0, VFS_ATTR_DIR);
        }
        return 0;
    }
    if (nxfs_get_dir_entry(nxfs, node->aux_index, &node->handle.nxfs_inode, *index_io, &nxfs_entry) != 0) {
        return 0;
    }
    vfs_emit_dir_entry(entry, 0, nxfs_entry.name, 0, 0);
    if (nxfs_read_inode(nxfs, nxfs_entry.inode, &nxfs_inode) == 0) {
        entry->size = nxfs_inode.size;
        if (nxfs_inode.type == NXFS_TYPE_DIR) {
            entry->attributes = VFS_ATTR_DIR;
        }
    }
    (*index_io)++;
    return 1;
}

static int64_t vfs_read_dir_devfs(uint32_t *index_io, struct vfs_dirent *entry) {
    if (*index_io == 0) {
        return vfs_emit_dir_entry(entry, index_io, "tty", 0, 0);
    } else if (*index_io == 1) {
        return vfs_emit_dir_entry(entry, index_io, "null", 0, 0);
    } else if (*index_io == 2) {
        return vfs_emit_dir_entry(entry, index_io, "zero", 0, 0);
    } else if (*index_io == 3) {
        return vfs_emit_dir_entry(entry, index_io, "stdin", 0, 0);
    } else if (*index_io == 4) {
        return vfs_emit_dir_entry(entry, index_io, "stdout", 0, 0);
    } else if (*index_io == 5) {
        return vfs_emit_dir_entry(entry, index_io, "stderr", 0, 0);
    } else {
        uint32_t ordinal = *index_io - 6u;
        uint32_t seen = 0;

        for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
            struct block_device *dev = blockdev_get(disk_index);

            if (dev == 0) {
                continue;
            }
            if (seen == ordinal) {
                return vfs_emit_devfs_block_entry(entry, index_io, disk_index, VFS_PARTITION_RAW, dev);
            }
            seen++;
            for (uint32_t part_index = 0; part_index < blockdev_partition_count(dev); part_index++) {
                if (seen == ordinal) {
                    return vfs_emit_devfs_block_entry(entry, index_io, disk_index, part_index, dev);
                }
                seen++;
            }
        }
        return 0;
    }
}

static int64_t vfs_read_dir_procfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry) {
    if (node->aux_index == VFS_PROC_PID_DIR) {
        if (*index_io == 0) {
            return vfs_emit_dir_entry(entry, index_io, "status", 0, 0);
        }
        return 0;
    }
    if (*index_io == 0) {
        return vfs_emit_dir_entry(entry, index_io, "meminfo", 0, 0);
    }
    if (*index_io == 1) {
        return vfs_emit_dir_entry(entry, index_io, "mounts", 0, 0);
    }
    if (*index_io == 2) {
        return vfs_emit_dir_entry(entry, index_io, "uptime", 0, 0);
    }
    if (*index_io == 3) {
        return vfs_emit_dir_entry(entry, index_io, "kmsg", 0, 0);
    }
    if (*index_io == 4) {
        return vfs_emit_dir_entry(entry, index_io, "actions", 0, 0);
    }
    if (*index_io == 5) {
        return vfs_emit_dir_entry(entry, index_io, "rtc", 0, 0);
    }
    {
        uint32_t ordinal = *index_io - 6u;
        uint32_t seen = 0;

        for (uint32_t i = 0; i < process_capacity(); i++) {
            struct process_snapshot proc;
            char name[16];

            if (!process_get(i, &proc)) {
                continue;
            }
            if (seen == ordinal) {
                name[0] = '\0';
                vfs_append_u32_text(name, 0, sizeof(name), proc.pid);
                return vfs_emit_dir_entry(entry, index_io, name, 0, VFS_ATTR_DIR);
            }
            seen++;
        }
    }
    return 0;
}

static int64_t vfs_read_dir_eventfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry) {
    if (node->aux_index == VFS_EVENT_ROOT) {
        if (*index_io == 0) {
            return vfs_emit_dir_entry(entry, index_io, "timer", 0, 0);
        }
        if (*index_io == 1) {
            return vfs_emit_dir_entry(entry, index_io, "input", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 2) {
            return vfs_emit_dir_entry(entry, index_io, "net", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 3) {
            return vfs_emit_dir_entry(entry, index_io, "file", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 4) {
            return vfs_emit_dir_entry(entry, index_io, "block", 0, VFS_ATTR_DIR);
        }
        return 0;
    }
    if (node->aux_index == VFS_EVENT_INPUT_DIR) {
        if (*index_io == 0) {
            return vfs_emit_dir_entry(entry, index_io, "keyboard", 0, 0);
        }
        if (*index_io == 1) {
            return vfs_emit_dir_entry(entry, index_io, "mouse", 0, 0);
        }
        return 0;
    }
    if (node->aux_index == VFS_EVENT_NET_DIR) {
        return *index_io == 0 ? vfs_emit_dir_entry(entry, index_io, "status", 0, 0) : 0;
    }
    if (node->aux_index == VFS_EVENT_FILE_DIR) {
        return *index_io == 0 ? vfs_emit_dir_entry(entry, index_io, "change", 0, 0) : 0;
    }
    if (node->aux_index == VFS_EVENT_BLOCK_DIR) {
        return *index_io == 0 ? vfs_emit_dir_entry(entry, index_io, "change", 0, 0) : 0;
    }
    return 0;
}

struct block_device *vfs_blockdev_from_node(const struct vfs_node *node,
                                            uint64_t *base_lba_out,
                                            uint64_t *block_count_out) {
    struct block_device *dev;

    if (base_lba_out != 0) {
        *base_lba_out = 0;
    }
    if (block_count_out != 0) {
        *block_count_out = 0;
    }
    if (node == 0 || node->mount_kind != VFS_MOUNT_DEVFS) {
        return 0;
    }
    if (node->aux_index == VFS_DEV_BLOCK_DEVICE) {
        dev = blockdev_get(node->aux_data);
        if (dev != 0 && block_count_out != 0) {
            *block_count_out = dev->block_count;
        }
        return dev;
    }
    if (node->aux_index == VFS_DEV_BLOCK_PARTITION) {
        uint32_t disk_index = node->aux_data >> 16;
        uint32_t part_index = node->aux_data & 0xffffu;
        struct blockdev_partition part;

        dev = blockdev_get(disk_index);
        if (dev == 0 || blockdev_partition_get(dev, part_index, &part) != 0) {
            return 0;
        }
        if (base_lba_out != 0) {
            *base_lba_out = part.start_lba;
        }
        if (block_count_out != 0) {
            *block_count_out = part.sector_count;
        }
        return dev;
    }
    return 0;
}

static int64_t vfs_blockdev_transfer_bytes(struct block_device *dev,
                                           uint64_t base_lba,
                                           uint64_t block_count,
                                           uint32_t *offset_io,
                                           void *buffer,
                                           uint32_t size,
                                           uint32_t write_mode) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t total = 0;

    if (dev == 0 || offset_io == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (dev->block_size == 0 || dev->block_size > VFS_DEV_BLOCK_BUFFER_SIZE ||
        (write_mode != 0 && dev->write == 0)) {
        return -1;
    }
    while (total < size) {
        uint64_t byte_offset = (uint64_t)(*offset_io) + total;
        uint64_t lba = byte_offset / dev->block_size;
        uint32_t block_off = (uint32_t)(byte_offset % dev->block_size);
        uint32_t chunk = dev->block_size - block_off;

        if (lba >= block_count) {
            break;
        }
        if (chunk > size - total) {
            chunk = size - total;
        }
        if ((write_mode == 0 || block_off != 0 || chunk != dev->block_size) &&
            blockdev_read(dev, base_lba + lba, 1, g_vfs_block_buffer) != 0) {
            return total != 0 ? (int64_t)total : -1;
        }
        if (write_mode != 0) {
            vfs_copy_bytes(g_vfs_block_buffer + block_off, bytes + total, chunk);
            if (blockdev_write(dev, base_lba + lba, 1, g_vfs_block_buffer) != 0) {
                return total != 0 ? (int64_t)total : -1;
            }
        } else {
            vfs_copy_bytes(bytes + total, g_vfs_block_buffer + block_off, chunk);
        }
        total += chunk;
    }

    *offset_io += total;
    return (int64_t)total;
}

int64_t vfs_blockdev_read_bytes(struct block_device *dev,
                                uint64_t base_lba,
                                uint64_t block_count,
                                uint32_t *offset_io,
                                void *buffer,
                                uint32_t size) {
    return vfs_blockdev_transfer_bytes(dev, base_lba, block_count, offset_io, buffer, size, 0);
}

int64_t vfs_blockdev_write_bytes(struct block_device *dev,
                                 uint64_t base_lba,
                                 uint64_t block_count,
                                 uint32_t *offset_io,
                                 const void *buffer,
                                 uint32_t size) {
    return vfs_blockdev_transfer_bytes(dev, base_lba, block_count, offset_io, (void *)buffer, size, 1);
}

int vfs_devfs_lookup(const char *name, struct vfs_node *out) {
    uint32_t index = 0;

    if (name == 0 || out == 0) {
        return -1;
    }
    if (streq(name, "tty")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_TTY);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_TTY, 0u);
        return 0;
    }
    if (streq(name, "null")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_NULL);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_MISC, 3u);
        return 0;
    }
    if (streq(name, "zero")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_ZERO);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_MISC, 5u);
        return 0;
    }
    if (streq(name, "stdin")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_STDIN);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_MISC, 0u);
        return 0;
    }
    if (streq(name, "stdout")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_STDOUT);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_MISC, 1u);
        return 0;
    }
    if (streq(name, "stderr")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_STDERR);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_MISC, 2u);
        return 0;
    }
    if (starts_with(name, "disk")) {
        const char *digits = name + 4;
        uint32_t part_index = 0;

        if (*digits == '\0') {
            return -1;
        }
        while (vfs_is_decimal_digit(*digits)) {
            index = index * 10u + (uint32_t)(*digits - '0');
            digits++;
        }
        if (*digits == '\0') {
            if (blockdev_get(index) == 0) {
                return -1;
            }
            vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_BLOCK_DEVICE);
            vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_BLOCK, index * 16u);
            out->aux_data = index;
            return 0;
        }
        if (*digits != 'p' || blockdev_get(index) == 0) {
            return -1;
        }
        digits++;
        if (*digits == '\0') {
            return -1;
        }
        while (vfs_is_decimal_digit(*digits)) {
            part_index = part_index * 10u + (uint32_t)(*digits - '0');
            digits++;
        }
        if (*digits != '\0') {
            return -1;
        }
        if (part_index == 0) {
            return -1;
        }
        part_index--;
        {
            struct blockdev_partition part;

            if (blockdev_partition_get(blockdev_get(index), part_index, &part) != 0) {
                return -1;
            }
            vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_BLOCK_PARTITION);
            vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_BLOCK, index * 16u + part.index + 1u);
        }
        out->aux_data = (index << 16) | (part_index & 0xffffu);
        return 0;
    }
    return -1;
}

int vfs_procfs_lookup(const char *name, struct vfs_node *out) {
    const char *slash;
    char pid_text[16];
    uint32_t pid_len = 0;
    uint32_t pid = 0;

    if (name == 0 || out == 0) {
        return -1;
    }
    if (streq(name, "meminfo")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_MEMINFO, 0);
        return 0;
    }
    if (streq(name, "mounts")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_MOUNTS, 0);
        return 0;
    }
    if (streq(name, "uptime")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_UPTIME, 0);
        return 0;
    }
    if (streq(name, "kmsg")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_KMSG, 0);
        return 0;
    }
    if (streq(name, "actions")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_ACTIONS, 0);
        return 0;
    }
    if (streq(name, "rtc")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_RTC, 0);
        return 0;
    }
    slash = name;
    while (*slash != '\0' && *slash != '/') {
        if (pid_len + 1u >= sizeof(pid_text)) {
            return -1;
        }
        pid_text[pid_len++] = *slash++;
    }
    pid_text[pid_len] = '\0';
    if (*slash != '/' || !vfs_parse_u32_full(pid_text, &pid) || !vfs_process_get_pid(pid, 0)) {
        return -1;
    }
    slash++;
    if (streq(slash, "status")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_PID_STATUS, pid);
        return 0;
    }
    return -1;
}

int vfs_procfs_opendir(const char *name, struct vfs_node *out) {
    uint32_t pid = 0;

    if (name == 0 || out == 0) {
        return -1;
    }
    if (!vfs_parse_u32_full(name, &pid) || !vfs_process_get_pid(pid, 0)) {
        return -1;
    }
    vfs_set_procfs_node(out, VFS_NODE_DIR, VFS_PROC_PID_DIR, pid);
    return 0;
}

int vfs_eventfs_lookup(const char *name, struct vfs_node *out) {
    if (name == 0 || out == 0) {
        return -1;
    }
    if (streq(name, "timer")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_TIMER);
        return 0;
    }
    if (streq(name, "timer.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_TIMER_JSON);
        return 0;
    }
    if (streq(name, "input/keyboard")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_INPUT_KEYBOARD);
        return 0;
    }
    if (streq(name, "input/keyboard.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_INPUT_KEYBOARD_JSON);
        return 0;
    }
    if (streq(name, "input/mouse")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_INPUT_MOUSE);
        return 0;
    }
    if (streq(name, "input/mouse.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_INPUT_MOUSE_JSON);
        return 0;
    }
    if (streq(name, "net/status")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_NET_STATUS);
        return 0;
    }
    if (streq(name, "net/status.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_NET_STATUS_JSON);
        return 0;
    }
    if (streq(name, "file/change")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_FILE_CHANGE);
        return 0;
    }
    if (streq(name, "file/change.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_FILE_CHANGE_JSON);
        return 0;
    }
    if (streq(name, "block/change")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_BLOCK_CHANGE);
        return 0;
    }
    if (streq(name, "block/change.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_BLOCK_CHANGE_JSON);
        return 0;
    }
    return -1;
}

int vfs_eventfs_opendir(const char *name, struct vfs_node *out) {
    if (name == 0 || out == 0) {
        return -1;
    }
    if (streq(name, "input")) {
        vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_INPUT_DIR);
        return 0;
    }
    if (streq(name, "net")) {
        vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_NET_DIR);
        return 0;
    }
    if (streq(name, "file")) {
        vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_FILE_DIR);
        return 0;
    }
    if (streq(name, "block")) {
        vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_BLOCK_DIR);
        return 0;
    }
    return -1;
}

static int vfs_can_access_file_node(const struct vfs *vfs, const struct vfs_node *node) {
    if (vfs == 0 || node == 0 || node->kind != VFS_NODE_FILE) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS ||
        node->mount_kind == VFS_MOUNT_PROCFS ||
        node->mount_kind == VFS_MOUNT_EVENTFS) {
        return 1;
    }
    return vfs_mount_ready(vfs, node->mount_kind);
}

static int vfs_can_access_dir_node(const struct vfs *vfs, const struct vfs_node *node) {
    if (vfs == 0 || node == 0 || node->kind != VFS_NODE_DIR) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_ROOT ||
        node->mount_kind == VFS_MOUNT_DEVFS ||
        node->mount_kind == VFS_MOUNT_PROCFS ||
        node->mount_kind == VFS_MOUNT_EVENTFS) {
        return 1;
    }
    return vfs_mount_ready(vfs, node->mount_kind);
}

int64_t vfs_read(struct vfs *vfs,
                 struct vfs_node *node,
                 uint32_t *offset_io,
                 void *buffer,
                 uint32_t size,
                 uint32_t flags) {
    const struct vfs_mount_ops *ops;

    if (!vfs_can_access_file_node(vfs, node) || offset_io == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_read_from_devfs(node, offset_io, buffer, size, flags);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS) {
        (void)flags;
        return vfs_read_from_procfs(vfs, node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_EVENTFS) {
        (void)flags;
        return vfs_read_from_eventfs(node, offset_io, buffer, size);
    }
    ops = vfs_mount_ops(node->mount_kind);
    return ops != 0 && ops->read_file != 0 ? ops->read_file(vfs, node, offset_io, buffer, size) : -1;
}

int64_t vfs_write(struct vfs *vfs,
                  struct vfs_node *node,
                  uint32_t *offset_io,
                  const void *buffer,
                  uint32_t size,
                  const char *opened_path) {
    const struct vfs_mount_ops *ops;
    int64_t written;

    if (!vfs_can_access_file_node(vfs, node) || offset_io == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_write_to_devfs(node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS || node->mount_kind == VFS_MOUNT_EVENTFS) {
        return -1;
    }
    ops = vfs_mount_ops(node->mount_kind);
    written = ops != 0 && ops->write_file != 0 ? ops->write_file(vfs, node, offset_io, buffer, size) : -1;
    if (written > 0) {
        vfs_event_file_change_emit("write",
                                   opened_path,
                                   node->mount_kind,
                                   node->mount_slot,
                                   vfs_node_native_id(node),
                                   (uint32_t)written);
    }
    return written;
}

int64_t vfs_readdir(struct vfs *vfs,
                    struct vfs_node *node,
                    uint32_t *index_io,
                    struct vfs_dirent *entry) {
    const struct vfs_mount_ops *ops;

    if (!vfs_can_access_dir_node(vfs, node) || index_io == 0 || entry == 0) {
        return -1;
    }
    if (node->mount_kind == VFS_MOUNT_ROOT) {
        return vfs_readdir_root(vfs, index_io, entry);
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_read_dir_devfs(index_io, entry);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS) {
        return vfs_read_dir_procfs(node, index_io, entry);
    }
    if (node->mount_kind == VFS_MOUNT_EVENTFS) {
        return vfs_read_dir_eventfs(node, index_io, entry);
    }
    ops = vfs_mount_ops(node->mount_kind);
    return ops != 0 && ops->read_dir != 0 ? ops->read_dir(vfs, node, index_io, entry) : -1;
}

int vfs_prepare_opened_node(struct vfs *vfs,
                            struct vfs_node *node,
                            const char *path,
                            uint32_t flags,
                            uint32_t *offset_out) {
    const struct vfs_mount_ops *ops;

    if (offset_out == 0) {
        return -1;
    }
    *offset_out = 0;
    if (node == 0) {
        return -1;
    }
    ops = vfs_mount_ops(node->mount_kind);
    return ops != 0 && ops->prepare_opened_node != 0
               ? ops->prepare_opened_node(vfs, node, path, flags, offset_out)
               : 0;
}

int vfs_read_file_all(struct vfs *vfs,
                      const char *path,
                      struct vfs_node *node_out,
                      void *buffer,
                      uint32_t buffer_size,
                      uint32_t *bytes_read_out) {
    struct vfs_node node;
    uint32_t offset = 0;
    uint32_t file_size;
    int64_t read_rc;

    if (vfs == 0 || path == 0 || buffer == 0 || bytes_read_out == 0) {
        return -1;
    }
    *bytes_read_out = 0;
    if (vfs_open(vfs, path, 0, &node) != 0 || node.kind != VFS_NODE_FILE) {
        return -1;
    }
    file_size = vfs_node_file_size(&node);
    if (file_size > buffer_size) {
        return -1;
    }
    read_rc = vfs_read(vfs, &node, &offset, buffer, file_size, SYS_READ_BLOCKING);
    if (read_rc < 0 || (uint32_t)read_rc != file_size) {
        return -1;
    }
    if (node_out != 0) {
        *node_out = node;
    }
    *bytes_read_out = file_size;
    return 0;
}
