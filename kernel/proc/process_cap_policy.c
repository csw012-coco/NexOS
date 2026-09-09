#include "kernel/public/proc/process.h"
#include "fs/vfs_internal.h"
#include "lib/string.h"

enum {
    PROCESS_CAP_POLICY_MODE_SHIFT = 16u,
    PROCESS_CAP_POLICY_PRESENT = 1u << 31
};

struct process_cap_policy_cap_name {
    uint32_t cap;
    const char *name;
};

static const struct process_cap_policy_cap_name g_process_cap_policy_cap_names[] = {
    {PROCESS_CAP_POWER, "power"},
    {PROCESS_CAP_RAW_BLOCK, "raw-block"},
    {PROCESS_CAP_MOUNT, "mount"},
    {PROCESS_CAP_SIGNAL, "signal"},
    {PROCESS_CAP_GRANT, "grant"},
    {PROCESS_CAP_AUDIO, "audio"},
    {PROCESS_CAP_NET_RAW, "net-raw"},
    {PROCESS_CAP_DISPLAY, "display"},
    {PROCESS_CAP_INPUT, "input"},
    {PROCESS_CAP_CLIPBOARD, "clipboard"},
    {PROCESS_CAP_DEBUG, "debug"},
};

static void process_cap_policy_copy_text(char *dst,
                                         uint32_t dst_size,
                                         const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void process_cap_policy_append_text(char *dst,
                                           uint32_t dst_size,
                                           uint32_t *pos_io,
                                           const char *src) {
    uint32_t pos;

    if (dst == 0 || dst_size == 0u || pos_io == 0) {
        return;
    }
    pos = *pos_io;
    while (src != 0 && src[0] != '\0' && pos + 1u < dst_size) {
        dst[pos++] = *src++;
    }
    dst[pos] = '\0';
    *pos_io = pos;
}

static void process_cap_policy_copy_cap_names(char *dst,
                                              uint32_t dst_size,
                                              uint32_t caps) {
    uint32_t pos = 0u;
    uint8_t wrote = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    dst[0] = '\0';
    caps &= PROCESS_CAP_SYS_ADMIN;
    if (caps == 0u) {
        process_cap_policy_copy_text(dst, dst_size, "none");
        return;
    }
    for (uint32_t i = 0u;
         i < sizeof(g_process_cap_policy_cap_names) /
             sizeof(g_process_cap_policy_cap_names[0]);
         i++) {
        if ((caps & g_process_cap_policy_cap_names[i].cap) == 0u) {
            continue;
        }
        if (wrote) {
            process_cap_policy_append_text(dst, dst_size, &pos, ",");
        }
        process_cap_policy_append_text(
            dst, dst_size, &pos, g_process_cap_policy_cap_names[i].name);
        wrote = 1u;
    }
}

static void process_cap_policy_emit_audit(const char *image_name,
                                          uint32_t base_caps,
                                          uint32_t result_caps,
                                          uint32_t matched) {
    struct syscall_capability_event event;
    uint8_t *bytes = (uint8_t *)&event;

    for (uint32_t i = 0u; i < sizeof(event); i++) {
        bytes[i] = 0u;
    }
    process_cap_policy_copy_text(
        event.source, sizeof(event.source), matched ? "exec-node-policy" : "exec-node-unmatched");
    process_cap_policy_copy_text(event.action, sizeof(event.action), image_name);
    process_cap_policy_copy_cap_names(event.caps, sizeof(event.caps), result_caps);
    event.required = base_caps & PROCESS_CAP_SYS_ADMIN;
    event.allowed = result_caps & PROCESS_CAP_SYS_ADMIN;
    event.missing = event.required & ~event.allowed;
    event.decision = SYS_CAP_DECISION_ALLOW;
    event.reason = SYS_CAP_REASON_EXEC_POLICY;
    vfs_event_capability_emit(&event);
}

static int process_cap_policy_node_mask(const struct vfs_node *node,
                                        uint32_t *mask_out) {
    uint32_t mask;

    if (node == 0 || mask_out == 0 ||
        node->kind != VFS_NODE_FILE ||
        node->mount_kind != VFS_MOUNT_NXFS) {
        return 0;
    }
    if ((node->handle.nxfs_inode.mode & PROCESS_CAP_POLICY_PRESENT) == 0u) {
        return 0;
    }
    mask = (node->handle.nxfs_inode.mode >> PROCESS_CAP_POLICY_MODE_SHIFT) &
           PROCESS_CAP_SYS_ADMIN;
    *mask_out = mask;
    return 1;
}

uint32_t process_exec_policy_capabilities(struct vfs *vfs,
                                          const struct vfs_node *node,
                                          const char *image_name,
                                          uint32_t base_caps) {
    uint32_t policy_mask = 0u;
    uint32_t result_caps = base_caps & PROCESS_CAP_SYS_ADMIN;
    int matched;

    (void)vfs;
    matched = process_cap_policy_node_mask(node, &policy_mask);
    if (matched) {
        result_caps &= policy_mask;
    }
    process_cap_policy_emit_audit(
        image_name,
        base_caps,
        result_caps,
        matched ? 1u : 0u);
    return result_caps;
}
