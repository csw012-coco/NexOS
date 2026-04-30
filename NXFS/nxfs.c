#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_SIZE 512
#define MAX_INODES 128
#define NXFS_MAGIC 0x4E584653
#define EXTENTS 4

typedef struct {
    uint32_t start;
    uint32_t len;
} __attribute__((packed)) extent_t;

typedef struct {
    uint32_t used;
    uint32_t size;
    uint8_t type; // 1=file, 2=dir
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
    extent_t extents[EXTENTS];
} __attribute__((packed)) inode_t;

typedef struct {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t bitmap_start;
    uint32_t inode_start;
    uint32_t data_start;
} __attribute__((packed)) super_t;

typedef struct {
    uint32_t inode;
    char name[28];
} __attribute__((packed)) dir_entry_t;

FILE *disk;
super_t sb;

static const uint32_t dir_capacity = BLOCK_SIZE / sizeof(dir_entry_t);

void free_extent(uint32_t start, uint32_t len);

int64_t now_sec(void) {
    return (int64_t)time(NULL);
}

void inode_touch(inode_t *in, int update_atime, int update_mtime, int update_ctime) {
    int64_t now = now_sec();

    if (update_atime) in->atime = now;
    if (update_mtime) in->mtime = now;
    if (update_ctime) in->ctime = now;
}

void inode_init(inode_t *in, uint8_t type, mode_t mode, uint32_t nlink) {
    const struct fuse_context *ctx = fuse_get_context();
    uid_t uid = ctx ? ctx->uid : getuid();
    gid_t gid = ctx ? ctx->gid : getgid();
    int64_t now = now_sec();

    memset(in, 0, sizeof(*in));
    in->used = 1;
    in->type = type;
    in->mode = ((type == 2) ? S_IFDIR : S_IFREG) | (mode & 07777);
    in->uid = uid;
    in->gid = gid;
    in->nlink = nlink;
    in->atime = now;
    in->mtime = now;
    in->ctime = now;
}

// ================= I/O =================
void read_block(uint32_t b, void *buf) {
    if (!disk) {
        printf("disk NULL(rb)\n");
        exit(1);
    }
    fseek(disk, b * BLOCK_SIZE, SEEK_SET);
    fread(buf, BLOCK_SIZE, 1, disk);
}

void write_block(uint32_t b, void *buf) {
    if (!disk) {
        printf("disk NULL(wb)\n");
        exit(1);
    }
    fseek(disk, b * BLOCK_SIZE, SEEK_SET);
    fwrite(buf, BLOCK_SIZE, 1, disk);
}

void load_super() {
    if (!disk) {
        printf("disk NULL (load_super)\n");
        exit(1);
    }

    fseek(disk, 0, SEEK_SET);
    fread(&sb, sizeof(sb), 1, disk);
}

void read_bitmap(uint8_t *bitmap) {
    read_block(sb.bitmap_start, bitmap);
}

void write_bitmap(uint8_t *bitmap) {
    write_block(sb.bitmap_start, bitmap);
}

int bitmap_get(uint8_t *bitmap, uint32_t block) {
    return (bitmap[block / 8] >> (block % 8)) & 1;
}

void bitmap_set(uint8_t *bitmap, uint32_t block, int used) {
    if (used) {
        bitmap[block / 8] |= (uint8_t)(1u << (block % 8));
    } else {
        bitmap[block / 8] &= (uint8_t)~(1u << (block % 8));
    }
}

uint32_t inode_block_count(const inode_t *in) {
    uint32_t total = 0;

    for (int i = 0; i < EXTENTS; i++)
        total += in->extents[i].len;

    return total;
}

int logical_to_physical(const inode_t *in, uint32_t logical_block) {
    uint32_t base = 0;

    for (int i = 0; i < EXTENTS; i++) {
        uint32_t len = in->extents[i].len;

        if (logical_block < base + len)
            return (int)(in->extents[i].start + (logical_block - base));

        base += len;
    }

    return -1;
}

int append_extent(inode_t *in, uint32_t start, uint32_t len) {
    for (int i = 0; i < EXTENTS; i++) {
        if (in->extents[i].len == 0) {
            in->extents[i].start = start;
            in->extents[i].len = len;
            return 0;
        }
    }

    return -ENOSPC;
}

