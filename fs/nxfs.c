#include "fs/nxfs_internal.h"
#include "kernel/public/sys/system_limits.h"
#include "lib/string.h"

enum {
    NXFS_ROOT_INODE = 0
};

static uint32_t nxfs_inode_block_count(const struct nxfs_inode *inode) {
    uint32_t total = 0;

    for (uint32_t i = 0; i < NXFS_EXTENTS; i++) {
        total += inode->extents[i].len;
    }
    return total;
}

static int nxfs_write_inode(struct nxfs_volume *vol, uint32_t inode_index, const struct nxfs_inode *inode) {
    uint32_t inode_offset;

    if (vol == 0 || !vol->mounted || inode == 0 || inode_index >= NXFS_MAX_INODES) {
        return -1;
    }
    inode_offset = vol->super.inode_start * NXFS_BLOCK_SIZE + inode_index * sizeof(*inode);
    return nxfs_write_bytes(vol, inode_offset, inode, sizeof(*inode));
}

static int nxfs_split_parent_child_path(const char *path,
                                        char *parent_path,
                                        uint32_t parent_size,
                                        char *child_name,
                                        uint32_t child_size);

static int nxfs_logical_to_physical(const struct nxfs_inode *inode, uint32_t logical_block) {
    uint32_t base = 0;

    for (uint32_t i = 0; i < NXFS_EXTENTS; i++) {
        uint32_t len = inode->extents[i].len;

        if (logical_block < base + len) {
            return (int)(inode->extents[i].start + (logical_block - base));
        }
        base += len;
    }

    return -1;
}

static int nxfs_append_extent(struct nxfs_inode *inode, uint32_t start, uint32_t len) {
    for (uint32_t i = 0; i < NXFS_EXTENTS; i++) {
        if (inode->extents[i].len == 0) {
            inode->extents[i].start = start;
            inode->extents[i].len = len;
            return 0;
        }
    }
    return -1;
}

static uint32_t nxfs_bitmap_bits_per_block(void) {
    return NXFS_BLOCK_SIZE * 8u;
}

static uint32_t nxfs_bitmap_block_count(struct nxfs_volume *vol) {
    if (vol == 0 || vol->super.inode_start <= vol->super.bitmap_start) {
        return 0;
    }
    return vol->super.inode_start - vol->super.bitmap_start;
}

static int nxfs_bitmap_bit_get(const uint8_t *bitmap, uint32_t block) {
    return (bitmap[block / 8u] >> (block % 8u)) & 1u;
}

static void nxfs_bitmap_bit_set(uint8_t *bitmap, uint32_t block, int used) {
    if (used) {
        bitmap[block / 8u] |= (uint8_t)(1u << (block % 8u));
    } else {
        bitmap[block / 8u] &= (uint8_t)~(1u << (block % 8u));
    }
}

static int nxfs_bitmap_set_range(struct nxfs_volume *vol, uint32_t start, uint32_t len, int used) {
    uint32_t bits_per_block = nxfs_bitmap_bits_per_block();
    uint32_t done = 0;
    uint8_t bitmap[NXFS_BLOCK_SIZE];

    if (vol == 0 || len == 0 || start >= vol->super.total_blocks ||
        start + len < start || start + len > vol->super.total_blocks) {
        return -1;
    }
    while (done < len) {
        uint32_t block = start + done;
        uint32_t bitmap_index = block / bits_per_block;
        uint32_t bit = block % bits_per_block;
        uint32_t chunk = bits_per_block - bit;

        if (chunk > len - done) {
            chunk = len - done;
        }
        if (bitmap_index >= nxfs_bitmap_block_count(vol) ||
            nxfs_read_block(vol, vol->super.bitmap_start + bitmap_index, bitmap) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            nxfs_bitmap_bit_set(bitmap, bit + i, used);
        }
        if (nxfs_write_block(vol, vol->super.bitmap_start + bitmap_index, bitmap) != 0) {
            return -1;
        }
        done += chunk;
    }
    return 0;
}

