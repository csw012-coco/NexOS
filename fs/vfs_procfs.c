#include "fs/vfs_internal.h"
#include "fs/vfs_text.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"

static int64_t vfs_procfs_emit_dir_entry(struct vfs_dirent *entry,
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

int64_t vfs_read_from_procfs(struct vfs *vfs,
                             struct vfs_node *node,
                             uint32_t *offset_io,
                             void *buffer,
                             uint32_t size) {
    char *proc_text;
    uint32_t proc_text_size;
    uint32_t text_size = 0;

    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    if (node->aux_index == VFS_PROC_KMSG) {
        uint32_t copied = kprint_log_read(*offset_io, (char *)buffer, size);

        *offset_io += copied;
        return (int64_t)copied;
    }
    proc_text = vfs->procfs_text;
    proc_text_size = sizeof(vfs->procfs_text);
    proc_text[0] = '\0';
    text_size = vfs_format_procfs_node(vfs, node, proc_text, proc_text_size);
    if (text_size == 0) {
        return -1;
    }
    return vfs_read_from_generated_text(offset_io, buffer, size, proc_text, text_size);
}

int64_t vfs_read_dir_procfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry) {
    if (node->aux_index == VFS_PROC_PID_DIR) {
        if (*index_io == 0) {
            return vfs_procfs_emit_dir_entry(entry, index_io, "status", 0, 0);
        }
        return 0;
    }
    if (*index_io == 0) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "meminfo", 0, 0);
    }
    if (*index_io == 1) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "mounts", 0, 0);
    }
    if (*index_io == 2) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "uptime", 0, 0);
    }
    if (*index_io == 3) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "kmsg", 0, 0);
    }
    if (*index_io == 4) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "actions", 0, 0);
    }
    if (*index_io == 5) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "rtc", 0, 0);
    }
    if (*index_io == 6) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "caps", 0, 0);
    }
    if (*index_io == 7) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "devices", 0, 0);
    }
    if (*index_io == 8) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "drivers", 0, 0);
    }
    {
        uint32_t ordinal = *index_io - 9u;
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
                return vfs_procfs_emit_dir_entry(entry, index_io, name, 0, VFS_ATTR_DIR);
            }
            seen++;
        }
    }
    return 0;
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
    if (streq(name, "caps")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_CAPS, 0);
        return 0;
    }
    if (streq(name, "devices")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_DEVICES, 0);
        return 0;
    }
    if (streq(name, "drivers")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_DRIVERS, 0);
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
    if (*slash != '/' || !vfs_parse_u32_full(pid_text, &pid) || !vfs_procfs_pid_exists(pid)) {
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
    if (!vfs_parse_u32_full(name, &pid) || !vfs_procfs_pid_exists(pid)) {
        return -1;
    }
    vfs_set_procfs_node(out, VFS_NODE_DIR, VFS_PROC_PID_DIR, pid);
    return 0;
}