void free_all_extents(const inode_t *in) {
    for (int i = 0; i < EXTENTS; i++)
        free_extent(in->extents[i].start, in->extents[i].len);
}

int read_inode_block(const inode_t *in, uint32_t logical_block, char *buf) {
    int phys = logical_to_physical(in, logical_block);
    if (phys < 0) return -EIO;

    read_block((uint32_t)phys, buf);
    return 0;
}

int write_inode_block(const inode_t *in, uint32_t logical_block, char *buf) {
    int phys = logical_to_physical(in, logical_block);
    if (phys < 0) return -EIO;

    write_block((uint32_t)phys, buf);
    return 0;
}

// ================= inode =================
inode_t read_inode(int i) {
    if (!disk) {
        printf("disk NULL (read_inode)\n");
        exit(1);
    }

    inode_t inode;
    fseek(disk, sb.inode_start * BLOCK_SIZE + i * sizeof(inode_t), SEEK_SET);
    fread(&inode, sizeof(inode_t), 1, disk);
    return inode;
}

void write_inode(int i, inode_t *in) {
    if (!disk) {
        printf("disk NULL (write_inode)\n");
        exit(1);
    }

    fseek(disk, sb.inode_start * BLOCK_SIZE + i * sizeof(inode_t), SEEK_SET);
    fwrite(in, sizeof(inode_t), 1, disk);
}

int find_free_inode() {
    for (int i = 0; i < MAX_INODES; i++) {
        inode_t in = read_inode(i);
        if (!in.used) return i;
    }
    return -1;
}

// ================= block =================
int alloc_extent(uint32_t len) {
    if (len == 0) return -1;

    uint8_t bitmap[BLOCK_SIZE];
    read_bitmap(bitmap);

    for (uint32_t start = sb.data_start; start + len <= sb.total_blocks; start++) {
        int free = 1;

        for (uint32_t i = 0; i < len; i++) {
            if (bitmap_get(bitmap, start + i)) {
                free = 0;
                start += i;
                break;
            }
        }

        if (!free) continue;

        for (uint32_t i = 0; i < len; i++)
            bitmap_set(bitmap, start + i, 1);

        write_bitmap(bitmap);
        return (int)start;
    }

    return -1;
}

void free_extent(uint32_t start, uint32_t len) {
    if (len == 0) return;

    uint8_t bitmap[BLOCK_SIZE];
    read_bitmap(bitmap);

    for (uint32_t i = 0; i < len; i++)
        bitmap_set(bitmap, start + i, 0);

    write_bitmap(bitmap);
}

int ensure_blocks(inode_t *in, uint32_t need_blocks) {
    inode_t tmp = *in;
    uint32_t have = inode_block_count(&tmp);
    uint32_t new_starts[EXTENTS] = {0};
    uint32_t new_lens[EXTENTS] = {0};
    int new_count = 0;

    while (have < need_blocks) {
        uint32_t remaining = need_blocks - have;
        uint32_t chunk = remaining;
        int start = -1;

        while (chunk > 0) {
            start = alloc_extent(chunk);
            if (start >= 0) break;
            chunk--;
        }

        if (start < 0) {
            for (int i = 0; i < new_count; i++)
                free_extent(new_starts[i], new_lens[i]);
            return -ENOSPC;
        }

        if (append_extent(&tmp, (uint32_t)start, chunk) < 0) {
            free_extent((uint32_t)start, chunk);
            for (int i = 0; i < new_count; i++)
                free_extent(new_starts[i], new_lens[i]);
            return -ENOSPC;
        }

        new_starts[new_count] = (uint32_t)start;
        new_lens[new_count] = chunk;
        new_count++;

        char zero[BLOCK_SIZE] = {0};
        for (uint32_t i = 0; i < chunk; i++)
            write_block((uint32_t)start + i, zero);

        have += chunk;
    }

    *in = tmp;
    return 0;
}

