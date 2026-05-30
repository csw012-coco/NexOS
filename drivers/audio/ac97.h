#pragma once

#include <stdint.h>
#include "kernel/public/driver/driver.h"

struct ac97_status {
    uint8_t present;
    uint8_t initialized;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t reserved0;
    uint8_t reserved1;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t nambar;
    uint32_t nabmbar;
    uint32_t mixer_reset;
    uint32_t powerdown;
    uint32_t ext_audio_id;
    uint32_t ext_audio_ctrl;
    uint32_t codec_id;
    uint32_t global_status;
    uint32_t global_control;
};

int ac97_init(void);
int ac97_query_status(struct ac97_status *out);

extern const struct kernel_driver ac97_kernel_driver;
