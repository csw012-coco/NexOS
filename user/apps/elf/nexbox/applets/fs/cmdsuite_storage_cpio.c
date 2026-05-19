#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"

enum {
    CPIO_NEWC_HEADER_SIZE = 110u,
    CPIO_MODE_DIR = 0040755u,
    CPIO_MODE_FILE = 0100644u
};

static uint32_t cpio_align4_local(uint32_t value) {
    return (4u - (value & 3u)) & 3u;
}

static void cpio_put_hex8_local(char *dst, uint32_t value) {
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 7; i >= 0; i--) {
        dst[i] = hex[value & 0x0fu];
        value >>= 4;
    }
}

static int cpio_parse_hex8_local(const char *src, uint32_t *value_out) {
    uint32_t value = 0u;
    uint32_t i;

    if (src == NULL || value_out == NULL) {
        return 0;
    }
    for (i = 0; i < 8u; i++) {
        char ch = src[i];
        uint32_t nibble;

        if (ch >= '0' && ch <= '9') {
            nibble = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = 10u + (uint32_t)(ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            nibble = 10u + (uint32_t)(ch - 'A');
        } else {
            return 0;
        }
        value = (value << 4) | nibble;
    }
    *value_out = value;
    return 1;
}

static int cpio_write_all_local(int fd, const void *buf, uint32_t bytes) {
    return (uint32_t)write(fd, buf, bytes) == bytes;
}

static int cpio_read_all_local(int fd, void *buf, uint32_t bytes) {
    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0u;

    while (done < bytes) {
        uint32_t got = (uint32_t)read(fd, dst + done, bytes - done);

        if (got == 0u) {
            return 0;
        }
        done += got;
    }
    return 1;
}

static int cpio_skip_bytes_local(int fd, uint32_t bytes) {
    uint8_t scratch[64];
    uint32_t left = bytes;

    while (left > 0u) {
        uint32_t want = left > sizeof(scratch) ? (uint32_t)sizeof(scratch) : left;

        if (!cpio_read_all_local(fd, scratch, want)) {
            return 0;
        }
        left -= want;
    }
    return 1;
}

static int cpio_write_pad_local(int fd, uint32_t count) {
    static const uint8_t zeros[4] = {0u, 0u, 0u, 0u};

    if (count == 0u) {
        return 1;
    }
    return cpio_write_all_local(fd, zeros, count);
}

static int cpio_is_dot_entry_local(const char *name) {
    return streq_local(name, ".") || streq_local(name, "..");
}

static const char *cpio_path_basename_local(const char *path) {
    const char *last = path;
    uint32_t i = 0u;

    if (path == NULL || path[0] == '\0') {
        return "";
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            last = path + i + 1u;
        }
        i++;
    }
    return *last != '\0' ? last : path;
}

static void cpio_normalize_name_local(const char *path, char *out, uint32_t out_size) {
    uint32_t src = 0u;
    uint32_t dst = 0u;

    if (out == NULL || out_size == 0u) {
        return;
    }
    while (path != NULL && path[src] == '/') {
        src++;
    }
    if (path == NULL || path[src] == '\0') {
        copy_line_local(out, ".", out_size);
        return;
    }
    while (path[src] != '\0' && dst + 1u < out_size) {
        out[dst++] = path[src++];
    }
    out[dst] = '\0';
}

static int cpio_join_local(char *out, uint32_t out_size, const char *dir, const char *name) {
    if (out == NULL || out_size == 0u || dir == NULL || name == NULL) {
        return 0;
    }
    if (streq_local(dir, ".") || dir[0] == '\0') {
        copy_line_local(out, name, out_size);
        return out[0] != '\0';
    }
    if (streq_local(dir, "/")) {
        if (snprintf(out, out_size, "/%s", name) < 0) {
            return 0;
        }
    } else if (snprintf(out, out_size, "%s/%s", dir, name) < 0) {
        return 0;
    }
    return 1;
}

