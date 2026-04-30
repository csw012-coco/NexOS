#include "kernel/internal/fs/file_pipe_backend.h"

enum {
    FILE_PIPE_MAX = 4,
    FILE_PIPE_BUFFER_SIZE = 65536u
};

struct file_pipe {
    uint8_t used;
    uint8_t readers;
    uint8_t writers;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint8_t buffer[FILE_PIPE_BUFFER_SIZE];
};

static struct file_pipe g_file_pipes[FILE_PIPE_MAX];

static void file_pipe_init_with_ops(struct file *file, uint8_t kind, const struct file_ops *ops) {
    if (file == 0) {
        return;
    }
    file_reset(file);
    file->kind = kind;
    file->ops = ops;
}

static void file_pipe_reset(struct file_pipe *pipe) {
    if (pipe == 0) {
        return;
    }
    pipe->used = 0;
    pipe->readers = 0;
    pipe->writers = 0;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
}

static void file_pipe_init_end(struct file *file,
                               uint8_t kind,
                               struct file_pipe *pipe,
                               const struct file_ops *ops) {
    if (file == 0) {
        return;
    }
    file_pipe_init_with_ops(file, kind, ops);
    file->private_data = pipe;
}

static int64_t file_pipe_reset_and_ok(struct file *file) {
    file_reset(file);
    return 0;
}

static int64_t file_pipe_readdir_unsupported(struct file *file,
                                             const struct vfs *vfs,
                                             struct vfs_dirent *entry) {
    (void)file;
    (void)vfs;
    (void)entry;
    return -1;
}

static void file_pipe_release_end(struct file_pipe *pipe, uint8_t kind) {
    if (pipe == 0) {
        return;
    }
    if (kind == KERNEL_FILE_PIPE_READ) {
        if (pipe->readers != 0) {
            pipe->readers--;
        }
    } else if (kind == KERNEL_FILE_PIPE_WRITE) {
        if (pipe->writers != 0) {
            pipe->writers--;
        }
    }
}

static void file_pipe_acquire_end(struct file_pipe *pipe, uint8_t kind) {
    if (pipe == 0) {
        return;
    }
    if (kind == KERNEL_FILE_PIPE_READ) {
        pipe->readers++;
    } else if (kind == KERNEL_FILE_PIPE_WRITE) {
        pipe->writers++;
    }
}

static struct file_pipe *file_pipe_from_file(struct file *file) {
    return file != 0 ? (struct file_pipe *)file->private_data : 0;
}

static int64_t file_pipe_read(struct file *file,
                              const struct vfs *vfs,
                              void *buffer,
                              uint32_t size,
                              uint32_t flags) {
    struct file_pipe *pipe = file_pipe_from_file(file);
    uint8_t *out = (uint8_t *)buffer;
    uint32_t total = 0;

    (void)vfs;
    (void)flags;
    if (pipe == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (pipe->count == 0) {
        return 0;
    }
    while (total < size && pipe->count != 0) {
        out[total++] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1u) % FILE_PIPE_BUFFER_SIZE;
        pipe->count--;
    }
    return (int64_t)total;
}

static int64_t file_pipe_write(struct file *file,
                               const struct vfs *vfs,
                               const void *buffer,
                               uint32_t size) {
    struct file_pipe *pipe = file_pipe_from_file(file);
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t total = 0;

    (void)vfs;
    if (pipe == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (pipe->readers == 0) {
        return -1;
    }
    while (total < size && pipe->count < FILE_PIPE_BUFFER_SIZE) {
        pipe->buffer[pipe->write_pos] = in[total++];
        pipe->write_pos = (pipe->write_pos + 1u) % FILE_PIPE_BUFFER_SIZE;
        pipe->count++;
    }
    return (int64_t)total;
}

static int64_t file_pipe_close(struct file *file) {
    struct file_pipe *pipe = file_pipe_from_file(file);

    if (pipe != 0) {
        file_pipe_release_end(pipe, file->kind);
        if (pipe->readers == 0 && pipe->writers == 0) {
            file_pipe_reset(pipe);
        }
    }
    return file_pipe_reset_and_ok(file);
}

static const struct file_ops g_file_ops_pipe_read = {
    .read = file_pipe_read,
    .write = 0,
    .close = file_pipe_close,
    .readdir = file_pipe_readdir_unsupported,
};

static const struct file_ops g_file_ops_pipe_write = {
    .read = 0,
    .write = file_pipe_write,
    .close = file_pipe_close,
    .readdir = file_pipe_readdir_unsupported,
};

int file_pipe_backend_is_kind(uint8_t kind) {
    return kind == KERNEL_FILE_PIPE_READ || kind == KERNEL_FILE_PIPE_WRITE;
}

int file_pipe_backend_init_pair(struct file *read_file, struct file *write_file) {
    struct file_pipe *pipe = 0;

    if (read_file == 0 || write_file == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < FILE_PIPE_MAX; i++) {
        if (!g_file_pipes[i].used) {
            pipe = &g_file_pipes[i];
            file_pipe_reset(pipe);
            pipe->used = 1u;
            pipe->readers = 1u;
            pipe->writers = 1u;
            break;
        }
    }
    if (pipe == 0) {
        return 0;
    }
    file_pipe_init_end(read_file, KERNEL_FILE_PIPE_READ, pipe, &g_file_ops_pipe_read);
    file_pipe_init_end(write_file, KERNEL_FILE_PIPE_WRITE, pipe, &g_file_ops_pipe_write);
    return 1;
}

void file_pipe_backend_discard(struct file *file) {
    if (file == 0 || !file_is_active(file)) {
        return;
    }
    (void)file_pipe_close(file);
}

int file_pipe_backend_clone(struct file *dst, const struct file *src) {
    struct file_pipe *pipe;

    if (dst == 0 || src == 0 || !file_is_active(src) || !file_pipe_backend_is_kind(src->kind)) {
        return 0;
    }
    *dst = *src;
    pipe = (struct file_pipe *)dst->private_data;
    if (pipe == 0) {
        file_reset(dst);
        return 0;
    }
    file_pipe_acquire_end(pipe, dst->kind);
    return 1;
}

int file_pipe_backend_read_would_block(const struct file *file) {
    const struct file_pipe *pipe;

    if (file == 0 || file->kind != KERNEL_FILE_PIPE_READ) {
        return 0;
    }
    pipe = (const struct file_pipe *)file->private_data;
    return pipe != 0 && pipe->count == 0 && pipe->writers != 0;
}

int file_pipe_backend_write_would_block(const struct file *file) {
    const struct file_pipe *pipe;

    if (file == 0 || file->kind != KERNEL_FILE_PIPE_WRITE) {
        return 0;
    }
    pipe = (const struct file_pipe *)file->private_data;
    return pipe != 0 && pipe->count >= FILE_PIPE_BUFFER_SIZE && pipe->readers != 0;
}
