#include "drivers/bus/pci.h"
#include "arch/x86/io.h"

enum {
    PCI_CONFIG_ADDRESS = 0x0cf8,
    PCI_CONFIG_DATA = 0x0cfc,
    PCI_CLASS_MASS_STORAGE = 0x01,
    PCI_CLASS_MULTIMEDIA = 0x04,
    PCI_CLASS_SERIAL_BUS = 0x0c,
    PCI_SUBCLASS_IDE = 0x01,
    PCI_SUBCLASS_SATA = 0x06,
    PCI_PROGIF_AHCI = 0x01,
    PCI_SUBCLASS_AUDIO = 0x01,
    PCI_SUBCLASS_HDA = 0x03,
    PCI_SUBCLASS_USB = 0x03,
    PCI_PROGIF_EHCI = 0x20,
    PCI_PROGIF_XHCI = 0x30
};

static uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           (offset & 0xfcu);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset);

    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset);

    return (uint8_t)(value >> ((offset & 3u) * 8u));
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint8_t aligned_offset = (uint8_t)(offset & 0xfcu);
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t mask = 0xffffu << shift;
    uint32_t current = pci_config_read32(bus, slot, function, aligned_offset);

    current = (current & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, slot, function, aligned_offset, current);
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value) {
    uint8_t aligned_offset = (uint8_t)(offset & 0xfcu);
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = 0xffu << shift;
    uint32_t current = pci_config_read32(bus, slot, function, aligned_offset);

    current = (current & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, slot, function, aligned_offset, current);
}

static void pci_fill_device_info(uint8_t bus, uint8_t slot, uint8_t function, struct pci_device_info *out) {
    out->bus = bus;
    out->slot = slot;
    out->function = function;
    out->class_code = pci_config_read8(bus, slot, function, 0x0b);
    out->subclass = pci_config_read8(bus, slot, function, 0x0a);
    out->prog_if = pci_config_read8(bus, slot, function, 0x09);
    out->irq_line = pci_config_read8(bus, slot, function, 0x3c);
    out->irq_pin = pci_config_read8(bus, slot, function, 0x3d);
    out->vendor_id = pci_config_read16(bus, slot, function, 0x00);
    out->device_id = pci_config_read16(bus, slot, function, 0x02);
    out->bar0 = pci_config_read32(bus, slot, function, 0x10);
    out->bar1 = pci_config_read32(bus, slot, function, 0x14);
    out->bar2 = pci_config_read32(bus, slot, function, 0x18);
    out->bar3 = pci_config_read32(bus, slot, function, 0x1c);
    out->bar4 = pci_config_read32(bus, slot, function, 0x20);
    out->bar5 = pci_config_read32(bus, slot, function, 0x24);
}

int pci_find_device_by_class(uint8_t class_code, uint8_t subclass, struct pci_device_info *out) {
    return pci_find_device_by_class_at(class_code, subclass, 0u, out);
}

int pci_find_device_by_index(uint32_t index, struct pci_device_info *out) {
    uint32_t matched = 0u;

    if (out == 0) {
        return 0;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint8_t function_count = 1;

            if (pci_config_read16((uint8_t)bus, slot, 0, 0x00) == 0xffffu) {
                continue;
            }
            if ((pci_config_read8((uint8_t)bus, slot, 0, 0x0e) & 0x80u) != 0) {
                function_count = 8;
            }

            for (uint8_t function = 0; function < function_count; function++) {
                if (pci_config_read16((uint8_t)bus, slot, function, 0x00) == 0xffffu) {
                    continue;
                }
                if (matched++ != index) {
                    continue;
                }

                pci_fill_device_info((uint8_t)bus, slot, function, out);
                return 1;
            }
        }
    }

    return 0;
}

int pci_find_device_by_class_at(uint8_t class_code, uint8_t subclass, uint32_t index, struct pci_device_info *out) {
    uint32_t matched = 0u;

    if (out == 0) {
        return 0;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint8_t function_count = 1;

            if (pci_config_read16((uint8_t)bus, slot, 0, 0x00) == 0xffffu) {
                continue;
            }
            if ((pci_config_read8((uint8_t)bus, slot, 0, 0x0e) & 0x80u) != 0) {
                function_count = 8;
            }

            for (uint8_t function = 0; function < function_count; function++) {
                uint16_t vendor_id = pci_config_read16((uint8_t)bus, slot, function, 0x00);
                uint8_t found_class;
                uint8_t found_subclass;

                if (vendor_id == 0xffffu) {
                    continue;
                }

                found_class = pci_config_read8((uint8_t)bus, slot, function, 0x0b);
                found_subclass = pci_config_read8((uint8_t)bus, slot, function, 0x0a);
                if (found_class != class_code || found_subclass != subclass) {
                    continue;
                }
                if (matched++ != index) {
                    continue;
                }

                pci_fill_device_info((uint8_t)bus, slot, function, out);
                return 1;
            }
        }
    }

    return 0;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device_info *out) {
    return pci_find_device_at(vendor_id, device_id, 0u, out);
}

int pci_find_device_at(uint16_t vendor_id, uint16_t device_id, uint32_t index, struct pci_device_info *out) {
    uint32_t matched = 0u;

    if (out == 0) {
        return 0;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint8_t function_count = 1;

            if (pci_config_read16((uint8_t)bus, slot, 0, 0x00) == 0xffffu) {
                continue;
            }
            if ((pci_config_read8((uint8_t)bus, slot, 0, 0x0e) & 0x80u) != 0) {
                function_count = 8;
            }

            for (uint8_t function = 0; function < function_count; function++) {
                if (pci_config_read16((uint8_t)bus, slot, function, 0x00) != vendor_id ||
                    pci_config_read16((uint8_t)bus, slot, function, 0x02) != device_id) {
                    continue;
                }
                if (matched++ != index) {
                    continue;
                }

                pci_fill_device_info((uint8_t)bus, slot, function, out);
                return 1;
            }
        }
    }

    return 0;
}

