#include "fs/fat32_internal.h"

uint32_t fat32_dir_first_cluster(struct fat32_volume *vol, const struct fat32_file *dir) {
    if (vol == 0) {
        return 0;
    }
    if (dir == 0 || !fat32_cluster_is_data(vol, dir->first_cluster)) {
        return vol->root_cluster;
    }
    return dir->first_cluster;
}

int fat32_list_directory(struct fat32_volume *vol,
                                const struct fat32_file *dir,
                                struct fat32_file *entries,
                                uint32_t max_entries,
                                uint32_t *entry_count) {
    uint32_t cluster;
    uint32_t out_count = 0;
    uint32_t raw_index = 0;
    struct fat32_lfn_state lfn_state;
    uint8_t dir_sector[512];

    if (vol == 0 || !vol->mounted || entries == 0 || entry_count == 0) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    fat32_lfn_state_reset(&lfn_state);
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);
        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, dir_sector) != 0) {
                return -1;
            }
            dirents = (struct fat32_dirent *)dir_sector;
            for (uint32_t i = 0; i < 16; i++) {
                if (fat32_is_end_of_dirent(&dirents[i])) {
                    *entry_count = out_count;
                    return 0;
                }
                if (fat32_is_deleted_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (fat32_is_lfn_dirent(&dirents[i])) {
                    (void)fat32_lfn_capture(&lfn_state, (const struct fat32_lfn_dirent *)&dirents[i]);
                    raw_index++;
                    continue;
                }
                if (fat32_is_volume_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (out_count < max_entries &&
                    fat32_fill_file_from_dirent(&dirents[i], &lfn_state, &entries[out_count])) {
                    entries[out_count].dirent_index = raw_index;
                    entries[out_count].dirent_lba = cluster_lba + sec;
                    entries[out_count].dirent_offset = i * sizeof(struct fat32_dirent);
                    out_count++;
                }
                fat32_lfn_state_reset(&lfn_state);
                raw_index++;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    *entry_count = out_count;
    return 0;
}

int fat32_find_in_directory(struct fat32_volume *vol,
                                   const struct fat32_file *dir,
                                   const char *name,
                                   struct fat32_file *out) {
    uint32_t cluster;
    uint32_t raw_index = 0;
    struct fat32_lfn_state lfn_state;
    uint8_t dir_sector[512];

    if (vol == 0 || !vol->mounted || name == 0 || out == 0) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    fat32_lfn_state_reset(&lfn_state);
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);
        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, dir_sector) != 0) {
                return -1;
            }
            dirents = (struct fat32_dirent *)dir_sector;
            for (uint32_t i = 0; i < 16; i++) {
                if (fat32_is_end_of_dirent(&dirents[i])) {
                    return -1;
                }
                if (fat32_is_deleted_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (fat32_is_lfn_dirent(&dirents[i])) {
                    (void)fat32_lfn_capture(&lfn_state, (const struct fat32_lfn_dirent *)&dirents[i]);
                    raw_index++;
                    continue;
                }
                if (fat32_is_volume_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (fat32_match_dirent_name(&dirents[i], &lfn_state, name)) {
                    fat32_fill_file_from_dirent(&dirents[i], &lfn_state, out);
                    out->dirent_index = raw_index;
                    out->dirent_lba = cluster_lba + sec;
                    out->dirent_offset = i * sizeof(struct fat32_dirent);
                    return 0;
                }
                fat32_lfn_state_reset(&lfn_state);
                raw_index++;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

uint32_t fat32_name_length(const char *text) {
    uint32_t len = 0;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

int fat32_name_needs_lfn(const char *name) {
    uint32_t i = 0;

    if (!fat32_name_fits_short_exact(name)) {
        return 1;
    }
    while (name != 0 && name[i] != '\0') {
        if (name[i] >= 'a' && name[i] <= 'z') {
            return 1;
        }
        i++;
    }
    return 0;
}

uint32_t fat32_lfn_entry_count_for_name(const char *name) {
    uint32_t len = fat32_name_length(name);

    return len == 0u ? 0u : (len + 12u) / 13u;
}

int fat32_short_name_exists(struct fat32_volume *vol,
                                   const struct fat32_file *dir,
                                   const uint8_t raw_name[11]) {
    uint32_t cluster;

    if (vol == 0 || !vol->mounted || raw_name == 0) {
        return 0;
    }
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);

        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
                return 0;
            }
            dirents = (struct fat32_dirent *)vol->sector_buffer;
            for (uint32_t i = 0; i < 16u; i++) {
                if (fat32_is_end_of_dirent(&dirents[i])) {
                    return 0;
                }
                if (fat32_is_deleted_dirent(&dirents[i]) || fat32_is_lfn_dirent(&dirents[i]) ||
                    fat32_is_volume_dirent(&dirents[i])) {
                    continue;
                }
                {
                    uint32_t match = 1u;

                    for (uint32_t j = 0; j < 11u; j++) {
                        if (dirents[i].name[j] != raw_name[j]) {
                            match = 0u;
                            break;
                        }
                    }
                    if (match) {
                        return 1;
                    }
                }
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return 0;
        }
    }
    return 0;
}

int fat32_build_unique_short_name(struct fat32_volume *vol,
                                         const struct fat32_file *dir,
                                         const char *name,
                                         uint8_t raw_name[11]) {
    for (uint32_t serial = 0; serial < 1000000u; serial++) {
        fat32_make_short_alias(name, raw_name, serial);
        if (!fat32_short_name_exists(vol, dir, raw_name)) {
            return 0;
        }
    }
    return -1;
}

int fat32_locate_free_dirent_span(struct fat32_volume *vol,
                                         const struct fat32_file *dir,
                                         uint32_t needed,
                                         uint32_t lbas_out[],
                                         uint32_t offsets_out[],
                                         uint32_t *raw_index_out) {
    uint32_t cluster;
    uint32_t raw_index = 0;
    uint32_t run = 0;

    if (vol == 0 || !vol->mounted || needed == 0u || lbas_out == 0 || offsets_out == 0 || raw_index_out == 0) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);

        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
                return -1;
            }
            dirents = (struct fat32_dirent *)vol->sector_buffer;
            for (uint32_t i = 0; i < 16u; i++, raw_index++) {
                if (fat32_is_end_of_dirent(&dirents[i]) || fat32_is_deleted_dirent(&dirents[i])) {
                    lbas_out[run] = cluster_lba + sec;
                    offsets_out[run] = i * sizeof(struct fat32_dirent);
                    run++;
                    if (run >= needed) {
                        *raw_index_out = raw_index - needed + 1u;
                        return 0;
                    }
                    continue;
                }
                run = 0;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

int fat32_append_directory_cluster(struct fat32_volume *vol, const struct fat32_file *dir) {
    uint32_t cluster;
    uint32_t next = FAT32_CLUSTER_END;
    uint32_t new_cluster = 0;

    if (vol == 0 || !vol->mounted) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    cluster = fat32_dir_first_cluster(vol, dir);
    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }

    for (;;) {
        if (fat32_next_cluster(vol, cluster, &next) != 0) {
            return -1;
        }
        if (next >= FAT32_CLUSTER_END) {
            break;
        }
        if (!fat32_cluster_is_data(vol, next)) {
            return -1;
        }
        cluster = next;
    }

    if (fat32_alloc_cluster(vol, &new_cluster) != 0) {
        return -1;
    }
    if (fat32_write_fat_entry(vol, cluster, new_cluster) != 0) {
        (void)fat32_write_fat_entry(vol, new_cluster, 0u);
        return -1;
    }
    return 0;
}

