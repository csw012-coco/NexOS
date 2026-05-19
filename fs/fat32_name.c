#include "fs/fat32_internal.h"

int fat32_update_dirent(struct fat32_volume *vol, const struct fat32_file *file) {
    struct fat32_dirent *dirent;

    if (vol == 0 || file == 0 || file->dirent_lba == 0 || file->dirent_offset + sizeof(struct fat32_dirent) > sizeof(vol->sector_buffer)) {
        return -1;
    }
    if (fat32_read_sector(vol, file->dirent_lba, vol->sector_buffer) != 0) {
        return -1;
    }
    dirent = (struct fat32_dirent *)(vol->sector_buffer + file->dirent_offset);
    dirent->first_cluster_high = (uint16_t)(file->first_cluster >> 16);
    dirent->first_cluster_low = (uint16_t)(file->first_cluster & 0xffffu);
    dirent->file_size = file->size;
    dirent->attr = file->attributes;
    return fat32_write_sector(vol, file->dirent_lba, vol->sector_buffer);
}

void fat32_format_name83(const uint8_t raw[11], char *out) {
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 8 && raw[i] != ' '; i++) {
        out[pos++] = (char)raw[i];
    }
    if (raw[8] != ' ') {
        out[pos++] = '.';
        for (uint32_t i = 8; i < 11 && raw[i] != ' '; i++) {
            out[pos++] = (char)raw[i];
        }
    }
    out[pos] = '\0';
}

int fat32_ascii_casecmp(const char *lhs, const char *rhs) {
    uint32_t i = 0;

    if (lhs == 0 || rhs == 0) {
        return lhs == rhs ? 0 : 1;
    }
    for (;;) {
        char a = to_lower(lhs[i]);
        char b = to_lower(rhs[i]);

        if (a != b) {
            return (int)((unsigned char)a - (unsigned char)b);
        }
        if (lhs[i] == '\0') {
            return 0;
        }
        i++;
    }
}

int fat32_is_space_or_dot(const char *text) {
    uint32_t i = 0;

    if (text == 0 || text[0] == '\0') {
        return 1;
    }
    while (text[i] != '\0') {
        if (text[i] != ' ' && text[i] != '.') {
            return 0;
        }
        i++;
    }
    return 1;
}

int fat32_short_name_char_allowed(char ch) {
    ch = to_upper(ch);
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '$' || ch == '%' || ch == '\'' || ch == '-' ||
           ch == '_' || ch == '@' || ch == '~' || ch == '`' ||
           ch == '!' || ch == '(' || ch == ')' || ch == '{' ||
           ch == '}' || ch == '^' || ch == '#' || ch == '&';
}

char fat32_sanitize_short_char(char ch) {
    ch = to_upper(ch);
    return fat32_short_name_char_allowed(ch) ? ch : '_';
}

void fat32_normalize_name83(const char *input, char out[12]) {
    uint32_t name_pos = 0;
    uint32_t ext_pos = 8;
    int in_ext = 0;

    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    out[11] = '\0';

    while (input != 0 && *input != '\0') {
        char ch = *input++;

        if (ch == '.') {
            if (in_ext) {
                continue;
            }
            in_ext = 1;
            continue;
        }
        if (!in_ext) {
            if (name_pos < 8u) {
                out[name_pos++] = fat32_sanitize_short_char(ch);
            }
        } else if (ext_pos < 11u) {
            out[ext_pos++] = fat32_sanitize_short_char(ch);
        }
    }
}

int fat32_name_fits_short_exact(const char *input) {
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    int in_ext = 0;
    int saw_char = 0;

    if (input == 0 || input[0] == '\0' || fat32_is_space_or_dot(input)) {
        return 0;
    }
    while (*input != '\0') {
        char ch = *input++;

        if (ch == '.') {
            if (in_ext) {
                return 0;
            }
            in_ext = 1;
            continue;
        }
        if (!fat32_short_name_char_allowed(ch)) {
            return 0;
        }
        saw_char = 1;
        if (!in_ext) {
            if (++base_len > 8u) {
                return 0;
            }
        } else {
            if (++ext_len > 3u) {
                return 0;
            }
        }
    }
    return saw_char && base_len != 0;
}

