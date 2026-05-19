#pragma once

#include <stdint.h>

struct hda_status {
    uint8_t present;
    uint8_t initialized;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t mmio_base_lo;
    uint32_t mmio_base_hi;
    uint32_t pci_command;
    uint32_t gcap;
    uint32_t vmaj;
    uint32_t vmin;
    uint32_t outpay;
    uint32_t inpay;
    uint32_t gctl;
    uint32_t statests;
    uint32_t wakeen;
    uint32_t corb_size;
    uint32_t rirb_size;
    uint32_t codec_mask;
};

int hda_init(void);
int hda_query_status(struct hda_status *out);