int nxfs_space_info(struct nxfs_volume *vol,
                    uint32_t *block_size_out,
                    uint64_t *total_blocks_out,
                    uint64_t *free_blocks_out) {
    uint64_t free_blocks = 0;
    uint32_t bits_per_block;
    uint32_t bitmap_blocks;
    uint8_t bitmap[NXFS_BLOCK_SIZE];

    if (vol == 0 || !vol->mounted || block_size_out == 0 ||
        total_blocks_out == 0 || free_blocks_out == 0) {
        return -1;
    }
    bits_per_block = nxfs_bitmap_bits_per_block();
    bitmap_blocks = nxfs_bitmap_block_count(vol);
    for (uint32_t bitmap_index = 0; bitmap_index < bitmap_blocks; bitmap_index++) {
        uint32_t base_block = bitmap_index * bits_per_block;
        uint32_t limit = bits_per_block;

        if (base_block >= vol->super.total_blocks) {
            break;
        }
        if (limit > vol->super.total_blocks - base_block) {
            limit = vol->super.total_blocks - base_block;
        }
        if (nxfs_read_block(vol, vol->super.bitmap_start + bitmap_index, bitmap) != 0) {
            return -1;
        }
        for (uint32_t bit = 0; bit < limit; bit++) {
            if (!nxfs_bitmap_bit_get(bitmap, bit)) {
                free_blocks++;
            }
        }
    }
    *block_size_out = NXFS_BLOCK_SIZE;
    *total_blocks_out = vol->super.total_blocks;
    *free_blocks_out = free_blocks;
    return 0;
}

static int nxfs_find_free_inode(struct nxfs_volume *vol) {
    struct nxfs_inode inode;

    for (uint32_t i = 0; i < NXFS_MAX_INODES; i++) {
        if (nxfs_read_inode(vol, i, &inode) != 0) {
            return (int)i;
        }
    }
    return -1;
}

static int nxfs_alloc_extent(struct nxfs_volume *vol, uint32_t len) {
    uint32_t bits_per_block;
    uint32_t bitmap_blocks;
    uint32_t run_start = 0;
    uint32_t run_len = 0;
    uint8_t bitmap[NXFS_BLOCK_SIZE];

    if (vol == 0 || !vol->mounted || len == 0) {
        return -1;
    }
    bits_per_block = nxfs_bitmap_bits_per_block();
    bitmap_blocks = nxfs_bitmap_block_count(vol);
    for (uint32_t bitmap_index = 0; bitmap_index < bitmap_blocks; bitmap_index++) {
        uint32_t base_block = bitmap_index * bits_per_block;
        uint32_t limit = bits_per_block;

        if (base_block >= vol->super.total_blocks) {
            break;
        }
        if (limit > vol->super.total_blocks - base_block) {
            limit = vol->super.total_blocks - base_block;
        }
        if (nxfs_read_block(vol, vol->super.bitmap_start + bitmap_index, bitmap) != 0) {
            return -1;
        }
        for (uint32_t bit = 0; bit < limit; bit++) {
            uint32_t block = base_block + bit;

            if (block < vol->super.data_start) {
                continue;
            }
            if (nxfs_bitmap_bit_get(bitmap, bit)) {
                run_len = 0;
                continue;
            }
            if (run_len == 0) {
                run_start = block;
            }
            run_len++;
            if (run_len >= len) {
                if (nxfs_bitmap_set_range(vol, run_start, len, 1) != 0) {
                    return -1;
                }
                return (int)run_start;
            }
        }
        if (limit != bits_per_block) {
            break;
        }
    }
    return -1;
}

static int nxfs_free_extent(struct nxfs_volume *vol, uint32_t start, uint32_t len) {
    if (vol == 0 || !vol->mounted || len == 0) {
        return 0;
    }
    return nxfs_bitmap_set_range(vol, start, len, 0);
}

