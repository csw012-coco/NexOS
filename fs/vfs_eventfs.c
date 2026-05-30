#include "fs/vfs_internal.h"
#include "fs/vfs_text.h"
#include "lib/string.h"

static int64_t vfs_eventfs_emit_dir_entry(struct vfs_dirent *entry,
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

int64_t vfs_read_from_eventfs(struct vfs *vfs,
                              struct vfs_node *node,
                              uint32_t *offset_io,
                              void *buffer,
                              uint32_t size) {
    char *event_text;
    uint32_t event_text_size;

    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    event_text = vfs->eventfs_text;
    if (*offset_io == 0 ||
        *offset_io >= vfs->eventfs_text_size ||
        vfs->eventfs_text_node != node->aux_index) {
        *offset_io = 0;
        event_text[0] = '\0';
        vfs->eventfs_text_size = vfs_format_eventfs_node(node, event_text, sizeof(vfs->eventfs_text));
        vfs->eventfs_text_node = node->aux_index;
    }
    event_text_size = vfs->eventfs_text_size;
    if (event_text_size == 0) {
        return 0;
    }
    return vfs_read_from_generated_text(offset_io, buffer, size, event_text, event_text_size);
}

int64_t vfs_read_dir_eventfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry) {
    if (node->aux_index == VFS_EVENT_ROOT) {
        if (*index_io == 0) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "timer", 0, 0);
        }
        if (*index_io == 1) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "input", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 2) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "net", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 3) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "file", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 4) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "block", 0, VFS_ATTR_DIR);
        }
        if (*index_io == 5) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "security", 0, VFS_ATTR_DIR);
        }
        return 0;
    }
    if (node->aux_index == VFS_EVENT_INPUT_DIR) {
        if (*index_io == 0) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "keyboard", 0, 0);
        }
        if (*index_io == 1) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "keyboard.json", 0, 0);
        }
        if (*index_io == 2) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "mouse", 0, 0);
        }
        if (*index_io == 3) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "mouse.json", 0, 0);
        }
        return 0;
    }
    if (node->aux_index == VFS_EVENT_NET_DIR) {
        return *index_io == 0 ? vfs_eventfs_emit_dir_entry(entry, index_io, "status", 0, 0) : 0;
    }
    if (node->aux_index == VFS_EVENT_FILE_DIR) {
        return *index_io == 0 ? vfs_eventfs_emit_dir_entry(entry, index_io, "change", 0, 0) : 0;
    }
    if (node->aux_index == VFS_EVENT_BLOCK_DIR) {
        return *index_io == 0 ? vfs_eventfs_emit_dir_entry(entry, index_io, "change", 0, 0) : 0;
    }
    if (node->aux_index == VFS_EVENT_SECURITY_DIR) {
        return *index_io == 0 ? vfs_eventfs_emit_dir_entry(entry, index_io, "capability", 0, 0) : 0;
    }
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
    if (streq(name, "security/capability")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_SECURITY_CAPABILITY);
        return 0;
    }
    if (streq(name, "security/capability.json")) {
        vfs_set_eventfs_node(out, VFS_NODE_FILE, VFS_EVENT_SECURITY_CAPABILITY_JSON);
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
    if (streq(name, "security")) {
        vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_SECURITY_DIR);
        return 0;
    }
    return -1;
}