int pci_find_ide_controller(struct pci_ide_controller *out) {
    return pci_find_ide_controller_at(0u, out);
}

int pci_find_ide_controller_at(uint32_t index, struct pci_ide_controller *out) {
    struct pci_device_info device;

    if (out == 0) {
        return 0;
    }
    if (!pci_find_device_by_class_at(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE, index, &device)) {
        return 0;
    }

    out->bus = device.bus;
    out->slot = device.slot;
    out->function = device.function;
    out->prog_if = device.prog_if;
    out->vendor_id = device.vendor_id;
    out->device_id = device.device_id;
    out->bar0 = device.bar0;
    out->bar1 = device.bar1;
    out->bar2 = device.bar2;
    out->bar3 = device.bar3;
    out->bar4 = device.bar4;
    return 1;
}

int pci_find_ahci_controller(struct pci_ahci_controller *out) {
    return pci_find_ahci_controller_at(0u, out);
}

int pci_find_ahci_controller_at(uint32_t index, struct pci_ahci_controller *out) {
    uint32_t matched = 0u;
    struct pci_device_info device;

    if (out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; pci_find_device_by_class_at(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, i, &device); i++) {
        if (device.prog_if != PCI_PROGIF_AHCI) {
            continue;
        }
        if (matched++ == index) {
            out->bus = device.bus;
            out->slot = device.slot;
            out->function = device.function;
            out->prog_if = device.prog_if;
            out->irq_line = device.irq_line;
            out->irq_pin = device.irq_pin;
            out->vendor_id = device.vendor_id;
            out->device_id = device.device_id;
            out->abar = device.bar5;
            return 1;
        }
    }
    return 0;
}

int pci_find_ac97_controller(struct pci_ac97_controller *out) {
    return pci_find_ac97_controller_at(0u, out);
}

int pci_find_ac97_controller_at(uint32_t index, struct pci_ac97_controller *out) {
    struct pci_device_info device;

    if (out == 0) {
        return 0;
    }
    if (!pci_find_device_by_class_at(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AUDIO, index, &device)) {
        return 0;
    }

    out->bus = device.bus;
    out->slot = device.slot;
    out->function = device.function;
    out->prog_if = device.prog_if;
    out->irq_line = device.irq_line;
    out->irq_pin = device.irq_pin;
    out->vendor_id = device.vendor_id;
    out->device_id = device.device_id;
    out->nambar = device.bar0;
    out->nabmbar = device.bar1;
    out->bar2 = device.bar2;
    out->bar3 = device.bar3;
    out->bar4 = device.bar4;
    out->bar5 = device.bar5;
    return 1;
}

int pci_find_hda_controller(struct pci_hda_controller *out) {
    return pci_find_hda_controller_at(0u, out);
}

int pci_find_hda_controller_at(uint32_t index, struct pci_hda_controller *out) {
    struct pci_device_info device;

    if (out == 0) {
        return 0;
    }
    if (!pci_find_device_by_class_at(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HDA, index, &device)) {
        return 0;
    }

    out->bus = device.bus;
    out->slot = device.slot;
    out->function = device.function;
    out->prog_if = device.prog_if;
    out->irq_line = device.irq_line;
    out->irq_pin = device.irq_pin;
    out->vendor_id = device.vendor_id;
    out->device_id = device.device_id;
    out->mmio_base_lo = device.bar0;
    out->mmio_base_hi = device.bar1;
    return 1;
}

int pci_find_ehci_controller_at(uint32_t index, struct pci_ehci_controller *out) {
    uint32_t matched = 0u;
    struct pci_device_info device;

    if (out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; pci_find_device_by_class_at(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, i, &device); i++) {
        if (device.prog_if != PCI_PROGIF_EHCI) {
            continue;
        }
        if (matched++ != index) {
            continue;
        }
        out->bus = device.bus;
        out->slot = device.slot;
        out->function = device.function;
        out->prog_if = device.prog_if;
        out->irq_line = device.irq_line;
        out->irq_pin = device.irq_pin;
        out->vendor_id = device.vendor_id;
        out->device_id = device.device_id;
        out->mmio_base = device.bar0;
        return 1;
    }
    return 0;
}

int pci_find_ehci_controller(struct pci_ehci_controller *out) {
    return pci_find_ehci_controller_at(0u, out);
}

int pci_find_xhci_controller(struct pci_xhci_controller *out) {
    return pci_find_xhci_controller_at(0u, out);
}

int pci_find_xhci_controller_at(uint32_t index, struct pci_xhci_controller *out) {
    uint32_t matched = 0u;
    struct pci_device_info device;

    if (out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; pci_find_device_by_class_at(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, i, &device); i++) {
        if (device.prog_if != PCI_PROGIF_XHCI) {
            continue;
        }
        if (matched++ == index) {
            out->bus = device.bus;
            out->slot = device.slot;
            out->function = device.function;
            out->prog_if = device.prog_if;
            out->irq_line = device.irq_line;
            out->irq_pin = device.irq_pin;
            out->vendor_id = device.vendor_id;
            out->device_id = device.device_id;
            out->mmio_base_lo = device.bar0;
            out->mmio_base_hi = device.bar1;
            return 1;
        }
    }
    return 0;
}