int fat32_append_file_cluster(struct fat32_volume *vol,
                                     uint32_t cluster,
                                     uint32_t *new_cluster_out) {
    uint32_t new_cluster = 0u;

    if (vol == 0 || !vol->mounted || new_cluster_out == 0) {
        return -1;
    }
    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    if (fat32_alloc_cluster(vol, &new_cluster) != 0) {
        return -1;
    }
    if (fat32_write_fat_entry(vol, cluster, new_cluster) != 0) {
        (void)fat32_write_fat_entry(vol, new_cluster, 0u);
        return -1;
    }
    *new_cluster_out = new_cluster;
    return 0;
}

int fat32_write_raw_dirent(struct fat32_volume *vol,
                                  uint32_t lba,
                                  uint32_t offset,
                                  const void *dirent,
                                  uint32_t size) {
    if (vol == 0 || dirent == 0 || offset + size > sizeof(vol->sector_buffer)) {
        return -1;
    }
    if (fat32_read_sector(vol, lba, vol->sector_buffer) != 0) {
        return -1;
    }
    mem_copy(vol->sector_buffer + offset, dirent, size);
    return fat32_write_sector(vol, lba, vol->sector_buffer);
}

void fat32_populate_lfn_dirent(struct fat32_lfn_dirent *lfn,
                                      const char *name,
                                      uint32_t total_entries,
                                      uint32_t order,
                                      uint8_t checksum) {
    uint32_t start;
    uint32_t len = fat32_name_length(name);

    mem_set(lfn, 0xff, sizeof(*lfn));
    lfn->order = (uint8_t)order;
    if (order == total_entries) {
        lfn->order |= 0x40u;
    }
    lfn->attr = FAT32_ATTR_LFN;
    lfn->type = 0u;
    lfn->checksum = checksum;
    lfn->first_cluster_low = 0u;

    start = (order - 1u) * 13u;
    for (uint32_t i = 0; i < 13u; i++) {
        uint32_t name_index = start + i;
        uint16_t value;

        if (name_index < len) {
            value = (uint16_t)(uint8_t)name[name_index];
        } else if (name_index == len) {
            value = 0x0000u;
        } else {
            value = 0xffffu;
        }
        if (i < 5u) {
            lfn->name1[i] = value;
        } else if (i < 11u) {
            lfn->name2[i - 5u] = value;
        } else {
            lfn->name3[i - 11u] = value;
        }
    }
}