void shrink_blocks(inode_t *in, uint32_t keep_blocks) {
    uint32_t remaining = keep_blocks;

    for (int i = 0; i < EXTENTS; i++) {
        uint32_t len = in->extents[i].len;

        if (len == 0) continue;

        if (remaining >= len) {
            remaining -= len;
            continue;
        }

        if (remaining == 0) {
            free_extent(in->extents[i].start, len);
            in->extents[i].start = 0;
            in->extents[i].len = 0;
        } else {
            free_extent(in->extents[i].start + remaining, len - remaining);
            in->extents[i].len = remaining;
            remaining = 0;
        }

        for (int j = i + 1; j < EXTENTS; j++) {
            free_extent(in->extents[j].start, in->extents[j].len);
            in->extents[j].start = 0;
            in->extents[j].len = 0;
        }
        break;
    }
}

int dir_lookup_entry(const inode_t *dir, const char *name, int *found_ino,
                     uint32_t *found_block, uint32_t *found_slot) {
    uint32_t blocks = inode_block_count(dir);

    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        char block[BLOCK_SIZE];
        if (read_inode_block(dir, block_idx, block) < 0) return -EIO;

        dir_entry_t *e = (dir_entry_t *)block;
        for (uint32_t i = 0; i < dir_capacity; i++) {
            if (e[i].name[0] != '\0' && strcmp(e[i].name, name) == 0) {
                if (found_ino) *found_ino = (int)e[i].inode;
                if (found_block) *found_block = block_idx;
                if (found_slot) *found_slot = i;
                return 0;
            }
        }
    }

    return -ENOENT;
}

int dir_find_free_slot(inode_t *dir, uint32_t *found_block, uint32_t *found_slot) {
    uint32_t blocks = inode_block_count(dir);

    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        char block[BLOCK_SIZE];
        if (read_inode_block(dir, block_idx, block) < 0) return -EIO;

        dir_entry_t *e = (dir_entry_t *)block;
        for (uint32_t i = 0; i < dir_capacity; i++) {
            if (e[i].name[0] == '\0') {
                *found_block = block_idx;
                *found_slot = i;
                return 0;
            }
        }
    }

    if (ensure_blocks(dir, blocks + 1) < 0) return -ENOSPC;

    *found_block = blocks;
    *found_slot = 0;
    return 0;
}

int dir_is_empty(const inode_t *dir) {
    uint32_t blocks = inode_block_count(dir);

    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        char block[BLOCK_SIZE];
        if (read_inode_block(dir, block_idx, block) < 0) return 0;

        dir_entry_t *e = (dir_entry_t *)block;
        for (uint32_t i = 0; i < dir_capacity; i++) {
            if (e[i].name[0] != '\0' &&
                strcmp(e[i].name, ".") != 0 &&
                strcmp(e[i].name, "..") != 0)
                return 0;
        }
    }

    return 1;
}

int dir_add_entry(int dir_ino, inode_t *dir, const char *name, int child_ino) {
    if (strlen(name) >= sizeof(((dir_entry_t *)0)->name)) return -ENAMETOOLONG;

    uint32_t block_idx;
    uint32_t slot_idx;
    int rc = dir_find_free_slot(dir, &block_idx, &slot_idx);
    if (rc < 0) return rc;

    char buf[BLOCK_SIZE];
    if (read_inode_block(dir, block_idx, buf) < 0) return -EIO;

    dir_entry_t *e = (dir_entry_t *)buf;
    e[slot_idx].inode = (uint32_t)child_ino;
    memset(e[slot_idx].name, 0, sizeof(e[slot_idx].name));
    strncpy(e[slot_idx].name, name, sizeof(e[slot_idx].name) - 1);

    if (write_inode_block(dir, block_idx, buf) < 0) return -EIO;

    dir->size = inode_block_count(dir) * BLOCK_SIZE;
    inode_touch(dir, 0, 1, 1);
    write_inode(dir_ino, dir);
    return 0;
}

