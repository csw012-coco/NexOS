#pragma once

#include "kernel/internal/fs/file_internal.h"

int file_pipe_backend_is_kind(uint8_t kind);
int file_pipe_backend_init_pair(struct file *read_file, struct file *write_file);
void file_pipe_backend_discard(struct file *file);
int file_pipe_backend_clone(struct file *dst, const struct file *src);
int file_pipe_backend_read_would_block(const struct file *file);
int file_pipe_backend_write_would_block(const struct file *file);
