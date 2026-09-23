#pragma once

#include <stdint.h>
#include "kernel/public/mem/pmm.h"

struct janus_boot_info;

int x86_64_pmm_init(const struct janus_boot_info *boot_info,
                    uint64_t kernel_phys_addr);
