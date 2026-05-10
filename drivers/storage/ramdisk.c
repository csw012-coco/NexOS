#include "drivers/storage/ramdisk.h"
#include "block/blockdev.h"
#include "lib/string.h"

enum {
    RAMDISK_MAX = 2,
    RAMDISK_BLOCK_SIZE = 512u
};

struct ramdisk_device {
    char name[12];
    uint8_t *base;
    uint64_t size;
    uint32_t disk_index;
    uint8_t writable;
    struct block_device dev;
};

static struct ramdisk_device g_ramdisks[RAMDISK_MAX];
static uint32_t g_ramdisk_first_disk_index = 0xffffffffu;

static void ramdisk_reset(struct ramdisk_device *ramdisk) {
    if (ramdisk == 0) {
        return;
    }
    for (uint32_t i = 0; i < sizeof(ramdisk->name); i++) {
        ramdisk->name[i] = '\0';
    }
    ramdisk->base = 0;
    ramdisk->size = 0;
    ramdisk->disk_index = 0xffffffffu;
    ramdisk->writable = 0u;
    ramdisk->dev.name = ramdisk->name;
    ramdisk->dev.block_size = RAMDISK_BLOCK_SIZE;
    ramdisk->dev.block_count = 0;
    ramdisk->dev.read = 0;
    ramdisk->dev.write = 0;
    ramdisk->dev.driver_data = ramdisk;
}

static int ramdisk_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    struct ramdisk_device *ramdisk = 0;
    uint8_t *out = (uint8_t *)buffer;
    uint64_t start;
    uint64_t bytes;

    if (dev == 0 || buffer == 0 || count == 0) {
        return -1;
    }
    ramdisk = (struct ramdisk_device *)dev->driver_data;
    if (ramdisk == 0 || ramdisk->base == 0 || dev->block_size == 0) {
        return -1;
    }
    if (lba >= dev->block_count || (uint64_t)count > dev->block_count - lba) {
        return -1;
    }

    start = lba * dev->block_size;
    bytes = (uint64_t)count * dev->block_size;
    if (start + bytes > ramdisk->size) {
        return -1;
    }
    for (uint64_t i = 0; i < bytes; i++) {
        out[i] = ramdisk->base[start + i];
    }
    return 0;
}

static int ramdisk_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    struct ramdisk_device *ramdisk = 0;
    const uint8_t *in = (const uint8_t *)buffer;
    uint64_t start;
    uint64_t bytes;

    if (dev == 0 || buffer == 0 || count == 0) {
        return -1;
    }
    ramdisk = (struct ramdisk_device *)dev->driver_data;
    if (ramdisk == 0 || ramdisk->base == 0 || dev->block_size == 0 || !ramdisk->writable) {
        return -1;
    }
    if (lba >= dev->block_count || (uint64_t)count > dev->block_count - lba) {
        return -1;
    }

    start = lba * dev->block_size;
    bytes = (uint64_t)count * dev->block_size;
    if (start + bytes > ramdisk->size) {
        return -1;
    }
    for (uint64_t i = 0; i < bytes; i++) {
        ramdisk->base[start + i] = in[i];
    }
    return 0;
}

static int ramdisk_module_is_writable(const char name[12]) {
    return name != 0 && streq(name, "RAMDISK IMG");
}

static int ramdisk_module_is_block_image(const char name[12]) {
    return name != 0 && name[8] == 'I' && name[9] == 'M' && name[10] == 'G';
}

static void ramdisk_name_to_83(char out[12], const char *name) {
    uint32_t pos = 0;
    uint32_t ext = 8;

    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    out[11] = '\0';
    if (name == 0) {
        return;
    }
    while (*name != '\0') {
        char ch = *name++;

        if (ch == '.') {
            pos = 8;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - ('a' - 'A'));
        }
        if (pos < 8) {
            out[pos++] = ch;
        } else if (ext < 11) {
            out[ext++] = ch;
            pos = ext;
        }
    }
}

void ramdisk_init_from_boot_modules(const struct bootx_boot_info *boot_info) {
    const struct bootx_module *modules;
    uint32_t registered = 0;

    g_ramdisk_first_disk_index = 0xffffffffu;
    for (uint32_t i = 0; i < RAMDISK_MAX; i++) {
        ramdisk_reset(&g_ramdisks[i]);
    }
    if (boot_info == 0 || boot_info->module_count == 0 || boot_info->modules == 0) {
        return;
    }

    modules = (const struct bootx_module *)(uintptr_t)boot_info->modules;
    for (uint32_t i = 0; i < boot_info->module_count && registered < RAMDISK_MAX; i++) {
        struct ramdisk_device *ramdisk = &g_ramdisks[registered];
        uint64_t size = modules[i].size;

        if (modules[i].address == 0 || size < RAMDISK_BLOCK_SIZE ||
            !ramdisk_module_is_block_image(modules[i].name)) {
            continue;
        }
        for (uint32_t j = 0; j < sizeof(ramdisk->name); j++) {
            ramdisk->name[j] = modules[i].name[j];
        }
        ramdisk->base = (uint8_t *)(uintptr_t)modules[i].address;
        ramdisk->size = size;
        ramdisk->writable = (uint8_t)ramdisk_module_is_writable(ramdisk->name);
        ramdisk->dev.block_size = RAMDISK_BLOCK_SIZE;
        ramdisk->dev.block_count = size / RAMDISK_BLOCK_SIZE;
        ramdisk->dev.read = ramdisk_read;
        ramdisk->dev.write = ramdisk->writable ? ramdisk_write : 0;
        if (ramdisk->dev.block_count == 0) {
            continue;
        }
        if (blockdev_register(&ramdisk->dev) != 0) {
            break;
        }
        ramdisk->disk_index = blockdev_count() - 1u;
        if (g_ramdisk_first_disk_index == 0xffffffffu) {
            g_ramdisk_first_disk_index = ramdisk->disk_index;
        }
        registered++;
    }
}

uint32_t ramdisk_first_disk_index(void) {
    return g_ramdisk_first_disk_index;
}

uint32_t ramdisk_disk_index_by_name(const char *name) {
    char name83[12];

    if (name == 0 || name[0] == '\0') {
        return 0xffffffffu;
    }
    ramdisk_name_to_83(name83, name);
    for (uint32_t i = 0; i < RAMDISK_MAX; i++) {
        if (g_ramdisks[i].dev.read == 0) {
            continue;
        }
        if (streq(g_ramdisks[i].name, name83)) {
            return g_ramdisks[i].disk_index;
        }
    }
    return 0xffffffffu;
}
