#pragma once

#include <stddef.h>
#include <stdint.h>
#include "kernel/public/driver/driver.h"

/*
 * Public ABI for loadable .DRV files.
 *
 * .DRV sources should include this header instead of reaching into unrelated
 * kernel internals. Symbols declared here are resolved by the driver loader.
 */
void driver_log(const char *fmt, ...);

struct driver_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar[6];
};

uint32_t driver_str_len(const char *text);
int driver_streq(const char *lhs, const char *rhs);
int driver_starts_with(const char *text, const char *prefix);

void *driver_memcpy(void *dst, const void *src, uint32_t size);
void *driver_memmove(void *dst, const void *src, uint32_t size);
void *driver_memset(void *dst, int value, uint32_t size);

uint8_t driver_io_in8(uint16_t port);
uint16_t driver_io_in16(uint16_t port);
uint32_t driver_io_in32(uint16_t port);
void driver_io_out8(uint16_t port, uint8_t value);
void driver_io_out16(uint16_t port, uint16_t value);
void driver_io_out32(uint16_t port, uint32_t value);

int driver_pci_find_by_class(uint8_t class_code,
                             uint8_t subclass,
                             uint32_t index,
                             struct driver_pci_device *out);
int driver_pci_find_by_id(uint16_t vendor_id,
                          uint16_t device_id,
                          uint32_t index,
                          struct driver_pci_device *out);
uint8_t driver_pci_read8(const struct driver_pci_device *dev, uint8_t offset);
uint16_t driver_pci_read16(const struct driver_pci_device *dev, uint8_t offset);
uint32_t driver_pci_read32(const struct driver_pci_device *dev, uint8_t offset);
void driver_pci_write8(const struct driver_pci_device *dev, uint8_t offset, uint8_t value);
void driver_pci_write16(const struct driver_pci_device *dev, uint8_t offset, uint16_t value);
void driver_pci_write32(const struct driver_pci_device *dev, uint8_t offset, uint32_t value);
