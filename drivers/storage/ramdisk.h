#pragma once

#include <stdint.h>
#include "janus/janus.h"

void ramdisk_init_from_boot_modules(const struct janus_boot_info *boot_info);
uint32_t ramdisk_first_disk_index(void);
uint32_t ramdisk_disk_index_by_name(const char *name);