static int nxfs_free_all_extents(struct nxfs_volume *vol, const struct nxfs_inode *inode) {
    for (uint32_t i = 0; i < NXFS_EXTENTS; i++) {
        if (inode->extents[i].len != 0 &&
            nxfs_free_extent(vol, inode->extents[i].start, inode->extents[i].len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int nxfs_ensure_blocks(struct nxfs_volume *vol, struct nxfs_inode *inode, uint32_t need_blocks) {
    struct nxfs_inode tmp;
    uint32_t have;

    if (vol == 0 || inode == 0) {
        return -1;
    }
    tmp = *inode;
    have = nxfs_inode_block_count(&tmp);
    while (have < need_blocks) {
        uint32_t remaining = need_blocks - have;
        uint32_t chunk = remaining;
        int start = -1;

        while (chunk > 0) {
            start = nxfs_alloc_extent(vol, chunk);
            if (start >= 0) {
                break;
            }
            chunk--;
        }
        if (start < 0 || nxfs_append_extent(&tmp, (uint32_t)start, chunk) != 0) {
            return -1;
        }
        nxfs_mem_set(vol->sector_buffer, 0, sizeof(vol->sector_buffer));
        for (uint32_t i = 0; i < chunk; i++) {
            if (nxfs_write_block(vol, (uint32_t)start + i, vol->sector_buffer) != 0) {
                return -1;
            }
        }
        have += chunk;
    }
    *inode = tmp;
    return 0;
}

static int nxfs_read_inode_block(struct nxfs_volume *vol, const struct nxfs_inode *inode, uint32_t logical_block, void *buffer) {
    int phys = nxfs_logical_to_physical(inode, logical_block);

    if (phys < 0) {
        return -1;
    }
    return nxfs_read_block(vol, (uint32_t)phys, buffer);
}

static int nxfs_write_inode_block(struct nxfs_volume *vol, const struct nxfs_inode *inode, uint32_t logical_block, const void *buffer) {
    int phys = nxfs_logical_to_physical(inode, logical_block);

    if (phys < 0) {
        return -1;
    }
    return nxfs_write_block(vol, (uint32_t)phys, buffer);
}

static int nxfs_calc_block_window(uint32_t block_start,
                                  uint32_t block_end,
                                  uint32_t data_limit,
                                  uint32_t offset,
                                  uint32_t request_end,
                                  uint32_t *copy_start_out,
                                  uint32_t *chunk_out) {
    uint32_t copy_start;
    uint32_t copy_end;

    if (copy_start_out == 0 || chunk_out == 0) {
        return 0;
    }
    if (offset >= block_end || block_start >= data_limit) {
        return 0;
    }
    copy_start = offset > block_start ? offset : block_start;
    copy_end = block_end;
    if (copy_end > data_limit) {
        copy_end = data_limit;
    }
    if (copy_end > request_end) {
        copy_end = request_end;
    }
    if (copy_end <= copy_start) {
        return 0;
    }
    *copy_start_out = copy_start;
    *chunk_out = copy_end - copy_start;
    return 1;
}

static int nxfs_resolve_parent_dir(struct nxfs_volume *vol,
                                   const char *path,
                                   uint32_t *parent_ino_out,
                                   struct nxfs_inode *parent_out,
                                   char parent_path[NOS_PATH_BUFFER_SIZE],
                                   char name[28]) {
    if (vol == 0 || !vol->mounted || path == 0 || path[0] == '\0' ||
        parent_ino_out == 0 || parent_out == 0) {
        return -1;
    }
    if (nxfs_split_parent_child_path(path, parent_path, NOS_PATH_BUFFER_SIZE, name, 28) != 0) {
        return -1;
    }
    *parent_ino_out = NXFS_ROOT_INODE;
    if (parent_path[0] == '\0') {
        return nxfs_read_inode(vol, NXFS_ROOT_INODE, parent_out) == 0 && parent_out->type == NXFS_TYPE_DIR ? 0 : -1;
    }
    return nxfs_lookup_path(vol, parent_path, parent_ino_out, parent_out) == 0 && parent_out->type == NXFS_TYPE_DIR
               ? 0
               : -1;
}

static int nxfs_find_dir_entry_slot(struct nxfs_volume *vol,
                                    const struct nxfs_inode *dir,
                                    const char *name,
                                    int want_free_slot,
                                    uint32_t *found_ino,
                                    uint32_t *found_block,
                                    uint32_t *found_slot) {
    uint32_t blocks = nxfs_inode_block_count(dir);

    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        struct nxfs_dir_entry block_entries[NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry)];

        if (nxfs_read_inode_block(vol, dir, block_idx, block_entries) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry); i++) {
            if (want_free_slot) {
                if (block_entries[i].name[0] != '\0') {
                    continue;
                }
            } else {
                if (block_entries[i].name[0] == '\0' || !streq(block_entries[i].name, name)) {
                    continue;
                }
                if (found_ino != 0) {
                    *found_ino = block_entries[i].inode;
                }
            }
            if (found_block != 0) {
                *found_block = block_idx;
            }
            if (found_slot != 0) {
                *found_slot = i;
            }
            return 0;
        }
    }
    return -1;
}

static int nxfs_dir_lookup_entry(struct nxfs_volume *vol,
                                 const struct nxfs_inode *dir,
                                 const char *name,
                                 uint32_t *found_ino,
                                 uint32_t *found_block,
                                 uint32_t *found_slot) {
    return nxfs_find_dir_entry_slot(vol, dir, name, 0, found_ino, found_block, found_slot);
}

static int nxfs_dir_find_free_slot(struct nxfs_volume *vol,
                                   struct nxfs_inode *dir,
                                   uint32_t *found_block,
                                   uint32_t *found_slot) {
    uint32_t blocks = nxfs_inode_block_count(dir);

    if (nxfs_find_dir_entry_slot(vol, dir, 0, 1, 0, found_block, found_slot) == 0) {
        return 0;
    }
    if (nxfs_ensure_blocks(vol, dir, blocks + 1u) != 0) {
        return -1;
    }
    *found_block = blocks;
    *found_slot = 0;
    return 0;
}

static int nxfs_dir_add_entry(struct nxfs_volume *vol,
                              uint32_t dir_ino,
                              struct nxfs_inode *dir,
                              const char *name,
                              uint32_t child_ino) {
    uint32_t block_idx;
    uint32_t slot_idx;
    struct nxfs_dir_entry block_entries[NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry)];

    if (vol == 0 || dir == 0 || name == 0 || name[0] == '\0') {
        return -1;
    }
    if (str_len(name) >= sizeof(block_entries[0].name)) {
        return -1;
    }
    if (nxfs_dir_lookup_entry(vol, dir, name, 0, 0, 0) == 0) {
        return -1;
    }
    if (nxfs_dir_find_free_slot(vol, dir, &block_idx, &slot_idx) != 0) {
        return -1;
    }
    nxfs_mem_set(block_entries, 0, sizeof(block_entries));
    if (block_idx < nxfs_inode_block_count(dir) &&
        nxfs_read_inode_block(vol, dir, block_idx, block_entries) != 0) {
        return -1;
    }
    block_entries[slot_idx].inode = child_ino;
    nxfs_mem_set(block_entries[slot_idx].name, 0, sizeof(block_entries[slot_idx].name));
    nxfs_mem_copy(block_entries[slot_idx].name, name, str_len(name));
    if (nxfs_write_inode_block(vol, dir, block_idx, block_entries) != 0) {
        return -1;
    }
    dir->size = nxfs_inode_block_count(dir) * NXFS_BLOCK_SIZE;
    return nxfs_write_inode(vol, dir_ino, dir);
}

