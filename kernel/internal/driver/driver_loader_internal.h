#pragma once

#include "kernel/public/driver/driver.h"

struct vfs;
struct vfs_node;

int driver_register_source(const struct kernel_driver *driver,
                           const char *source,
                           const char *path);
int driver_boot_verbose_enabled(void);

enum kernel_driver_file_state driver_arch_probe_file(struct vfs *vfs,
                                                     struct vfs_node *node,
                                                     struct kernel_driver_file *file);
int driver_arch_load_file(struct vfs *vfs, struct kernel_driver_file *file);