static int cpio_split_parent_local(const char *path, char *parent, uint32_t parent_size, char *name, uint32_t name_size) {
    uint32_t len;
    uint32_t i;
    uint32_t cut = 0u;

    if (path == NULL || path[0] == '\0' || parent == NULL || name == NULL) {
        return 0;
    }
    len = str_len_local(path);
    while (len > 1u && path[len - 1u] == '/') {
        len--;
    }
    for (i = 0; i < len; i++) {
        if (path[i] == '/') {
            cut = i;
        }
    }
    if (cut == 0u) {
        copy_line_local(parent, path[0] == '/' ? "/" : ".", parent_size);
        copy_line_local(name, path[0] == '/' ? path + 1u : path, name_size);
        return name[0] != '\0';
    }
    if (cut >= parent_size || len - cut >= name_size) {
        return 0;
    }
    for (i = 0; i < cut; i++) {
        parent[i] = path[i];
    }
    parent[cut] = '\0';
    copy_line_local(name, path + cut + 1u, name_size);
    return name[0] != '\0';
}

static int cpio_join_absolute_local(char *out, uint32_t out_size, const char *path) {
    char cwd[CMD_PATH_MAX];

    if (out == NULL || out_size == 0u || path == NULL || path[0] == '\0') {
        return 0;
    }
    if (path[0] == '/') {
        copy_line_local(out, path, out_size);
        return out[0] != '\0';
    }
    if (getcwd(cwd, sizeof(cwd)) < 0) {
        copy_line_local(cwd, ".", sizeof(cwd));
    }
    if (streq_local(cwd, "/")) {
        return snprintf(out, out_size, "/%s", path) >= 0;
    }
    return snprintf(out, out_size, "%s/%s", cwd, path) >= 0;
}

static int cpio_path_matches_mount_target_local(const char *path, const char *target) {
    uint32_t target_len;

    if (path == NULL || target == NULL || target[0] == '\0') {
        return 0;
    }
    if (path[0] != '/') {
        return 0;
    }
    target_len = str_len_local(target);
    if (path[1] == '\0') {
        return 0;
    }
    for (uint32_t i = 0; i < target_len; i++) {
        if (path[1u + i] != target[i]) {
            return 0;
        }
    }
    return path[1u + target_len] == '\0' || path[1u + target_len] == '/';
}

static int cpio_path_is_fat_mount_local(const char *path) {
    struct syscall_mount_info info;
    char absolute[CMD_PATH_MAX];

    if (!cpio_join_absolute_local(absolute, sizeof(absolute), path)) {
        return 0;
    }
    for (uint32_t i = 0; mount_query(i, &info) > 0; i++) {
        if (info.kind == NEX_MOUNT_INFO_FAT32 &&
            cpio_path_matches_mount_target_local(absolute, info.target)) {
            return 1;
        }
    }
    return 0;
}

static int cpio_query_path_local(const char *path, uint32_t *attributes_out, uint32_t *size_out) {
    struct syscall_dirent entry;
    char parent[CMD_PATH_MAX];
    char name[CMD_PATH_MAX];
    int ignore_case;
    int fd;

    fd = opendir(path);
    if (fd >= 0) {
        close((uint32_t)fd);
        if (attributes_out != NULL) {
            *attributes_out = 0x10u;
        }
        if (size_out != NULL) {
            *size_out = 0u;
        }
        return 1;
    }
    if (!cpio_split_parent_local(path, parent, sizeof(parent), name, sizeof(name))) {
        return 0;
    }
    fd = opendir(parent);
    if (fd < 0) {
        return 0;
    }
    ignore_case = cpio_path_is_fat_mount_local(parent);
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (streq_local(entry.name, name) ||
            (ignore_case && streq_ignore_case_local(entry.name, name))) {
            close((uint32_t)fd);
            if (attributes_out != NULL) {
                *attributes_out = entry.attributes;
            }
            if (size_out != NULL) {
                *size_out = entry.size;
            }
            return 1;
        }
    }
    close((uint32_t)fd);
    return 0;
}

static const char *stat_type_name_local(uint32_t attributes) {
    return (attributes & 0x10u) != 0u ? "directory" : "file";
}

static void stat_print_table_field_local(const char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    write_str("\"");
    while (text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            char slash = '\\';
            write_stdout(&slash, 1);
        }
        write_stdout(&text[i], 1);
        i++;
    }
    write_str("\"");
}

enum {
    DU_MAX_DEPTH = 24u
};

struct du_options {
    int all;
    int summary;
    int table;
};

static void du_print_size_local(uint64_t size, const char *path, uint32_t attributes, const struct du_options *opts) {
    if (opts->table) {
        stat_print_table_field_local(path);
        write_str(" ");
        storage_write_u64_dec(size);
        write_str(" ");
        write_str(stat_type_name_local(attributes));
        write_str("\n");
        return;
    }
    write_human_size(size);
    write_str("\t");
    write_str(path);
    write_str("\n");
}