void fat32_set_dirent_cluster(struct fat32_dirent *dirent, uint32_t cluster) {
    dirent->first_cluster_high = (uint16_t)(cluster >> 16);
    dirent->first_cluster_low = (uint16_t)(cluster & 0xffffu);
}

int fat32_create_entry_in_directory(struct fat32_volume *vol,
                                           const struct fat32_file *dir,
                                           const char *name,
                                           uint8_t attributes,
                                           uint32_t first_cluster,
                                           struct fat32_file *out) {
    uint8_t raw_short_name[11];
    struct fat32_dirent dirent;
    struct fat32_lfn_dirent lfn_dirent;
    uint32_t lbas[21];
    uint32_t offsets[21];
    uint32_t raw_index = 0;
    uint32_t lfn_count;
    uint32_t needed;
    uint8_t checksum;
    int span_rc;

    if (vol == 0 || !vol->mounted || name == 0 || out == 0) {
        return -1;
    }
    if (fat32_find_in_directory(vol, dir, name, out) == 0) {
        return -1;
    }
    if (fat32_build_unique_short_name(vol, dir, name, raw_short_name) != 0) {
        return -1;
    }
    lfn_count = fat32_name_needs_lfn(name) ? fat32_lfn_entry_count_for_name(name) : 0u;
    needed = lfn_count + 1u;
    if (lfn_count > 20u) {
        return -1;
    }
    span_rc = fat32_locate_free_dirent_span(vol, dir, needed, lbas, offsets, &raw_index);
    if (span_rc != 0) {
        if (fat32_append_directory_cluster(vol, dir) != 0 ||
            fat32_locate_free_dirent_span(vol, dir, needed, lbas, offsets, &raw_index) != 0) {
            return -1;
        }
    }
    mem_set(&dirent, 0, sizeof(dirent));
    mem_copy(dirent.name, raw_short_name, sizeof(dirent.name));
    fat32_set_dirent_cluster(&dirent, first_cluster);
    dirent.file_size = 0;
    dirent.attr = attributes;
    checksum = fat32_short_name_checksum(raw_short_name);

    for (uint32_t i = 0; i < lfn_count; i++) {
        uint32_t order = lfn_count - i;

        fat32_populate_lfn_dirent(&lfn_dirent, name, lfn_count, order, checksum);
        if (fat32_write_raw_dirent(vol, lbas[i], offsets[i], &lfn_dirent, sizeof(lfn_dirent)) != 0) {
            return -1;
        }
    }
    if (fat32_write_raw_dirent(vol, lbas[lfn_count], offsets[lfn_count], &dirent, sizeof(dirent)) != 0) {
        return -1;
    }
    fat32_file_reset(out);
    if (lfn_count != 0u) {
        fat32_copy_name(out->name, sizeof(out->name), name);
        out->lfn_entry_count = (uint8_t)lfn_count;
    } else {
        fat32_format_name83(raw_short_name, out->name);
    }
    out->first_cluster = first_cluster;
    out->size = 0;
    out->attributes = attributes;
    out->dirent_index = raw_index + lfn_count;
    out->dirent_lba = lbas[lfn_count];
    out->dirent_offset = offsets[lfn_count];
    return 0;
}

int fat32_raw_entry_position(struct fat32_volume *vol,
                                    const struct fat32_file *dir,
                                    uint32_t raw_index,
                                    uint32_t *lba_out,
                                    uint32_t *offset_out) {
    uint32_t cluster;
    uint32_t current = 0;

    if (vol == 0 || !vol->mounted || lba_out == 0 || offset_out == 0) {
        return -1;
    }
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);

        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            for (uint32_t i = 0; i < 16u; i++, current++) {
                if (current == raw_index) {
                    *lba_out = cluster_lba + sec;
                    *offset_out = i * sizeof(struct fat32_dirent);
                    return 0;
                }
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }
    return -1;
}