int dir_update_entry_inode(int dir_ino, inode_t *dir, const char *name, int child_ino) {
    uint32_t block_idx;
    uint32_t slot_idx;
    int rc = dir_lookup_entry(dir, name, NULL, &block_idx, &slot_idx);
    if (rc < 0) return rc;

    char buf[BLOCK_SIZE];
    if (read_inode_block(dir, block_idx, buf) < 0) return -EIO;

    dir_entry_t *e = (dir_entry_t *)buf;
    e[slot_idx].inode = (uint32_t)child_ino;

    if (write_inode_block(dir, block_idx, buf) < 0) return -EIO;
    inode_touch(dir, 0, 0, 1);
    write_inode(dir_ino, dir);
    return 0;
}

int dir_remove_entry(int dir_ino, inode_t *dir, const char *name) {
    uint32_t block_idx;
    uint32_t slot_idx;
    int rc = dir_lookup_entry(dir, name, NULL, &block_idx, &slot_idx);
    if (rc < 0) return rc;

    char buf[BLOCK_SIZE];
    if (read_inode_block(dir, block_idx, buf) < 0) return -EIO;

    dir_entry_t *e = (dir_entry_t *)buf;
    e[slot_idx].inode = 0;
    memset(e[slot_idx].name, 0, sizeof(e[slot_idx].name));

    if (write_inode_block(dir, block_idx, buf) < 0) return -EIO;

    dir->size = inode_block_count(dir) * BLOCK_SIZE;
    inode_touch(dir, 0, 1, 1);
    write_inode(dir_ino, dir);
    return 0;
}

int resolve_path(const char *path, int *ino_out, inode_t *inode_out) {
    if (strcmp(path, "/") == 0) {
        if (ino_out) *ino_out = 0;
        if (inode_out) *inode_out = read_inode(0);
        return 0;
    }

    if (path[0] != '/') return -EINVAL;

    char tmp[1024];
    if (strlen(path) >= sizeof(tmp)) return -ENAMETOOLONG;
    strcpy(tmp, path);

    int current_ino = 0;
    inode_t current = read_inode(0);

    char *saveptr = NULL;
    char *part = strtok_r(tmp + 1, "/", &saveptr);
    while (part) {
        if (current.type != 2) return -ENOTDIR;
        if (strlen(part) == 0) {
            part = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        int next_ino = -1;
        int rc = dir_lookup_entry(&current, part, &next_ino, NULL, NULL);
        if (rc < 0) return rc;

        current_ino = next_ino;
        current = read_inode(current_ino);
        part = strtok_r(NULL, "/", &saveptr);
    }

    if (ino_out) *ino_out = current_ino;
    if (inode_out) *inode_out = current;
    return 0;
}

int split_parent_child(const char *path, char *parent, size_t parent_size,
                       char *name, size_t name_size) {
    if (!path || path[0] != '/') return -EINVAL;
    if (strcmp(path, "/") == 0) return -EINVAL;

    const char *last = strrchr(path, '/');
    if (!last) return -EINVAL;

    if (*(last + 1) == '\0') return -EINVAL;
    if (strlen(last + 1) >= name_size) return -ENAMETOOLONG;
    if (strcmp(last + 1, ".") == 0 || strcmp(last + 1, "..") == 0) return -EINVAL;

    strcpy(name, last + 1);

    size_t parent_len = (size_t)(last - path);
    if (parent_len == 0) {
        if (parent_size < 2) return -ENAMETOOLONG;
        strcpy(parent, "/");
    } else {
        if (parent_len + 1 >= parent_size) return -ENAMETOOLONG;
        memcpy(parent, path, parent_len);
        parent[parent_len] = '\0';
    }

    return 0;
}

int dir_parent_ino(const inode_t *dir) {
    int parent_ino = -1;
    if (dir_lookup_entry(dir, "..", &parent_ino, NULL, NULL) < 0)
        return -1;
    return parent_ino;
}

int init_dir_entries(int dir_ino, inode_t *dir, int parent_ino) {
    int rc = dir_add_entry(dir_ino, dir, ".", dir_ino);
    if (rc < 0) return rc;

    return dir_add_entry(dir_ino, dir, "..", parent_ino);
}

int is_ancestor_dir(int ancestor_ino, int dir_ino) {
    int current_ino = dir_ino;

    while (1) {
        if (current_ino == ancestor_ino) return 1;

        inode_t current = read_inode(current_ino);
        if (current.type != 2) return 0;

        int parent_ino = dir_parent_ino(&current);
        if (parent_ino < 0 || parent_ino == current_ino) return 0;
        current_ino = parent_ino;
    }
}

// ================= path =================
int path_lookup(const char *path) {
    int ino = -1;
    if (resolve_path(path, &ino, NULL) == 0)
        return ino;
    return -1;
}

// ================= FUSE ops =================

static int nxfs_getattr(const char *path, struct stat *st,
                       struct fuse_file_info *fi) {
    (void) fi;
    memset(st, 0, sizeof(struct stat));

    int ino = -1;
    inode_t in;
    int rc = resolve_path(path, &ino, &in);
    if (rc < 0) return rc;

    if (in.type == 2) {
        st->st_mode = in.mode;
    } else {
        st->st_mode = in.mode;
        st->st_size = in.size;
    }
    st->st_nlink = in.nlink;
    st->st_uid = in.uid;
    st->st_gid = in.gid;
    st->st_atim.tv_sec = in.atime;
    st->st_mtim.tv_sec = in.mtime;
    st->st_ctim.tv_sec = in.ctime;

    return 0;
}

static int nxfs_readdir(const char *path, void *buf,
    fuse_fill_dir_t filler, off_t offset,
    struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) path;
    (void) offset;
    (void) fi;
    (void) flags;

    inode_t dir;
    int rc = resolve_path(path, NULL, &dir);
    if (rc < 0) return rc;
    if (dir.type != 2) return -ENOTDIR;

    uint32_t blocks = inode_block_count(&dir);

    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        char block[BLOCK_SIZE];
        if (read_inode_block(&dir, block_idx, block) < 0) return -EIO;

        dir_entry_t *e = (dir_entry_t*)block;
        for (uint32_t i = 0; i < dir_capacity; i++) {
            if (e[i].name[0] != '\0')
                filler(buf, e[i].name, NULL, 0, 0);
        }
    }

    return 0;
}

