#pragma once

#include <stdint.h>
#include "kernel/public/proc/context.h"

struct early_vfs;
struct vfs;
struct process_snapshot;

void process32_init(struct early_vfs *vfs);
void process32_init_runtime_vfs(struct vfs *vfs);
void process32_register_backend(void);
