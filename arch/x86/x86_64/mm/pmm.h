#pragma once

#include <stdint.h>
#include "kernel/public/mem/pmm.h"

struct bootx_boot_info;

int x86_64_pmm_init(const struct bootx_boot_info *boot_info,
                    uint64_t kernel_phys_addr);
