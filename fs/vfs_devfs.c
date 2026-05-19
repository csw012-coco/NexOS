#include "fs/vfs_internal.h"
#include "drivers/serial/uart.h"
#include "drivers/video/framebuffer.h"
#include "lib/string.h"

static void vfs_devfs_copy_bytes(void *dest, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dest;
    const uint8_t *in = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void vfs_devfs_fill_bytes(void *dest, uint8_t value, uint32_t size) {
    uint8_t *out = (uint8_t *)dest;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = value;
    }
}

static int vfs_devfs_is_decimal_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static int64_t vfs_devfs_emit_dir_entry(struct vfs_dirent *entry,
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

static int64_t vfs_read_devfs_zero(uint32_t *offset_io, void *buffer, uint32_t size) {
    vfs_devfs_fill_bytes(buffer, 0, size);
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
        return vfs_devfs_emit_dir_entry(entry,
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
        return vfs_devfs_emit_dir_entry(entry,
                                        index_io,
                                        entry->name,
                                        part.sector_count * dev->block_size,
                                        0);
    }
}

int64_t vfs_read_from_devfs(struct vfs *vfs,
                            struct vfs_node *node,
                            uint32_t *offset_io,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags) {
    uint64_t base_lba;
    uint64_t block_count;
    struct block_device *dev;

    (void)flags;
    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    dev = vfs_blockdev_from_node(node, &base_lba, &block_count);
    if (node->aux_index == VFS_DEV_TTY ||
        node->aux_index == VFS_DEV_TTY2 ||
        node->aux_index == VFS_DEV_TTY3 ||
        node->aux_index == VFS_DEV_STDIN) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_TTYS0) {
        int64_t read = (int64_t)uart_read((char *)buffer, size);

        *offset_io += (uint32_t)read;
        return read;
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
    if (node->aux_index == VFS_DEV_FRAMEBUFFER) {
        return framebuffer_device_read(offset_io, buffer, size);
    }
    if (dev == 0) {
        return -1;
    }
    return vfs_blockdev_read_bytes(vfs, dev, base_lba, block_count, offset_io, buffer, size);
}

int64_t vfs_write_to_devfs(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           const void *buffer,
                           uint32_t size) {
    uint64_t base_lba;
    uint64_t block_count;
    struct block_device *dev;

    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    dev = vfs_blockdev_from_node(node, &base_lba, &block_count);
    if (node->aux_index == VFS_DEV_TTY ||
        node->aux_index == VFS_DEV_TTY2 ||
        node->aux_index == VFS_DEV_TTY3 ||
        node->aux_index == VFS_DEV_STDOUT ||
        node->aux_index == VFS_DEV_STDERR) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_TTYS0) {
        uint32_t written = uart_write_buffer((const char *)buffer, size);

        *offset_io += written;
        return (int64_t)written;
    }
    if (node->aux_index == VFS_DEV_STDIN) {
        return -1;
    }
    if (node->aux_index == VFS_DEV_NULL || node->aux_index == VFS_DEV_ZERO) {
        *offset_io += size;
        return (int64_t)size;
    }
    if (node->aux_index == VFS_DEV_FRAMEBUFFER) {
        return framebuffer_device_write(offset_io, buffer, size);
    }
    if (dev == 0) {
        return -1;
    }
    return vfs_blockdev_write_bytes(vfs, dev, base_lba, block_count, offset_io, buffer, size);
}

uint32_t vfs_devfs_file_size(const struct vfs_node *node) {
    if (node != 0 && node->mount_kind == VFS_MOUNT_DEVFS && node->aux_index == VFS_DEV_FRAMEBUFFER) {
        return framebuffer_device_size();
    }
    return 0;
}