uint8_t fat32_short_name_checksum(const uint8_t raw[11]) {
    uint8_t sum = 0;

    for (uint32_t i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + raw[i]);
    }
    return sum;
}

void fat32_lfn_state_reset(struct fat32_lfn_state *state) {
    if (state == 0) {
        return;
    }
    mem_set(state->name, 0, sizeof(state->name));
    state->valid = 0;
    state->checksum = 0;
    state->next_order = 0;
    state->total_entries = 0;
}

int fat32_lfn_append_char(char *dst, uint32_t dst_size, uint32_t index, uint16_t value) {
    char ch;

    if (dst == 0 || dst_size == 0 || index + 1u >= dst_size) {
        return 0;
    }
    if (value == 0x0000u) {
        dst[index] = '\0';
        return 1;
    }
    if (value == 0xffffu) {
        if (dst[index] == '\0') {
            return 1;
        }
        dst[index] = '\0';
        return 1;
    }
    ch = (value & 0xff00u) == 0 ? (char)(value & 0x00ffu) : '?';
    if (ch == '\0') {
        dst[index] = '\0';
        return 1;
    }
    dst[index] = ch;
    dst[index + 1u] = '\0';
    return 1;
}

int fat32_lfn_capture(struct fat32_lfn_state *state, const struct fat32_lfn_dirent *lfn) {
    uint8_t order;
    uint32_t base;

    if (state == 0 || lfn == 0 || lfn->attr != FAT32_ATTR_LFN) {
        fat32_lfn_state_reset(state);
        return 0;
    }
    order = (uint8_t)(lfn->order & 0x1fu);
    if (order == 0u || order > 20u || lfn->type != 0u) {
        fat32_lfn_state_reset(state);
        return 0;
    }
    if ((lfn->order & 0x40u) != 0u) {
        fat32_lfn_state_reset(state);
        state->valid = 1u;
        state->checksum = lfn->checksum;
        state->next_order = order;
        state->total_entries = order;
    }
    if (!state->valid || state->checksum != lfn->checksum || state->next_order != order) {
        fat32_lfn_state_reset(state);
        return 0;
    }

    base = (uint32_t)(order - 1u) * 13u;
    for (uint32_t i = 0; i < 5u; i++) {
        if (!fat32_lfn_append_char(state->name, sizeof(state->name), base + i, lfn->name1[i])) {
            fat32_lfn_state_reset(state);
            return 0;
        }
    }
    for (uint32_t i = 0; i < 6u; i++) {
        if (!fat32_lfn_append_char(state->name, sizeof(state->name), base + 5u + i, lfn->name2[i])) {
            fat32_lfn_state_reset(state);
            return 0;
        }
    }
    for (uint32_t i = 0; i < 2u; i++) {
        if (!fat32_lfn_append_char(state->name, sizeof(state->name), base + 11u + i, lfn->name3[i])) {
            fat32_lfn_state_reset(state);
            return 0;
        }
    }

    if (state->next_order > 0u) {
        state->next_order--;
    }
    return 1;
}

void fat32_file_reset(struct fat32_file *file) {
    if (file == 0) {
        return;
    }
    file->name[0] = '\0';
    file->first_cluster = 0;
    file->size = 0;
    file->attributes = 0;
    file->lfn_entry_count = 0;
    file->reserved = 0;
    file->dirent_index = 0;
    file->dirent_lba = 0;
    file->dirent_offset = 0;
}