static int du_walk_local(const char *path, const struct du_options *opts, uint32_t depth, uint64_t *size_out) {
    struct syscall_dirent entry;
    uint32_t attributes = 0u;
    uint32_t file_size = 0u;
    uint64_t total = 0u;
    int fd;

    if (path == NULL || opts == NULL || size_out == NULL) {
        return 0;
    }
    if (!cpio_query_path_local(path, &attributes, &file_size)) {
        write_err_str("du: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    if ((attributes & 0x10u) == 0u) {
        total = file_size;
        *size_out = total;
        if (opts->all || depth == 0u) {
            du_print_size_local(total, path, attributes, opts);
        }
        return 1;
    }
    if (depth >= DU_MAX_DEPTH) {
        write_err_str("du: depth limit: ");
        write_err_str(path);
        write_err_str("\n");
        *size_out = 0u;
        return 1;
    }

    fd = opendir(path);
    if (fd < 0) {
        write_err_str("du: cannot open: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char child[CMD_PATH_MAX];
        uint64_t child_size = 0u;

        if (cpio_is_dot_entry_local(entry.name)) {
            continue;
        }
        if (!cpio_join_local(child, sizeof(child), path, entry.name)) {
            write_err_str("du: path too long\n");
            continue;
        }
        if (!du_walk_local(child, opts, depth + 1u, &child_size)) {
            close((uint32_t)fd);
            return 0;
        }
        total += child_size;
    }
    close((uint32_t)fd);
    *size_out = total;
    if (!opts->summary || depth == 0u) {
        du_print_size_local(total, path, attributes, opts);
    }
    return 1;
}

int cmd_du(int argc, char **argv) {
    struct du_options opts;
    const char *path = ".";
    int path_set = 0;
    uint64_t total = 0u;

    opts.all = 0;
    opts.summary = 0;
    opts.table = 0;
    for (int i = 1; i < argc; i++) {
        if (streq_local(argv[i], "-a")) {
            opts.all = 1;
        } else if (streq_local(argv[i], "-s")) {
            opts.summary = 1;
        } else if (streq_local(argv[i], "--table")) {
            opts.table = 1;
        } else if (!path_set) {
            path = argv[i];
            path_set = 1;
        } else {
            write_err_usage("du", " [-a] [-s] [--table] [path]\n");
            return 1;
        }
    }
    if (opts.summary) {
        opts.all = 0;
    }
    if (opts.table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path size type\n");
    }
    return du_walk_local(path, &opts, 0u, &total) ? 0 : 1;
}

static void tree_print_prefix_local(const uint8_t *last_stack, uint32_t depth) {
    uint32_t i;

    for (i = 1u; i < depth; i++) {
        write_str(last_stack[i] ? "   " : "|  ");
    }
}

static int tree_walk_local(const char *path, uint32_t depth, int table, uint8_t *last_stack, int is_last) {
    struct syscall_dirent entry;
    uint32_t entry_count = 0u;
    uint32_t entry_index = 0u;
    uint32_t attributes = 0u;
    uint32_t size = 0u;
    int fd;

    if (!cpio_query_path_local(path, &attributes, &size)) {
        write_err_str("tree: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    if (table) {
        stat_print_table_field_local(path);
        write_str(" ");
        write_dec(depth);
        write_str(" ");
        write_str(stat_type_name_local(attributes));
        write_str(" ");
        write_dec(size);
        write_str("\n");
    } else if (depth == 0u) {
        write_str(path);
        write_str("\n");
    } else {
        tree_print_prefix_local(last_stack, depth);
        write_str(is_last ? "`- " : "|- ");
        write_str(cpio_path_basename_local(path));
        write_str("\n");
    }

    if ((attributes & 0x10u) == 0u) {
        return 1;
    }
    if (depth >= DU_MAX_DEPTH) {
        write_err_str("tree: depth limit: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    fd = opendir(path);
    if (fd < 0) {
        write_err_str("tree: cannot open: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (cpio_is_dot_entry_local(entry.name)) {
            continue;
        }
        entry_count++;
    }
    close((uint32_t)fd);

    fd = opendir(path);
    if (fd < 0) {
        write_err_str("tree: cannot reopen: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char child[CMD_PATH_MAX];

        if (cpio_is_dot_entry_local(entry.name)) {
            continue;
        }
        entry_index++;
        if (!cpio_join_local(child, sizeof(child), path, entry.name)) {
            write_err_str("tree: path too long\n");
            continue;
        }
        if (depth + 1u < DU_MAX_DEPTH) {
            last_stack[depth + 1u] = (uint8_t)(entry_index == entry_count);
        }
        if (!tree_walk_local(child, depth + 1u, table, last_stack, entry_index == entry_count)) {
            close((uint32_t)fd);
            return 0;
        }
    }
    close((uint32_t)fd);
    return 1;
}

int cmd_tree(int argc, char **argv) {
    const char *path = ".";
    uint8_t last_stack[DU_MAX_DEPTH + 1u];
    int path_set = 0;
    int table = 0;

    for (int i = 1; i < argc; i++) {
        if (streq_local(argv[i], "--table")) {
            table = 1;
        } else if (!path_set) {
            path = argv[i];
            path_set = 1;
        } else {
            write_err_usage("tree", " [--table] [path]\n");
            return 1;
        }
    }
    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path depth type size\n");
    }
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(last_stack) / sizeof(last_stack[0])); i++) {
        last_stack[i] = 0u;
    }
    return tree_walk_local(path, 0u, table, last_stack, 1) ? 0 : 1;
}

static int file_has_suffix_local(const char *path, const char *suffix) {
    uint32_t path_len = str_len_local(path);
    uint32_t suffix_len = str_len_local(suffix);

    if (path_len < suffix_len) {
        return 0;
    }
    return streq_ignore_case_local(path + path_len - suffix_len, suffix);
}

static int file_bytes_are_text_local(const uint8_t *bytes, uint32_t count) {
    if (count == 0u) {
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t ch = bytes[i];

        if (ch == '\n' || ch == '\r' || ch == '\t') {
            continue;
        }
        if (ch < 32u || ch > 126u) {
            return 0;
        }
    }
    return 1;
}

static const char *file_text_kind_local(const char *path) {
    if (file_has_suffix_local(path, ".cfg")) {
        return "config";
    }
    if (file_has_suffix_local(path, ".asm")) {
        return "assembly";
    }
    if (file_has_suffix_local(path, ".sh")) {
        return "script";
    }
    if (file_has_suffix_local(path, ".txt") || file_has_suffix_local(path, ".md")) {
        return "text";
    }
    return "text";
}

static const char *file_text_detail_local(const char *path) {
    if (file_has_suffix_local(path, ".cfg")) {
        return "ASCII config";
    }
    if (file_has_suffix_local(path, ".asm")) {
        return "assembly source";
    }
    if (file_has_suffix_local(path, ".sh")) {
        return "shell script";
    }
    if (file_has_suffix_local(path, ".md")) {
        return "Markdown text";
    }
    return "ASCII text";
}

static const char *file_detect_kind_local(const char *path,
                                          const uint8_t *bytes,
                                          uint32_t count,
                                          uint32_t size,
                                          const char **detail_out) {
    if (detail_out != NULL) {
        *detail_out = "data";
    }
    if (size == 0u) {
        if (detail_out != NULL) {
            *detail_out = "empty file";
        }
        return "empty";
    }
    if (count >= 4u && bytes[0] == 0x7fu && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
        if (detail_out != NULL) {
            *detail_out = count >= 5u && bytes[4] == 2u ? "ELF64 executable" : "ELF executable";
        }
        return "elf";
    }
    if (count >= 12u &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'A' && bytes[10] == 'V' && bytes[11] == 'E') {
        if (detail_out != NULL) {
            *detail_out = "RIFF WAVE audio";
        }
        return "wav";
    }
    if (count >= 6u &&
        bytes[0] == '0' && bytes[1] == '7' && bytes[2] == '0' &&
        bytes[3] == '7' && bytes[4] == '0' && (bytes[5] == '1' || bytes[5] == '2')) {
        if (detail_out != NULL) {
            *detail_out = "cpio newc archive";
        }
        return "cpio";
    }
    if (file_bytes_are_text_local(bytes, count)) {
        if (detail_out != NULL) {
            *detail_out = file_text_detail_local(path);
        }
        return file_text_kind_local(path);
    }
    if (detail_out != NULL) {
        *detail_out = "binary data";
    }
    return "binary";
}

int cmd_file(int argc, char **argv) {
    const char *path = NULL;
    const char *kind;
    const char *detail;
    uint32_t attributes = 0u;
    uint32_t size = 0u;
    uint8_t bytes[128];
    uint32_t got = 0u;
    int table = 0;
    int fd;
    int raw_got;

    if (argc == 3 && streq_local(argv[1], "--table")) {
        table = 1;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        write_err_usage("file", " [--table] <path>\n");
        return 1;
    }
    if (!cpio_query_path_local(path, &attributes, &size)) {
        write_err_str("file: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    if ((attributes & 0x10u) != 0u) {
        kind = "directory";
        detail = "directory";
    } else {
        fd = open(path, 0);
        if (fd < 0) {
            write_err_str("file: cannot open: ");
            write_err_str(path);
            write_err_str("\n");
            return 1;
        }
        raw_got = (int)read(fd, bytes, sizeof(bytes));
        close((uint32_t)fd);
        if (raw_got < 0) {
            write_err_str("file: read failed: ");
            write_err_str(path);
            write_err_str("\n");
            return 1;
        }
        got = (uint32_t)raw_got;
        kind = file_detect_kind_local(path, bytes, got, size, &detail);
    }
    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path type detail size\n");
        stat_print_table_field_local(path);
        write_str(" ");
        write_str(kind);
        write_str(" ");
        stat_print_table_field_local(detail);
        write_str(" ");
        write_dec(size);
        write_str("\n");
        return 0;
    }
    write_str(path);
    write_str(": ");
    write_str(detail);
    write_str("\n");
    return 0;
}

int cmd_stat(int argc, char **argv) {
    const char *path = NULL;
    uint32_t attributes = 0u;
    uint32_t size = 0u;
    int table = 0;

    if (argc == 3 && streq_local(argv[1], "--table")) {
        table = 1;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        write_err_usage("stat", " [--table] <path>\n");
        return 1;
    }

    if (!cpio_query_path_local(path, &attributes, &size)) {
        write_err_str("stat: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }

    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path type size attr\n");
        stat_print_table_field_local(path);
        write_str(" ");
        write_str(stat_type_name_local(attributes));
        write_str(" ");
        write_dec(size);
        write_str(" 0x");
        write_hex_u32(attributes);
        write_str("\n");
        return 0;
    }

    write_str("path: ");
    write_str(path);
    write_str("\n");
    write_str("type: ");
    write_str(stat_type_name_local(attributes));
    write_str("\n");
    write_str("size: ");
    write_dec(size);
    write_str(" bytes");
    if ((attributes & 0x10u) == 0u) {
        write_str(" (");
        write_human_size(size);
        write_str(")");
    }
    write_str("\n");
    write_str("attr: 0x");
    write_hex_u32(attributes);
    write_str("\n");
    return 0;
}

static int cpio_write_entry_header_local(int fd,
                                         const char *name,
                                         uint32_t mode,
                                         uint32_t file_size,
                                         uint32_t mtime,
                                         uint32_t nlink) {
    char header[CPIO_NEWC_HEADER_SIZE + 1u];
    uint32_t name_size = str_len_local(name) + 1u;

    copy_line_local(header, "070701", sizeof(header));
    cpio_put_hex8_local(header + 6u, 0u);
    cpio_put_hex8_local(header + 14u, mode);
    cpio_put_hex8_local(header + 22u, 0u);
    cpio_put_hex8_local(header + 30u, 0u);
    cpio_put_hex8_local(header + 38u, nlink);
    cpio_put_hex8_local(header + 46u, mtime);
    cpio_put_hex8_local(header + 54u, file_size);
    cpio_put_hex8_local(header + 62u, 0u);
    cpio_put_hex8_local(header + 70u, 0u);
    cpio_put_hex8_local(header + 78u, 0u);
    cpio_put_hex8_local(header + 86u, 0u);
    cpio_put_hex8_local(header + 94u, name_size);
    cpio_put_hex8_local(header + 102u, 0u);
    return cpio_write_all_local(fd, header, CPIO_NEWC_HEADER_SIZE) &&
           cpio_write_all_local(fd, name, name_size) &&
           cpio_write_pad_local(fd, cpio_align4_local(CPIO_NEWC_HEADER_SIZE + name_size));
}

static int cpio_write_file_data_local(int archive_fd, int src_fd, uint32_t size) {
    uint8_t buf[128];
    uint32_t left = size;

    while (left > 0u) {
        uint32_t want = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
        uint32_t got = (uint32_t)read(src_fd, buf, want);

        if (got == 0u || !cpio_write_all_local(archive_fd, buf, got)) {
            return 0;
        }
        left -= got;
    }
    return cpio_write_pad_local(archive_fd, cpio_align4_local(size));
}

static int cpio_archive_path_local(int archive_fd,
                                   const char *disk_path,
                                   const char *archive_name,
                                   uint32_t attributes,
                                   uint32_t size) {
    struct syscall_dirent entry;
    char disk_child[CMD_PATH_MAX];
    char archive_child[CMD_PATH_MAX];
    int fd;

    if ((attributes & 0x10u) != 0u) {
        fd = opendir(disk_path);
        if (fd < 0) {
            return 0;
        }
        if (!cpio_write_entry_header_local(archive_fd, archive_name, CPIO_MODE_DIR, 0u, 0u, 2u)) {
            close((uint32_t)fd);
            return 0;
        }
        while (readdir((uint32_t)fd, &entry) > 0) {
            if (cpio_is_dot_entry_local(entry.name)) {
                continue;
            }
            if (!cpio_join_local(disk_child, sizeof(disk_child), disk_path, entry.name) ||
                !cpio_join_local(archive_child, sizeof(archive_child), archive_name, entry.name) ||
                !cpio_archive_path_local(archive_fd, disk_child, archive_child, entry.attributes, entry.size)) {
                close((uint32_t)fd);
                return 0;
            }
        }
        close((uint32_t)fd);
        return 1;
    }

    fd = open(disk_path, 0);
    if (fd < 0) {
        return 0;
    }
    if (!cpio_write_entry_header_local(archive_fd, archive_name, CPIO_MODE_FILE, size, 0u, 1u) ||
        !cpio_write_file_data_local(archive_fd, fd, size)) {
        close((uint32_t)fd);
        return 0;
    }
    close((uint32_t)fd);
    return 1;
}

static int cpio_mkdir_parents_local(const char *path) {
    char current[CMD_PATH_MAX];
    uint32_t i = 0u;
    uint32_t len;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    len = str_len_local(path);
    if (len >= sizeof(current)) {
        return 0;
    }
    while (i < len) {
        current[i] = path[i];
        if (path[i] == '/' && i > 0u) {
            current[i] = '\0';
            if (!streq_local(current, ".") && !streq_local(current, "/")) {
                (void)mkdir(current);
            }
            current[i] = '/';
        }
        i++;
    }
    current[len] = '\0';
    return 1;
}

static int cmd_cpio_create_local(int argc, char **argv) {
    char archive_name[CMD_PATH_MAX];
    char member_name[CMD_PATH_MAX];
    uint32_t attributes;
    uint32_t size;
    int fd;
    int i;

    if (argc < 4) {
        write_err_usage("cpio", " -o <archive> <path...>\n");
        return 1;
    }
    fd = open(argv[2], O_CREAT | O_TRUNC);
    if (fd < 0) {
        write_err_str("cpio: archive open failed\n");
        return 1;
    }
    for (i = 3; i < argc; i++) {
        cpio_normalize_name_local(argv[i], archive_name, sizeof(archive_name));
        copy_line_local(member_name, archive_name, sizeof(member_name));
        if (streq_local(member_name, ".")) {
            copy_line_local(member_name, cpio_path_basename_local(argv[i]), sizeof(member_name));
        }
        if (!cpio_query_path_local(argv[i], &attributes, &size) ||
            !cpio_archive_path_local(fd, argv[i], member_name, attributes, size)) {
            close((uint32_t)fd);
            write_err_str("cpio: archive write failed\n");
            return 1;
        }
    }
    if (!cpio_write_entry_header_local(fd, "TRAILER!!!", CPIO_MODE_FILE, 0u, 0u, 1u)) {
        close((uint32_t)fd);
        write_err_str("cpio: trailer write failed\n");
        return 1;
    }
    close((uint32_t)fd);
    return 0;
}

static int cmd_cpio_list_or_extract_local(int argc, char **argv, int extract_mode) {
    char header[CPIO_NEWC_HEADER_SIZE];
    char name[CMD_PATH_MAX];
    char out_path[CMD_PATH_MAX];
    char dest_root[CMD_PATH_MAX];
    uint32_t mode;
    uint32_t file_size;
    uint32_t name_size;
    uint32_t pad;
    int fd;

    if ((!extract_mode && argc != 3) || (extract_mode && argc != 3 && argc != 4)) {
        write_err_usage("cpio", extract_mode ? " -i <archive> [dest]\n" : " -t <archive>\n");
        return 1;
    }
    copy_line_local(dest_root, extract_mode && argc == 4 ? argv[3] : ".", sizeof(dest_root));
    fd = open(argv[2], 0);
    if (fd < 0) {
        write_err_str("cpio: archive open failed\n");
        return 1;
    }

    for (;;) {
        if (!cpio_read_all_local(fd, header, sizeof(header))) {
            close((uint32_t)fd);
            write_err_str("cpio: short header\n");
            return 1;
        }
        if (strncmp(header, "070701", 6u) != 0 ||
            !cpio_parse_hex8_local(header + 14u, &mode) ||
            !cpio_parse_hex8_local(header + 54u, &file_size) ||
            !cpio_parse_hex8_local(header + 94u, &name_size) ||
            name_size == 0u || name_size > sizeof(name)) {
            close((uint32_t)fd);
            write_err_str("cpio: invalid archive\n");
            return 1;
        }
        if (!cpio_read_all_local(fd, name, name_size)) {
            close((uint32_t)fd);
            write_err_str("cpio: short name\n");
            return 1;
        }
        name[name_size - 1u] = '\0';
        pad = cpio_align4_local(CPIO_NEWC_HEADER_SIZE + name_size);
        if (!cpio_skip_bytes_local(fd, pad)) {
            close((uint32_t)fd);
            write_err_str("cpio: short padding\n");
            return 1;
        }
        if (streq_local(name, "TRAILER!!!")) {
            break;
        }

        if (!extract_mode) {
            write_str(((mode >> 12) & 0x0fu) == 4u ? "dir  " : "file ");
            write_str(name);
            write_str(" ");
            write_dec(file_size);
            write_str("\n");
            if (!cpio_skip_bytes_local(fd, file_size + cpio_align4_local(file_size))) {
                close((uint32_t)fd);
                write_err_str("cpio: short data\n");
                return 1;
            }
            continue;
        }

        if (!cpio_join_local(out_path, sizeof(out_path), dest_root, name) ||
            !cpio_mkdir_parents_local(out_path)) {
            close((uint32_t)fd);
            write_err_str("cpio: path too long\n");
            return 1;
        }
        if (((mode >> 12) & 0x0fu) == 4u) {
            if (mkdir(out_path) != 0 && opendir(out_path) < 0) {
                close((uint32_t)fd);
                write_err_str("cpio: mkdir failed\n");
                return 1;
            }
            if (!cpio_skip_bytes_local(fd, file_size + cpio_align4_local(file_size))) {
                close((uint32_t)fd);
                write_err_str("cpio: short data\n");
                return 1;
            }
        } else {
            uint8_t buf[128];
            uint32_t left = file_size;
            int out_fd = open(out_path, O_CREAT | O_TRUNC);

            if (out_fd < 0) {
                close((uint32_t)fd);
                write_err_str("cpio: create failed\n");
                return 1;
            }
            while (left > 0u) {
                uint32_t want = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;

                if (!cpio_read_all_local(fd, buf, want) || !cpio_write_all_local(out_fd, buf, want)) {
                    close((uint32_t)out_fd);
                    close((uint32_t)fd);
                    write_err_str("cpio: extract failed\n");
                    return 1;
                }
                left -= want;
            }
            close((uint32_t)out_fd);
            if (!cpio_skip_bytes_local(fd, cpio_align4_local(file_size))) {
                close((uint32_t)fd);
                write_err_str("cpio: short padding\n");
                return 1;
            }
        }
    }
    close((uint32_t)fd);
    return 0;
}

int cmd_cpio(int argc, char **argv) {
    if (argc < 3) {
        write_err_usage("cpio", " -o <archive> <path...> | -t <archive> | -i <archive> [dest]\n");
        return 1;
    }
    if (streq_local(argv[1], "-o")) {
        return cmd_cpio_create_local(argc, argv);
    }
    if (streq_local(argv[1], "-t")) {
        return cmd_cpio_list_or_extract_local(argc, argv, 0);
    }
    if (streq_local(argv[1], "-i")) {
        return cmd_cpio_list_or_extract_local(argc, argv, 1);
    }
    write_err_usage("cpio", " -o <archive> <path...> | -t <archive> | -i <archive> [dest]\n");
    return 1;
}
