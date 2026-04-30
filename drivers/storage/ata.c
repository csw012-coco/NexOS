#include "drivers/storage/ata.h"
#include "drivers/bus/pci.h"
#include "hal/hal.h"

enum {
    ATA_REG_DATA = 0x00,
    ATA_REG_SECCOUNT0 = 0x02,
    ATA_REG_LBA0 = 0x03,
    ATA_REG_LBA1 = 0x04,
    ATA_REG_LBA2 = 0x05,
    ATA_REG_HDDEVSEL = 0x06,
    ATA_REG_COMMAND = 0x07,
    ATA_REG_STATUS = 0x07,

    ATA_CMD_IDENTIFY = 0xec,
    ATA_CMD_READ_PIO = 0x20,
    ATA_CMD_WRITE_PIO = 0x30,
    ATA_CMD_CACHE_FLUSH = 0xe7,

    ATA_SR_ERR = 0x01,
    ATA_SR_DRQ = 0x08,
    ATA_SR_DF = 0x20,
    ATA_SR_BSY = 0x80
};

static struct ata_device ata_devices[] = {
    {
        .io_base = 0x1f0,
        .ctrl_base = 0x3f6,
        .slave = 0
    },
    {
        .io_base = 0x1f0,
        .ctrl_base = 0x3f6,
        .slave = 1
    },
    {
        .io_base = 0x170,
        .ctrl_base = 0x376,
        .slave = 0
    },
    {
        .io_base = 0x170,
        .ctrl_base = 0x376,
        .slave = 1
    }
};

static void ata_delay_400ns(struct ata_device *dev) {
    (void)hal_io_in8(dev->ctrl_base);
    (void)hal_io_in8(dev->ctrl_base);
    (void)hal_io_in8(dev->ctrl_base);
    (void)hal_io_in8(dev->ctrl_base);
}

static int ata_poll(struct ata_device *dev, int check_error) {
    uint8_t status;

    ata_delay_400ns(dev);
    for (;;) {
        status = hal_io_in8(dev->io_base + ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            break;
        }
    }

    if (check_error) {
        if ((status & ATA_SR_ERR) != 0 || (status & ATA_SR_DF) != 0) {
            return -1;
        }
        if ((status & ATA_SR_DRQ) == 0) {
            return -1;
        }
    }

    return 0;
}

static int ata_wait_idle(struct ata_device *dev) {
    uint8_t status;

    if (dev == 0) {
        return -1;
    }
    ata_delay_400ns(dev);
    for (;;) {
        status = hal_io_in8(dev->io_base + ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            break;
        }
    }
    if ((status & ATA_SR_ERR) != 0 || (status & ATA_SR_DF) != 0) {
        return -1;
    }
    return 0;
}

static void ata_select_drive(struct ata_device *dev) {
    hal_io_out8(dev->io_base + ATA_REG_HDDEVSEL, (uint8_t)(0xe0 | (dev->slave << 4)));
    ata_delay_400ns(dev);
}

static uint16_t ata_bar_io_base(uint32_t bar, uint16_t fallback) {
    uint16_t base;

    if ((bar & 0x1u) == 0) {
        return fallback;
    }
    base = (uint16_t)(bar & ~0x3u);
    return base != 0 ? base : fallback;
}

static uint16_t ata_bar_ctrl_base(uint32_t bar, uint16_t fallback) {
    uint16_t base;

    if ((bar & 0x1u) == 0) {
        return fallback;
    }
    base = (uint16_t)(bar & ~0x3u);
    if (base == 0) {
        return fallback;
    }
    return (uint16_t)(base + 2u);
}

static void ata_configure_channels(void) {
    struct pci_ide_controller ide;
    uint16_t primary_io = 0x1f0;
    uint16_t primary_ctrl = 0x3f6;
    uint16_t secondary_io = 0x170;
    uint16_t secondary_ctrl = 0x376;

    if (pci_find_ide_controller(&ide)) {
        if ((ide.prog_if & 0x01u) != 0) {
            primary_io = ata_bar_io_base(ide.bar0, primary_io);
            primary_ctrl = ata_bar_ctrl_base(ide.bar1, primary_ctrl);
        }
        if ((ide.prog_if & 0x04u) != 0) {
            secondary_io = ata_bar_io_base(ide.bar2, secondary_io);
            secondary_ctrl = ata_bar_ctrl_base(ide.bar3, secondary_ctrl);
        }
    }

    ata_devices[0].io_base = primary_io;
    ata_devices[0].ctrl_base = primary_ctrl;
    ata_devices[1].io_base = primary_io;
    ata_devices[1].ctrl_base = primary_ctrl;
    ata_devices[2].io_base = secondary_io;
    ata_devices[2].ctrl_base = secondary_ctrl;
    ata_devices[3].io_base = secondary_io;
    ata_devices[3].ctrl_base = secondary_ctrl;
}