int fat32_delete_entry_chain(struct fat32_volume *vol,
                                    const struct fat32_file *dir,
                                    const struct fat32_file *file) {
    uint32_t total = (uint32_t)file->lfn_entry_count + 1u;

    for (uint32_t i = 0; i < total; i++) {
        uint32_t raw_index = file->dirent_index - i;
        uint32_t lba;
        uint32_t offset;
        struct fat32_dirent *dirent;

        if (fat32_raw_entry_position(vol, dir, raw_index, &lba, &offset) != 0 ||
            fat32_read_sector(vol, lba, vol->sector_buffer) != 0) {
            return -1;
        }
        dirent = (struct fat32_dirent *)(vol->sector_buffer + offset);
        dirent->name[0] = 0xe5;
        if (i == 0u) {
            dirent->file_size = 0u;
            fat32_set_dirent_cluster(dirent, 0u);
        }
        if (fat32_write_sector(vol, lba, vol->sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

int fat32_init_directory_cluster(struct fat32_volume *vol, uint32_t self_cluster, uint32_t parent_cluster) {
    struct fat32_dirent *dirents;

    if (vol == 0 || !vol->mounted || !fat32_cluster_is_data(vol, self_cluster)) {
        return -1;
    }
    if (fat32_read_sector(vol, fat32_cluster_lba(vol, self_cluster), vol->sector_buffer) != 0) {
        return -1;
    }
    dirents = (struct fat32_dirent *)vol->sector_buffer;
    mem_set(dirents, 0, sizeof(struct fat32_dirent) * 2u);

    mem_set(dirents[0].name, ' ', sizeof(dirents[0].name));
    dirents[0].name[0] = '.';
    dirents[0].attr = FAT32_ATTR_DIRECTORY;
    fat32_set_dirent_cluster(&dirents[0], self_cluster);

    mem_set(dirents[1].name, ' ', sizeof(dirents[1].name));
    dirents[1].name[0] = '.';
    dirents[1].name[1] = '.';
    dirents[1].attr = FAT32_ATTR_DIRECTORY;
    fat32_set_dirent_cluster(&dirents[1], fat32_cluster_is_data(vol, parent_cluster) ? parent_cluster : vol->root_cluster);

    return fat32_write_sector(vol, fat32_cluster_lba(vol, self_cluster), vol->sector_buffer);
}

int fat32_free_cluster_chain(struct fat32_volume *vol, uint32_t first_cluster) {
    uint32_t cluster = first_cluster;

    if (vol == 0 || !vol->mounted || !fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t next = FAT32_CLUSTER_END;

        if (fat32_read_fat_entry(vol, cluster, &next) != 0) {
            return -1;
        }
        if (fat32_write_fat_entry(vol, cluster, 0) != 0) {
            return -1;
        }
        if (next >= FAT32_CLUSTER_END) {
            break;
        }
        if (!fat32_cluster_is_data(vol, next)) {
            return -1;
        }
        cluster = next;
    }
    return 0;
}

int fat32_split_parent_child_path(const char *path,
                                         char *parent,
                                         uint32_t parent_size,
                                         char *name,
                                         uint32_t name_size) {
    uint32_t len = 0;
    uint32_t end;
    uint32_t slash = 0xffffffffu;
    uint32_t i;
    uint32_t parent_len;
    uint32_t name_len;

    if (path == 0 || parent == 0 || name == 0 || parent_size == 0 || name_size == 0) {
        return -1;
    }
    while (path[len] != '\0') {
        len++;
    }
    while (len != 0u && path[len - 1u] == '/') {
        len--;
    }
    if (len == 0u) {
        return -1;
    }
    end = len;
    for (i = 0; i < end; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    parent[0] = '\0';
    name[0] = '\0';
    if (slash == 0xffffffffu) {
        name_len = end;
        if (name_len + 1u > name_size) {
            return -1;
        }
        mem_copy(name, path, name_len);
        name[name_len] = '\0';
        return 0;
    }

    parent_len = slash;
    name_len = end - (slash + 1u);
    if (name_len == 0u || parent_len + 1u > parent_size || name_len + 1u > name_size) {
        return -1;
    }
    if (parent_len == 0u) {
        parent[0] = '\0';
    } else {
        mem_copy(parent, path, parent_len);
        parent[parent_len] = '\0';
    }
    mem_copy(name, path + slash + 1u, name_len);
    name[name_len] = '\0';
    return 0;
}

int fat32_is_directory_empty(struct fat32_volume *vol, const struct fat32_file *dir) {
    struct fat32_file entry;

    if (vol == 0 || dir == 0 || (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return 0;
    }
    for (uint32_t index = 0; fat32_get_dir_entry(vol, dir, index, &entry) == 0; index++) {
        if (entry.name[0] == '.' &&
            (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) {
            continue;
        }
        return 0;
    }
    return 1;
}

int fat32_path_next_segment(const char **path_io, char segment[NOS_NAME_BUFFER_SIZE]) {
    const char *path = *path_io;
    uint32_t pos = 0;

    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        return 0;
    }
    while (*path != '\0' && *path != '/') {
        if (pos + 1u >= NOS_NAME_BUFFER_SIZE) {
            return -1;
        }
        segment[pos++] = *path++;
    }
    segment[pos] = '\0';
    while (*path == '/') {
        path++;
    }
    *path_io = path;
    return 1;
}
