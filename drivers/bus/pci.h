#pragma once

#include <stdint.h>

struct pci_device_info {
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
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
};

struct pci_ide_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
};

struct pci_ahci_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t abar;
};

struct pci_ac97_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t nambar;
    uint32_t nabmbar;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
};

struct pci_hda_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t mmio_base_lo;
    uint32_t mmio_base_hi;
};

struct pci_ehci_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t mmio_base;
};

struct pci_xhci_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t mmio_base_lo;
    uint32_t mmio_base_hi;
};

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);

int pci_find_device_by_class(uint8_t class_code, uint8_t subclass, struct pci_device_info *out);
int pci_find_device_by_class_at(uint8_t class_code, uint8_t subclass, uint32_t index, struct pci_device_info *out);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device_info *out);
int pci_find_device_at(uint16_t vendor_id, uint16_t device_id, uint32_t index, struct pci_device_info *out);
int pci_find_ide_controller(struct pci_ide_controller *out);
int pci_find_ide_controller_at(uint32_t index, struct pci_ide_controller *out);
int pci_find_ahci_controller(struct pci_ahci_controller *out);
int pci_find_ahci_controller_at(uint32_t index, struct pci_ahci_controller *out);
int pci_find_ac97_controller(struct pci_ac97_controller *out);
int pci_find_ac97_controller_at(uint32_t index, struct pci_ac97_controller *out);
int pci_find_hda_controller(struct pci_hda_controller *out);
int pci_find_hda_controller_at(uint32_t index, struct pci_hda_controller *out);
int pci_find_ehci_controller(struct pci_ehci_controller *out);
int pci_find_ehci_controller_at(uint32_t index, struct pci_ehci_controller *out);
int pci_find_xhci_controller(struct pci_xhci_controller *out);
int pci_find_xhci_controller_at(uint32_t index, struct pci_xhci_controller *out);