static int nxfs_dir_remove_entry(struct nxfs_volume *vol, uint32_t dir_ino, struct nxfs_inode *dir, const char *name) {
    uint32_t block_idx;
    uint32_t slot_idx;
    struct nxfs_dir_entry block_entries[NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry)];

    if (nxfs_dir_lookup_entry(vol, dir, name, 0, &block_idx, &slot_idx) != 0) {
        return -1;
    }
    if (nxfs_read_inode_block(vol, dir, block_idx, block_entries) != 0) {
        return -1;
    }
    block_entries[slot_idx].inode = 0;
    nxfs_mem_set(block_entries[slot_idx].name, 0, sizeof(block_entries[slot_idx].name));
    if (nxfs_write_inode_block(vol, dir, block_idx, block_entries) != 0) {
        return -1;
    }
    dir->size = nxfs_inode_block_count(dir) * NXFS_BLOCK_SIZE;
    return nxfs_write_inode(vol, dir_ino, dir);
}

static int nxfs_dir_is_empty(struct nxfs_volume *vol, const struct nxfs_inode *dir) {
    uint32_t count = 0;
    struct nxfs_dir_entry entries[16];

    if (vol == 0 || dir == 0 || dir->type != NXFS_TYPE_DIR) {
        return 0;
    }
    if (nxfs_list_dir(vol, 0, dir, entries, 16, &count) != 0) {
        return 0;
    }
    for (uint32_t i = 0; i < count && i < 16u; i++) {
        if (!streq(entries[i].name, ".") && !streq(entries[i].name, "..")) {
            return 0;
        }
    }
    return 1;
}

static int nxfs_path_next_segment(const char **path_io, char *segment, uint32_t segment_size) {
    const char *path = *path_io;
    uint32_t len = 0;

    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        return 0;
    }
    while (*path != '\0' && *path != '/') {
        if (len + 1u >= segment_size) {
            return -1;
        }
        segment[len++] = *path++;
    }
    segment[len] = '\0';
    while (*path == '/') {
        path++;
    }
    *path_io = path;
    return 1;
}