void fat32_copy_name(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int fat32_fill_file_from_dirent(const struct fat32_dirent *dirent,
                                       const struct fat32_lfn_state *lfn_state,
                                       struct fat32_file *out) {
    uint32_t cluster;

    if (dirent == 0 || out == 0 || fat32_is_end_of_dirent(dirent) ||
        fat32_is_deleted_dirent(dirent) || fat32_is_lfn_dirent(dirent) || fat32_is_volume_dirent(dirent)) {
        return 0;
    }

    fat32_file_reset(out);
    cluster = ((uint32_t)dirent->first_cluster_high << 16) | dirent->first_cluster_low;
    if (lfn_state != 0 && lfn_state->valid && lfn_state->next_order == 0u &&
        lfn_state->name[0] != '\0' && lfn_state->checksum == fat32_short_name_checksum(dirent->name)) {
        mem_copy(out->name, lfn_state->name, sizeof(out->name));
        out->lfn_entry_count = lfn_state->total_entries;
    } else {
        fat32_format_name83(dirent->name, out->name);
        out->lfn_entry_count = 0;
    }
    out->first_cluster = cluster;
    out->size = dirent->file_size;
    out->attributes = dirent->attr;
    return 1;
}

int fat32_match_dirent_name(const struct fat32_dirent *dirent,
                                   const struct fat32_lfn_state *lfn_state,
                                   const char *input) {
    struct fat32_file file;

    if (input == 0 || !fat32_fill_file_from_dirent(dirent, lfn_state, &file)) {
        return 0;
    }
    if (fat32_ascii_casecmp(file.name, input) == 0) {
        return 1;
    }
    fat32_format_name83(dirent->name, file.name);
    return fat32_ascii_casecmp(file.name, input) == 0;
}

void fat32_make_short_alias(const char *input, uint8_t out[11], uint32_t serial) {
    char base[9];
    char ext[4];
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    const char *last_dot = 0;
    const char *cursor = input;
    char serial_text[8];
    uint32_t serial_digits = 0;
    uint32_t serial_value = serial;

    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    while (cursor != 0 && *cursor != '\0') {
        if (*cursor == '.') {
            last_dot = cursor;
        }
        cursor++;
    }
    cursor = input;
    while (cursor != 0 && *cursor != '\0' && cursor != last_dot) {
        char ch = fat32_sanitize_short_char(*cursor++);

        if (ch != ' ' && ch != '.' && base_len + 1u < sizeof(base)) {
            base[base_len++] = ch;
        }
    }
    base[base_len] = '\0';
    if (last_dot != 0 && last_dot[1] != '\0') {
        cursor = last_dot + 1;
        while (*cursor != '\0' && ext_len + 1u < sizeof(ext)) {
            char ch = fat32_sanitize_short_char(*cursor++);

            if (ch != ' ' && ch != '.') {
                ext[ext_len++] = ch;
            }
        }
    }
    ext[ext_len] = '\0';

    if (serial == 0u && fat32_name_fits_short_exact(input)) {
        char normalized[12];

        fat32_normalize_name83(input, normalized);
        mem_copy(out, normalized, 11u);
        return;
    }

    if (base_len == 0u) {
        base[base_len++] = '_';
        base[base_len] = '\0';
    }
    do {
        serial_text[serial_digits++] = (char)('0' + (serial_value % 10u));
        serial_value /= 10u;
    } while (serial_value != 0u && serial_digits < sizeof(serial_text));

    {
        uint32_t prefix_len = 8u - (serial_digits + 1u);
        uint32_t pos = 0;

        if (prefix_len > base_len) {
            prefix_len = base_len;
        }
        while (pos < prefix_len) {
            out[pos] = (uint8_t)base[pos];
            pos++;
        }
        out[pos++] = '~';
        while (serial_digits > 0u && pos < 8u) {
            out[pos++] = (uint8_t)serial_text[--serial_digits];
        }
    }
    for (uint32_t i = 0; i < ext_len && i < 3u; i++) {
        out[8u + i] = (uint8_t)ext[i];
    }
}
