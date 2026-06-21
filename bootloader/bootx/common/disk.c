#include "bootx.h"

int find_boot_partition(uint8_t drive, struct partition_entry *out) {
    static uint8_t sector[512];
    if (bios_read_sectors(drive, 0, 1, sector) != 0) {
        return -1;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return -1;
    }

    struct partition_entry *parts = (struct partition_entry *)(sector + 446);
    struct partition_entry *fallback = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t type = parts[i].type;
        if (type == 0x04 || type == 0x06 || type == 0x0E ||
            type == 0x0B || type == 0x0C || type == 0x1B || type == 0x1C) {
            if ((parts[i].status & 0x80) != 0) {
                *out = parts[i];
                return 0;
            }
            if (fallback == 0) {
                fallback = &parts[i];
            }
        }
    }

    if (fallback != 0) {
        *out = *fallback;
        return 0;
    }
    return -1;
}