static int nxfs_split_parent_child_path(const char *path,
                                        char *parent,
                                        uint32_t parent_size,
                                        char *name,
                                        uint32_t name_size) {
    uint32_t len = 0;
    uint32_t end;
    uint32_t slash = 0xffffffffu;

    if (path == 0 || parent == 0 || name == 0 || parent_size == 0 || name_size == 0) {
        return -1;
    }
    while (path[len] != '\0') {
        len++;
    }
    while (len != 0u && path[len - 1u] == '/') {
        len--;
    }
    while (*path == '/') {
        path++;
        len--;
    }
    if (len == 0u) {
        return -1;
    }
    end = len;
    for (uint32_t i = 0; i < end; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }
    parent[0] = '\0';
    name[0] = '\0';
    if (slash == 0xffffffffu) {
        if (end + 1u > name_size) {
            return -1;
        }
        nxfs_mem_copy(name, path, end);
        name[end] = '\0';
        return 0;
    }
    if (slash + 1u > parent_size || end - (slash + 1u) + 1u > name_size) {
        return -1;
    }
    if (slash != 0u) {
        nxfs_mem_copy(parent, path, slash);
        parent[slash] = '\0';
    }
    nxfs_mem_copy(name, path + slash + 1u, end - (slash + 1u));
    name[end - (slash + 1u)] = '\0';
    return name[0] != '\0' ? 0 : -1;
}

int nxfs_mount(struct nxfs_volume *vol, struct block_device *bdev, uint32_t partition_lba) {
    if (vol == 0 || bdev == 0 || bdev->block_size != NXFS_BLOCK_SIZE) {
        return -1;
    }

    nxfs_mem_set(vol, 0, sizeof(*vol));
    if (blockdev_read(bdev, partition_lba, 1, vol->sector_buffer) != 0) {
        return -1;
    }

    nxfs_mem_copy(&vol->super, vol->sector_buffer, sizeof(vol->super));
    if (vol->super.magic != NXFS_MAGIC || vol->super.total_blocks == 0) {
        nxfs_mem_set(vol, 0, sizeof(*vol));
        return -1;
    }

    vol->bdev = bdev;
    vol->partition_lba = partition_lba;
    vol->mounted = 1;
    return 0;
}

int nxfs_read_inode(struct nxfs_volume *vol, uint32_t inode_index, struct nxfs_inode *out) {
    uint32_t inode_offset;

    if (vol == 0 || !vol->mounted || out == 0 || inode_index >= NXFS_MAX_INODES) {
        return -1;
    }

    inode_offset = vol->super.inode_start * NXFS_BLOCK_SIZE + inode_index * sizeof(*out);
    if (nxfs_read_bytes(vol, inode_offset, out, sizeof(*out)) != 0) {
        return -1;
    }
    return out->used ? 0 : -1;
}

int nxfs_list_root(struct nxfs_volume *vol,
                   struct nxfs_dir_entry *entries,
                   uint32_t max_entries,
                   uint32_t *entry_count) {
    struct nxfs_inode root;

    if (vol == 0 || !vol->mounted || entries == 0 || entry_count == 0) {
        return -1;
    }
    if (nxfs_read_inode(vol, NXFS_ROOT_INODE, &root) != 0 || root.type != NXFS_TYPE_DIR) {
        return -1;
    }
    return nxfs_list_dir(vol, NXFS_ROOT_INODE, &root, entries, max_entries, entry_count);
}

int nxfs_lookup_root(struct nxfs_volume *vol, const char *name, uint32_t *inode_index, struct nxfs_inode *out) {
    return nxfs_lookup_path(vol, name, inode_index, out);
}

int nxfs_lookup_path(struct nxfs_volume *vol, const char *path, uint32_t *inode_index, struct nxfs_inode *out) {
    struct nxfs_inode root;
    struct nxfs_inode current;
    uint32_t current_ino = NXFS_ROOT_INODE;
    uint32_t child_ino;
    char segment[28];
    int rc;

    if (vol == 0 || !vol->mounted || path == 0) {
        return -1;
    }
    if (nxfs_read_inode(vol, NXFS_ROOT_INODE, &root) != 0 || root.type != NXFS_TYPE_DIR) {
        return -1;
    }
    current = root;
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        if (inode_index != 0) {
            *inode_index = current_ino;
        }
        if (out != 0) {
            *out = current;
        }
        return 0;
    }
    for (;;) {
        rc = nxfs_path_next_segment(&path, segment, sizeof(segment));
        if (rc <= 0) {
            return rc == 0 ? 0 : -1;
        }
        if (current.type != NXFS_TYPE_DIR ||
            nxfs_dir_lookup_entry(vol, &current, segment, &child_ino, 0, 0) != 0 ||
            nxfs_read_inode(vol, child_ino, &current) != 0) {
            return -1;
        }
        current_ino = child_ino;
        if (*path == '\0') {
            if (inode_index != 0) {
                *inode_index = current_ino;
            }
            if (out != 0) {
                *out = current;
            }
            return 0;
        }
    }
}

