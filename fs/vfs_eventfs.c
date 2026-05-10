#include "fs/vfs_internal.h"
#include "fs/vfs_text.h"
#include "block/block_event.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "drivers/net/net_event.h"
#include "kernel/public/proc/scheduler.h"
#include "lib/string.h"

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
        vfs->eventfs_text_size = vfs_format_eventfs(node, event_text, sizeof(vfs->eventfs_text));
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
        return 0;
    }
    if (node->aux_index == VFS_EVENT_INPUT_DIR) {
        if (*index_io == 0) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "keyboard", 0, 0);
        }
        if (*index_io == 1) {
            return vfs_eventfs_emit_dir_entry(entry, index_io, "mouse", 0, 0);
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
