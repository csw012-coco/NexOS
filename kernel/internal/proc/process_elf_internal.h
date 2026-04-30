#pragma once

#include "kernel/internal/proc/process_session_internal.h"

struct vfs;
struct vfs_node;

int process_begin_elf_session(void);
int process_load_elf_image(const uint8_t *image, uint32_t image_size, uint64_t *entry_out);
int process_extract_command_name(const char *command_line, char *name_out, uint32_t name_out_size);
int process_prepare_arguments(const char *command_line, const char *const *envp, uint64_t *stack_top_out);
int process_resolve_exec_target(struct vfs *vfs,
                                const char *image_name,
                                const char *command_line,
                                char *resolved_image_name_out,
                                uint32_t resolved_image_name_size,
                                char *resolved_command_line_out,
                                uint32_t resolved_command_line_size,
                                struct vfs_node *node_out,
                                uint32_t *bytes_read_out);