int nxfs_list_dir(struct nxfs_volume *vol,
                  uint32_t inode_index,
                  const struct nxfs_inode *dir,
                  struct nxfs_dir_entry *entries,
                  uint32_t max_entries,
                  uint32_t *entry_count) {
    uint32_t total = 0;
    uint32_t blocks;

    (void)inode_index;
    if (vol == 0 || !vol->mounted || dir == 0 || entries == 0 || entry_count == 0 || dir->type != NXFS_TYPE_DIR) {
        return -1;
    }
    blocks = nxfs_inode_block_count(dir);
    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        struct nxfs_dir_entry block_entries[NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry)];

        if (nxfs_read_inode_block(vol, dir, block_idx, block_entries) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry); i++) {
            if (block_entries[i].name[0] == '\0') {
                continue;
            }
            if (total < max_entries) {
                entries[total] = block_entries[i];
            }
            total++;
        }
    }
    *entry_count = total;
    return 0;
}

int nxfs_get_dir_entry(struct nxfs_volume *vol,
                       uint32_t inode_index,
                       const struct nxfs_inode *dir,
                       uint32_t entry_index,
                       struct nxfs_dir_entry *entry_out) {
    uint32_t total = 0;
    uint32_t blocks;

    (void)inode_index;
    if (vol == 0 || !vol->mounted || dir == 0 || entry_out == 0 || dir->type != NXFS_TYPE_DIR) {
        return -1;
    }
    blocks = nxfs_inode_block_count(dir);
    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        struct nxfs_dir_entry block_entries[NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry)];

        if (nxfs_read_inode_block(vol, dir, block_idx, block_entries) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry); i++) {
            if (block_entries[i].name[0] == '\0') {
                continue;
            }
            if (total == entry_index) {
                *entry_out = block_entries[i];
                return 0;
            }
            total++;
        }
    }
    return -1;
}

int nxfs_create_root(struct nxfs_volume *vol, const char *name, uint32_t *inode_index, struct nxfs_inode *out) {
    return nxfs_create_path(vol, name, inode_index, out);
}