static void ata_swap_ident_string(char *dest, const uint16_t *src, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        dest[i * 2] = (char)(src[i] >> 8);
        dest[i * 2 + 1] = (char)(src[i] & 0xff);
    }
    dest[words * 2] = '\0';

    for (int32_t i = (int32_t)(words * 2 - 1); i >= 0; i--) {
        if (dest[i] == ' ' || dest[i] == '\0') {
            dest[i] = '\0';
        } else {
            break;
        }
    }
}

static int ata_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct ata_device *dev = (struct ata_device *)bdev->driver_data;
    uint16_t *words = (uint16_t *)buffer;

    if (dev == 0 || !dev->present || count == 0 || count > 255) {
        return -1;
    }
    if (lba + count > dev->sector_count || lba > 0x0fffffffULL) {
        return -1;
    }

    ata_select_drive(dev);
    hal_io_out8(dev->io_base + ATA_REG_SECCOUNT0, (uint8_t)count);
    hal_io_out8(dev->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xff));
    hal_io_out8(dev->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xff));
    hal_io_out8(dev->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xff));
    hal_io_out8(dev->io_base + ATA_REG_HDDEVSEL,
                (uint8_t)(0xe0 | (dev->slave << 4) | ((lba >> 24) & 0x0f)));
    hal_io_out8(dev->io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (uint32_t sector = 0; sector < count; sector++) {
        if (ata_poll(dev, 1) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < 256; i++) {
            words[sector * 256 + i] = hal_io_in16(dev->io_base + ATA_REG_DATA);
        }
        ata_delay_400ns(dev);
    }

    if (ata_wait_idle(dev) != 0) {
        return -1;
    }

    return 0;
}

static int ata_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct ata_device *dev = (struct ata_device *)bdev->driver_data;
    const uint16_t *words = (const uint16_t *)buffer;

    if (dev == 0 || !dev->present || count == 0 || count > 255) {
        return -1;
    }
    if (lba + count > dev->sector_count || lba > 0x0fffffffULL) {
        return -1;
    }

    ata_select_drive(dev);
    hal_io_out8(dev->io_base + ATA_REG_SECCOUNT0, (uint8_t)count);
    hal_io_out8(dev->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xff));
    hal_io_out8(dev->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xff));
    hal_io_out8(dev->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xff));
    hal_io_out8(dev->io_base + ATA_REG_HDDEVSEL,
                (uint8_t)(0xe0 | (dev->slave << 4) | ((lba >> 24) & 0x0f)));
    hal_io_out8(dev->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    for (uint32_t sector = 0; sector < count; sector++) {
        if (ata_poll(dev, 1) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < 256; i++) {
            hal_io_out16(dev->io_base + ATA_REG_DATA, words[sector * 256 + i]);
        }
        if (ata_wait_idle(dev) != 0) {
            return -1;
        }
    }

    hal_io_out8(dev->io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (ata_wait_idle(dev) != 0) {
        return -1;
    }

    return 0;
}

static int ata_identify(struct ata_device *dev) {
    uint16_t identify[256];
    uint8_t status;

    ata_select_drive(dev);
    hal_io_out8(dev->io_base + ATA_REG_SECCOUNT0, 0);
    hal_io_out8(dev->io_base + ATA_REG_LBA0, 0);
    hal_io_out8(dev->io_base + ATA_REG_LBA1, 0);
    hal_io_out8(dev->io_base + ATA_REG_LBA2, 0);
    hal_io_out8(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    status = hal_io_in8(dev->io_base + ATA_REG_STATUS);
    if (status == 0) {
        return -1;
    }

    for (;;) {
        status = hal_io_in8(dev->io_base + ATA_REG_STATUS);
        if ((status & ATA_SR_ERR) != 0) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ) != 0) {
            break;
        }
    }

    for (uint32_t i = 0; i < 256; i++) {
        identify[i] = hal_io_in16(dev->io_base + ATA_REG_DATA);
    }

    dev->sector_count = ((uint32_t)identify[61] << 16) | identify[60];
    ata_swap_ident_string(dev->model, &identify[27], 20);
    dev->blockdev.block_size = 512;
    dev->blockdev.block_count = dev->sector_count;
    dev->blockdev.read = ata_read_impl;
    dev->blockdev.write = ata_write_impl;
    dev->blockdev.driver_data = dev;
    dev->present = 1;
    return 0;
}

void ata_init(void) {
    static const char *names[] = {
        "ata0",
        "ata1",
        "ata2",
        "ata3"
    };

    ata_configure_channels();

    for (uint32_t i = 0; i < (uint32_t)(sizeof(ata_devices) / sizeof(ata_devices[0])); i++) {
        ata_devices[i].present = 0;
        ata_devices[i].model[0] = '\0';
        ata_devices[i].blockdev.name = names[i];
        ata_devices[i].blockdev.block_size = 512;
        ata_devices[i].blockdev.block_count = 0;
        ata_devices[i].blockdev.read = ata_read_impl;
        ata_devices[i].blockdev.write = ata_write_impl;
        ata_devices[i].blockdev.driver_data = &ata_devices[i];

        if (ata_identify(&ata_devices[i]) == 0) {
            blockdev_register(&ata_devices[i].blockdev);
        }
    }
}

struct ata_device *ata_get_primary_master(void) {
    return &ata_devices[0];
}
