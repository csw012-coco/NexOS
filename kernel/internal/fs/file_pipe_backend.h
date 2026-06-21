#pragma once

#include "kernel/internal/fs/file_internal.h"

int file_pipe_backend_is_kind(uint8_t kind);
int file_pipe_backend_init_pair(struct file *read_file, struct file *write_file);
int file_pipe_backend_create_named(const char *path);
int file_pipe_backend_unlink_named(const char *path);
int file_pipe_backend_named_exists(const char *path);
int file_pipe_backend_open_named(struct file *file, const char *path, int writable);
void file_pipe_backend_discard(struct file *file);
int file_pipe_backend_clone(struct file *dst, const struct file *src);
int file_pipe_backend_read_would_block(const struct file *file);
int file_pipe_backend_write_would_block(const struct file *file);
int file_pipe_backend_has_readers(const struct file *file);