static int nxfs_read(const char *path, char *buf,
    size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    int ino = path_lookup(path);
    if (ino < 0) return -ENOENT;

    inode_t in = read_inode(ino);
    if (in.type != 1) return -EISDIR;

    if (offset < 0) return -EINVAL;

    if (offset >= in.size) return 0;
    if (offset + size > in.size)
        size = in.size - offset;

    size_t read_bytes = 0;

    uint32_t total_blocks = inode_block_count(&in);
    for (uint32_t i = 0; i < total_blocks; i++) {
        char block[BLOCK_SIZE];
        int phys = logical_to_physical(&in, i);
        if (phys < 0) return -EIO;
        read_block((uint32_t)phys, block);

        size_t block_off = i * BLOCK_SIZE;
        if ((size_t)offset >= block_off + BLOCK_SIZE) continue;

        size_t local = ((size_t)offset > block_off) ? (size_t)offset - block_off : 0;
        size_t copy = BLOCK_SIZE - local;
        if (copy > size - read_bytes) copy = size - read_bytes;

        memcpy(buf + read_bytes, block + local, copy);
        read_bytes += copy;

        if (read_bytes >= size) break;
    }

    inode_touch(&in, 1, 0, 0);
    write_inode(ino, &in);
    return read_bytes;
}

static int nxfs_write(const char *path, const char *buf,
    size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    if (size == 0) return 0;
    if (offset < 0) return -EINVAL;

    int ino = path_lookup(path);
    if (ino < 0) return -ENOENT;

    inode_t in = read_inode(ino);
    if (in.type != 1) return -EISDIR;

    size_t end = offset + size;
    uint32_t need = (uint32_t)((end + BLOCK_SIZE - 1) / BLOCK_SIZE);

    if (ensure_blocks(&in, need) < 0) return -ENOSPC;

    size_t written = 0;
    uint32_t total_blocks = inode_block_count(&in);
    for (uint32_t i = 0; i < total_blocks; i++) {
        char block[BLOCK_SIZE] = {0};
        int phys = logical_to_physical(&in, i);
        if (phys < 0) return -EIO;
        read_block((uint32_t)phys, block);

        size_t block_off = i * BLOCK_SIZE;
        if ((size_t)offset >= block_off + BLOCK_SIZE) continue;

        size_t local = ((size_t)offset > block_off) ? (size_t)offset - block_off : 0;
        size_t copy = BLOCK_SIZE - local;
        if (copy > size - written) copy = size - written;

        memcpy(block + local, buf + written, copy);
        write_block((uint32_t)phys, block);

        written += copy;
        if (written >= size) break;
    }

    if (end > in.size)
        in.size = end;

    inode_touch(&in, 0, 1, 1);
    write_inode(ino, &in);
    return size;
}