int nxfs_create_path(struct nxfs_volume *vol, const char *path, uint32_t *inode_index, struct nxfs_inode *out) {
    struct nxfs_inode parent;
    struct nxfs_inode root;
    struct nxfs_inode inode;
    uint32_t parent_ino = NXFS_ROOT_INODE;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[28];
    int ino;

    if (vol == 0 || !vol->mounted || path == 0 || path[0] == '\0') {
        return -1;
    }
    if (nxfs_split_parent_child_path(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return -1;
    }
    if (str_len(name) >= sizeof(((struct nxfs_dir_entry *)0)->name)) {
        return -1;
    }
    if (parent_path[0] == '\0') {
        if (nxfs_read_inode(vol, NXFS_ROOT_INODE, &parent) != 0 || parent.type != NXFS_TYPE_DIR) {
            return -1;
        }
    } else if (nxfs_lookup_path(vol, parent_path, &parent_ino, &parent) != 0 || parent.type != NXFS_TYPE_DIR) {
        return -1;
    }
    if (nxfs_lookup_path(vol, path, 0, 0) == 0) {
        return -1;
    }
    root = parent;
    ino = nxfs_find_free_inode(vol);
    if (ino < 0) {
        return -1;
    }
    nxfs_mem_set(&inode, 0, sizeof(inode));
    inode.used = 1;
    inode.type = NXFS_TYPE_FILE;
    inode.nlink = 1;
    if (nxfs_write_inode(vol, (uint32_t)ino, &inode) != 0) {
        return -1;
    }
    if (nxfs_dir_add_entry(vol, parent_ino, &root, name, (uint32_t)ino) != 0) {
        nxfs_mem_set(&inode, 0, sizeof(inode));
        (void)nxfs_write_inode(vol, (uint32_t)ino, &inode);
        return -1;
    }
    if (inode_index != 0) {
        *inode_index = (uint32_t)ino;
    }
    if (out != 0) {
        *out = inode;
    }
    return 0;
}

int nxfs_mkdir_path(struct nxfs_volume *vol, const char *path, uint32_t *inode_index, struct nxfs_inode *out) {
    struct nxfs_inode parent;
    struct nxfs_inode dir;
    uint32_t parent_ino = NXFS_ROOT_INODE;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[28];
    int ino;

    if (vol == 0 || !vol->mounted || path == 0 || path[0] == '\0') {
        return -1;
    }
    if (nxfs_split_parent_child_path(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return -1;
    }
    if (name[0] == '\0' || streq(name, ".") || streq(name, "..")) {
        return -1;
    }
    if (parent_path[0] == '\0') {
        if (nxfs_read_inode(vol, NXFS_ROOT_INODE, &parent) != 0 || parent.type != NXFS_TYPE_DIR) {
            return -1;
        }
    } else if (nxfs_lookup_path(vol, parent_path, &parent_ino, &parent) != 0 || parent.type != NXFS_TYPE_DIR) {
        return -1;
    }
    if (nxfs_lookup_path(vol, path, 0, 0) == 0) {
        return -1;
    }
    ino = nxfs_find_free_inode(vol);
    if (ino < 0) {
        return -1;
    }
    nxfs_mem_set(&dir, 0, sizeof(dir));
    dir.used = 1;
    dir.type = NXFS_TYPE_DIR;
    dir.nlink = 2;
    dir.size = NXFS_BLOCK_SIZE;
    if (nxfs_ensure_blocks(vol, &dir, 1) != 0 ||
        nxfs_write_inode(vol, (uint32_t)ino, &dir) != 0 ||
        nxfs_dir_add_entry(vol, (uint32_t)ino, &dir, ".", (uint32_t)ino) != 0 ||
        nxfs_dir_add_entry(vol, (uint32_t)ino, &dir, "..", parent_ino) != 0 ||
        nxfs_dir_add_entry(vol, parent_ino, &parent, name, (uint32_t)ino) != 0) {
        (void)nxfs_free_all_extents(vol, &dir);
        nxfs_mem_set(&dir, 0, sizeof(dir));
        (void)nxfs_write_inode(vol, (uint32_t)ino, &dir);
        return -1;
    }
    parent.nlink++;
    if (nxfs_write_inode(vol, parent_ino, &parent) != 0) {
        return -1;
    }
    if (inode_index != 0) {
        *inode_index = (uint32_t)ino;
    }
    if (out != 0) {
        *out = dir;
    }
    return 0;
}

int nxfs_read_file(struct nxfs_volume *vol,
                   const struct nxfs_inode *inode,
                   void *buffer,
                   uint32_t buffer_size,
                   uint32_t *bytes_read) {
    return nxfs_read_file_range(vol, inode, 0, buffer, buffer_size, bytes_read);
}

int nxfs_read_file_range(struct nxfs_volume *vol,
                         const struct nxfs_inode *inode,
                         uint32_t offset,
                         void *buffer,
                         uint32_t buffer_size,
                         uint32_t *bytes_read) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0;
    uint32_t blocks;

    if (vol == 0 || !vol->mounted || inode == 0 || buffer == 0 || bytes_read == 0) {
        return -1;
    }
    if (inode->type != NXFS_TYPE_FILE || offset > inode->size) {
        return -1;
    }
    if (buffer_size == 0 || offset == inode->size) {
        *bytes_read = 0;
        return 0;
    }

    blocks = nxfs_inode_block_count(inode);
    for (uint32_t block_idx = 0; block_idx < blocks && done < buffer_size; block_idx++) {
        uint8_t block[NXFS_BLOCK_SIZE];
        uint32_t block_start = block_idx * NXFS_BLOCK_SIZE;
        uint32_t block_end = block_start + NXFS_BLOCK_SIZE;
        uint32_t copy_start;
        uint32_t chunk;
        int phys = nxfs_logical_to_physical(inode, block_idx);

        if (phys < 0) {
            return -1;
        }
        if (nxfs_read_block(vol, (uint32_t)phys, block) != 0) {
            return -1;
        }
        if (!nxfs_calc_block_window(block_start,
                                    block_end,
                                    inode->size,
                                    offset,
                                    offset + buffer_size,
                                    &copy_start,
                                    &chunk)) {
            continue;
        }
        nxfs_mem_copy(out + done, block + (copy_start - block_start), chunk);
        done += chunk;
    }

    *bytes_read = done;
    return 0;
}

int nxfs_write_file_range(struct nxfs_volume *vol,
                          uint32_t inode_index,
                          struct nxfs_inode *inode,
                          uint32_t offset,
                          const void *buffer,
                          uint32_t buffer_size,
                          uint32_t *bytes_written) {
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t done = 0;
    uint32_t end;
    uint32_t need_blocks;

    if (vol == 0 || !vol->mounted || inode == 0 || buffer == 0 || bytes_written == 0) {
        return -1;
    }
    if (inode->type != NXFS_TYPE_FILE) {
        return -1;
    }
    if (buffer_size == 0) {
        *bytes_written = 0;
        return 0;
    }
    end = offset + buffer_size;
    if (end < offset) {
        return -1;
    }
    need_blocks = (end + NXFS_BLOCK_SIZE - 1u) / NXFS_BLOCK_SIZE;
    if (nxfs_ensure_blocks(vol, inode, need_blocks) != 0) {
        return -1;
    }

    for (uint32_t block_idx = 0; block_idx < nxfs_inode_block_count(inode) && done < buffer_size; block_idx++) {
        uint8_t block[NXFS_BLOCK_SIZE];
        uint32_t block_start = block_idx * NXFS_BLOCK_SIZE;
        uint32_t block_end = block_start + NXFS_BLOCK_SIZE;
        uint32_t copy_start;
        uint32_t chunk;

        if (nxfs_read_inode_block(vol, inode, block_idx, block) != 0) {
            return -1;
        }
        if (!nxfs_calc_block_window(block_start,
                                    block_end,
                                    end,
                                    offset,
                                    end,
                                    &copy_start,
                                    &chunk)) {
            continue;
        }
        nxfs_mem_copy(block + (copy_start - block_start), in + done, chunk);
        if (nxfs_write_inode_block(vol, inode, block_idx, block) != 0) {
            return -1;
        }
        done += chunk;
    }
    if (end > inode->size) {
        inode->size = end;
    }
    if (nxfs_write_inode(vol, inode_index, inode) != 0) {
        return -1;
    }
    *bytes_written = done;
    return 0;
}

int nxfs_unlink_root(struct nxfs_volume *vol, const char *name) {
    return nxfs_unlink_path(vol, name);
}

int nxfs_unlink_path(struct nxfs_volume *vol, const char *path) {
    struct nxfs_inode parent;
    struct nxfs_inode inode;
    uint32_t parent_ino = NXFS_ROOT_INODE;
    uint32_t inode_index;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[28];

    if (nxfs_resolve_parent_dir(vol, path, &parent_ino, &parent, parent_path, name) != 0) {
        return -1;
    }
    if (nxfs_lookup_path(vol, path, &inode_index, &inode) != 0 || inode.type != NXFS_TYPE_FILE) {
        return -1;
    }
    if (nxfs_free_all_extents(vol, &inode) != 0) {
        return -1;
    }
    nxfs_mem_set(&inode, 0, sizeof(inode));
    if (nxfs_write_inode(vol, inode_index, &inode) != 0) {
        return -1;
    }
    return nxfs_dir_remove_entry(vol, parent_ino, &parent, name);
}

int nxfs_rmdir_path(struct nxfs_volume *vol, const char *path) {
    struct nxfs_inode parent;
    struct nxfs_inode dir;
    uint32_t parent_ino = NXFS_ROOT_INODE;
    uint32_t inode_index;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[28];

    if (nxfs_resolve_parent_dir(vol, path, &parent_ino, &parent, parent_path, name) != 0) {
        return -1;
    }
    if (nxfs_lookup_path(vol, path, &inode_index, &dir) != 0 || dir.type != NXFS_TYPE_DIR || inode_index == NXFS_ROOT_INODE) {
        return -1;
    }
    if (!nxfs_dir_is_empty(vol, &dir)) {
        return -1;
    }
    if (nxfs_free_all_extents(vol, &dir) != 0) {
        return -1;
    }
    nxfs_mem_set(&dir, 0, sizeof(dir));
    if (nxfs_write_inode(vol, inode_index, &dir) != 0 ||
        nxfs_dir_remove_entry(vol, parent_ino, &parent, name) != 0) {
        return -1;
    }
    if (parent.nlink != 0) {
        parent.nlink--;
    }
    return nxfs_write_inode(vol, parent_ino, &parent);
}

int nxfs_truncate_inode(struct nxfs_volume *vol, uint32_t inode_index, struct nxfs_inode *inode) {
    if (vol == 0 || !vol->mounted || inode == 0 || inode->type != NXFS_TYPE_FILE) {
        return -1;
    }
    if (nxfs_free_all_extents(vol, inode) != 0) {
        return -1;
    }
    nxfs_mem_set(inode->extents, 0, sizeof(inode->extents));
    inode->size = 0;
    return nxfs_write_inode(vol, inode_index, inode);
}

int nxfs_truncate_path(struct nxfs_volume *vol, const char *path) {
    struct nxfs_inode inode;
    uint32_t inode_index;

    if (vol == 0 || !vol->mounted || path == 0 || path[0] == '\0') {
        return -1;
    }
    if (nxfs_lookup_path(vol, path, &inode_index, &inode) != 0 || inode.type != NXFS_TYPE_FILE) {
        return -1;
    }
    return nxfs_truncate_inode(vol, inode_index, &inode);
}