int64_t vfs_read_dir_devfs(uint32_t *index_io, struct vfs_dirent *entry) {
    if (*index_io == 0) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "tty", 0, 0);
    } else if (*index_io == 1) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "tty2", 0, 0);
    } else if (*index_io == 2) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "tty3", 0, 0);
    } else if (*index_io == 3) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "null", 0, 0);
    } else if (*index_io == 4) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "zero", 0, 0);
    } else if (*index_io == 5) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "stdin", 0, 0);
    } else if (*index_io == 6) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "stdout", 0, 0);
    } else if (*index_io == 7) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "stderr", 0, 0);
    } else if (*index_io == 8 && framebuffer_display_active()) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "fb", framebuffer_device_size(), 0);
    } else if (*index_io == (framebuffer_display_active() ? 9u : 8u) && uart_is_ready()) {
        return vfs_devfs_emit_dir_entry(entry, index_io, "ttyS0", 0, 0);
    } else {
        uint32_t base_index = (framebuffer_display_active() ? 9u : 8u) + (uart_is_ready() ? 1u : 0u);
        uint32_t ordinal = *index_io - base_index;
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

static int64_t vfs_blockdev_transfer_bytes(struct vfs *vfs,
                                           struct block_device *dev,
                                           uint64_t base_lba,
                                           uint64_t block_count,
                                           uint32_t *offset_io,
                                           void *buffer,
                                           uint32_t size,
                                           uint32_t write_mode) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint8_t *block_buffer;
    uint32_t total = 0;

    if (vfs == 0 || dev == 0 || offset_io == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (dev->block_size == 0 || dev->block_size > VFS_DEV_BLOCK_BUFFER_SIZE ||
        (write_mode != 0 && dev->write == 0)) {
        return -1;
    }
    block_buffer = vfs->devfs_block_buffer;
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
            blockdev_read(dev, base_lba + lba, 1, block_buffer) != 0) {
            return total != 0 ? (int64_t)total : -1;
        }
        if (write_mode != 0) {
            vfs_devfs_copy_bytes(block_buffer + block_off, bytes + total, chunk);
            if (blockdev_write(dev, base_lba + lba, 1, block_buffer) != 0) {
                return total != 0 ? (int64_t)total : -1;
            }
        } else {
            vfs_devfs_copy_bytes(bytes + total, block_buffer + block_off, chunk);
        }
        total += chunk;
    }

    *offset_io += total;
    return (int64_t)total;
}

int64_t vfs_blockdev_read_bytes(struct vfs *vfs,
                                struct block_device *dev,
                                uint64_t base_lba,
                                uint64_t block_count,
                                uint32_t *offset_io,
                                void *buffer,
                                uint32_t size) {
    return vfs_blockdev_transfer_bytes(vfs,
                                       dev,
                                       base_lba,
                                       block_count,
                                       offset_io,
                                       buffer,
                                       size,
                                       0);
}

int64_t vfs_blockdev_write_bytes(struct vfs *vfs,
                                 struct block_device *dev,
                                 uint64_t base_lba,
                                 uint64_t block_count,
                                 uint32_t *offset_io,
                                 const void *buffer,
                                 uint32_t size) {
    return vfs_blockdev_transfer_bytes(vfs,
                                       dev,
                                       base_lba,
                                       block_count,
                                       offset_io,
                                       (void *)buffer,
                                       size,
                                       1);
}

int vfs_devfs_lookup(const char *name, struct vfs_node *out) {
    uint32_t index = 0;

    if (name == 0 || out == 0) {
        return -1;
    }
    if (streq(name, "tty") || streq(name, "tty1")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_TTY);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_TTY, 0u);
        return 0;
    }
    if (streq(name, "tty2")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_TTY2);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_TTY, 1u);
        return 0;
    }
    if (streq(name, "tty3")) {
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_TTY3);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_TTY, 2u);
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
    if (streq(name, "fb") || streq(name, "fb0")) {
        if (!framebuffer_display_active()) {
            return -1;
        }
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_FRAMEBUFFER);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_FRAMEBUFFER, 0u);
        return 0;
    }
    if (streq(name, "ttyS0") || streq(name, "com1")) {
        if (!uart_is_ready()) {
            return -1;
        }
        vfs_set_devfs_node(out, VFS_NODE_FILE, VFS_DEV_TTYS0);
        vfs_set_node_device_numbers(out, VFS_DEV_MAJOR_TTY, 64u);
        return 0;
    }
    if (starts_with(name, "disk")) {
        const char *digits = name + 4;
        uint32_t part_index = 0;

        if (*digits == '\0') {
            return -1;
        }
        while (vfs_devfs_is_decimal_digit(*digits)) {
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
        while (vfs_devfs_is_decimal_digit(*digits)) {
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