static int nxfs_create(const char *path, mode_t mode,
    struct fuse_file_info *fi) {
    (void) mode;
    (void) fi;

    if (path_lookup(path) >= 0) return -EEXIST;

    char parent_path[1024];
    char name[sizeof(((dir_entry_t *)0)->name)];
    int rc = split_parent_child(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (rc < 0) return rc;

    int parent_ino = -1;
    inode_t parent;
    rc = resolve_path(parent_path, &parent_ino, &parent);
    if (rc < 0) return rc;
    if (parent.type != 2) return -ENOTDIR;

    int ino = find_free_inode();
    if (ino < 0) return -ENOSPC;

    inode_t in;
    inode_init(&in, 1, mode, 1);
    write_inode(ino, &in);

    rc = dir_add_entry(parent_ino, &parent, name, ino);
    if (rc < 0) {
        inode_t empty = {0};
        write_inode(ino, &empty);
        return rc;
    }
    return 0;
}

static int nxfs_truncate(const char *path, off_t size,
                         struct fuse_file_info *fi) {
    (void) fi;

    int ino = path_lookup(path);
    if (ino < 0) return -ENOENT;
    if (size < 0) return -EINVAL;

    inode_t in = read_inode(ino);
    if (in.type != 1) return -EISDIR;
    uint32_t need_blocks = (size == 0) ? 0 : (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    uint32_t current_blocks = inode_block_count(&in);

    if (need_blocks > current_blocks) {
        if (ensure_blocks(&in, need_blocks) < 0) return -ENOSPC;
    } else if (need_blocks < current_blocks) {
        shrink_blocks(&in, need_blocks);
    }

    if (need_blocks > 0 && (size % BLOCK_SIZE) != 0) {
        char block[BLOCK_SIZE];
        uint32_t last_block = need_blocks - 1;
        size_t tail = (size_t)(size % BLOCK_SIZE);

        if (read_inode_block(&in, last_block, block) < 0) return -EIO;
        memset(block + tail, 0, BLOCK_SIZE - tail);
        if (write_inode_block(&in, last_block, block) < 0) return -EIO;
    }

    if (size == 0) {
        memset(in.extents, 0, sizeof(in.extents));
    }

    in.size = (uint32_t)size;
    inode_touch(&in, 0, 1, 1);
    write_inode(ino, &in);
    return 0;
}

static int nxfs_unlink(const char *path) {
    int ino = -1;
    inode_t in;
    int rc = resolve_path(path, &ino, &in);
    if (rc < 0) return rc;
    if (in.type != 1) return -EISDIR;

    char parent_path[1024];
    char name[sizeof(((dir_entry_t *)0)->name)];
    rc = split_parent_child(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (rc < 0) return rc;

    int parent_ino = -1;
    inode_t parent;
    rc = resolve_path(parent_path, &parent_ino, &parent);
    if (rc < 0) return rc;

    free_all_extents(&in);

    inode_t empty = {0};
    write_inode(ino, &empty);

    return dir_remove_entry(parent_ino, &parent, name);
}

static int nxfs_mkdir(const char *path, mode_t mode) {
    (void) mode;

    if (path_lookup(path) >= 0) return -EEXIST;

    char parent_path[1024];
    char name[sizeof(((dir_entry_t *)0)->name)];
    int rc = split_parent_child(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (rc < 0) return rc;

    int parent_ino = -1;
    inode_t parent;
    rc = resolve_path(parent_path, &parent_ino, &parent);
    if (rc < 0) return rc;
    if (parent.type != 2) return -ENOTDIR;

    int ino = find_free_inode();
    if (ino < 0) return -ENOSPC;

    inode_t dir;
    inode_init(&dir, 2, mode, 2);
    dir.size = BLOCK_SIZE;

    if (ensure_blocks(&dir, 1) < 0) return -ENOSPC;

    write_inode(ino, &dir);

    rc = init_dir_entries(ino, &dir, parent_ino);
    if (rc < 0) {
        free_all_extents(&dir);
        inode_t empty = {0};
        write_inode(ino, &empty);
        return rc;
    }

    rc = dir_add_entry(parent_ino, &parent, name, ino);
    if (rc < 0) {
        free_all_extents(&dir);
        inode_t empty = {0};
        write_inode(ino, &empty);
        return rc;
    }

    parent.nlink++;
    inode_touch(&parent, 0, 0, 1);
    write_inode(parent_ino, &parent);

    return 0;
}

static int nxfs_rmdir(const char *path) {
    if (strcmp(path, "/") == 0) return -EBUSY;

    int ino = -1;
    inode_t dir;
    int rc = resolve_path(path, &ino, &dir);
    if (rc < 0) return rc;
    if (dir.type != 2) return -ENOTDIR;
    if (!dir_is_empty(&dir)) return -ENOTEMPTY;

    char parent_path[1024];
    char name[sizeof(((dir_entry_t *)0)->name)];
    rc = split_parent_child(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (rc < 0) return rc;

    int parent_ino = -1;
    inode_t parent;
    rc = resolve_path(parent_path, &parent_ino, &parent);
    if (rc < 0) return rc;

    free_all_extents(&dir);

    inode_t empty = {0};
    write_inode(ino, &empty);

    rc = dir_remove_entry(parent_ino, &parent, name);
    if (rc < 0) return rc;

    if (parent.nlink > 0) parent.nlink--;
    inode_touch(&parent, 0, 0, 1);
    write_inode(parent_ino, &parent);
    return 0;
}

static int nxfs_rename(const char *from, const char *to, unsigned int flags) {
    if (flags != 0) return -EINVAL;
    if (strcmp(from, to) == 0) return 0;

    int src_ino = -1;
    inode_t src;
    int rc = resolve_path(from, &src_ino, &src);
    if (rc < 0) return rc;

    char old_parent_path[1024];
    char old_name[sizeof(((dir_entry_t *)0)->name)];
    rc = split_parent_child(from, old_parent_path, sizeof(old_parent_path), old_name, sizeof(old_name));
    if (rc < 0) return rc;

    char new_parent_path[1024];
    char new_name[sizeof(((dir_entry_t *)0)->name)];
    rc = split_parent_child(to, new_parent_path, sizeof(new_parent_path), new_name, sizeof(new_name));
    if (rc < 0) return rc;

    int old_parent_ino = -1;
    inode_t old_parent;
    rc = resolve_path(old_parent_path, &old_parent_ino, &old_parent);
    if (rc < 0) return rc;

    int new_parent_ino = -1;
    inode_t new_parent;
    rc = resolve_path(new_parent_path, &new_parent_ino, &new_parent);
    if (rc < 0) return rc;
    if (new_parent.type != 2) return -ENOTDIR;

    int dst_removed_dir = 0;
    int dst_ino = -1;
    inode_t dst;
    rc = resolve_path(to, &dst_ino, &dst);
    if (rc == 0) {
        if (dst.type != src.type) return (src.type == 2) ? -ENOTDIR : -EISDIR;
        if (dst.type == 2 && !dir_is_empty(&dst)) return -ENOTEMPTY;
        if (dst_ino == src_ino) return 0;

        free_all_extents(&dst);
        inode_t empty = {0};
        write_inode(dst_ino, &empty);
        rc = dir_remove_entry(new_parent_ino, &new_parent, new_name);
        if (rc < 0) return rc;
        if (dst.type == 2 && new_parent.nlink > 0) {
            dst_removed_dir = 1;
            new_parent.nlink--;
            inode_touch(&new_parent, 0, 0, 1);
            write_inode(new_parent_ino, &new_parent);
        }
    } else if (rc != -ENOENT) {
        return rc;
    }

    if (src.type == 2 && is_ancestor_dir(src_ino, new_parent_ino))
        return -EINVAL;

    rc = dir_add_entry(new_parent_ino, &new_parent, new_name, src_ino);
    if (rc < 0) return rc;

    rc = dir_remove_entry(old_parent_ino, &old_parent, old_name);
    if (rc < 0) return rc;

    if (src.type == 2 && old_parent_ino != new_parent_ino) {
        if (old_parent.nlink > 0) old_parent.nlink--;
        inode_touch(&old_parent, 0, 0, 1);
        write_inode(old_parent_ino, &old_parent);
        new_parent.nlink++;
        inode_touch(&new_parent, 0, 0, 1);
        write_inode(new_parent_ino, &new_parent);
        rc = dir_update_entry_inode(src_ino, &src, "..", new_parent_ino);
        if (rc < 0) return rc;
        inode_touch(&src, 0, 0, 1);
        write_inode(src_ino, &src);
    } else if (src.type == 2 && dst_removed_dir) {
        new_parent.nlink++;
        inode_touch(&new_parent, 0, 0, 1);
        write_inode(new_parent_ino, &new_parent);
    }

    return 0;
}

static int nxfs_utimens(const char *path,
                        const struct timespec ts[2],
                        struct fuse_file_info *fi) {
    (void) fi;

    int ino = -1;
    inode_t in;
    int rc = resolve_path(path, &ino, &in);
    if (rc < 0) return rc;

    in.atime = ts[0].tv_sec;
    in.mtime = ts[1].tv_sec;
    in.ctime = now_sec();
    write_inode(ino, &in);
    return 0;
}

static struct fuse_operations ops = {
    .getattr = nxfs_getattr,
    .readdir = nxfs_readdir,
    .read = nxfs_read,
    .write = nxfs_write,
    .create = nxfs_create,
    .mkdir = nxfs_mkdir,
    .rename = nxfs_rename,
    .rmdir = nxfs_rmdir,
    .truncate = nxfs_truncate,
    .unlink = nxfs_unlink,
    .utimens = nxfs_utimens,
};

// ================= mkfs =================
void mkfs(const char *file) {
    disk = fopen(file, "wb+");

    char zero[BLOCK_SIZE] = {0};
    for (int i = 0; i < 1024; i++)
        fwrite(zero, BLOCK_SIZE, 1, disk);

    sb.magic = NXFS_MAGIC;
    sb.total_blocks = 1024;
    sb.bitmap_start = 1;
    sb.inode_start = 2;
    sb.data_start = 32;
    fseek(disk, 0, SEEK_SET);
    fwrite(&sb, sizeof(sb), 1, disk);

    uint8_t bitmap[BLOCK_SIZE] = {0};
    for (uint32_t b = 0; b < sb.data_start; b++)
        bitmap_set(bitmap, b, 1);
    bitmap_set(bitmap, sb.data_start, 1);
    write_bitmap(bitmap);

    inode_t root = {0};
    inode_init(&root, 2, 0755, 2);
    root.size = BLOCK_SIZE;
    root.extents[0].start = sb.data_start;
    root.extents[0].len = 1;
    write_inode(0, &root);

    write_block(root.extents[0].start, zero);
    init_dir_entries(0, &root, 0);
    fclose(disk);
}

// ================= main =================
int main(int argc, char *argv[]) {

    if (argc >= 3 && strcmp(argv[1], "mkfs") == 0) {
        mkfs(argv[2]);
        return 0;
    }

    if (argc < 4 || strcmp(argv[1], "mount") != 0) {
        printf("Usage:\n");
        printf("  %s mkfs <img>\n", argv[0]);
        printf("  %s mount <img> <mountpoint>\n", argv[0]);
        return 1;
    }

    disk = fopen(argv[2], "rb+");
    if (!disk) {
        perror("fopen");
        return 1;
    }

    load_super();

    char *fuse_argv[4];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = "-f";   // foreground
    fuse_argv[2] = "-s";   // ⭐ single-thread
    fuse_argv[3] = argv[3];

    return fuse_main(4, fuse_argv, &ops, NULL);
}
