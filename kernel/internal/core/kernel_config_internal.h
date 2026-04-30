#pragma once

#include <stdint.h>
#include "kernel/public/sys/system_limits.h"

struct vfs;

struct kernel_config {
    uint8_t loaded;
    uint8_t init_path_set;
    uint8_t ring3_smoke;
    char init_path[NOS_PATH_BUFFER_SIZE];
};

void kernel_config_defaults(struct kernel_config *config);
void kernel_config_load(struct vfs *vfs, const char *path, struct kernel_config *config);
