#include "bootx.h"

static const struct bootx_services *g_services;

void stage3_set_services(const struct bootx_services *services) {
    g_services = services;
}

int bios_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer) {
    if (g_services == 0 || g_services->bios_read_sectors == 0) {
        return -1;
    }
    return g_services->bios_read_sectors(drive, lba, count, buffer);
}

uint16_t bios_read_key(void) {
    if (g_services == 0 || g_services->bios_read_key == 0) {
        return 0;
    }
    return g_services->bios_read_key();
}

int bios_init_framebuffer(struct bootx_console_info *info, uint16_t width, uint16_t height, uint8_t bpp) {
    if (g_services == 0 || g_services->bios_init_framebuffer == 0) {
        return -1;
    }
    return g_services->bios_init_framebuffer(info, width, height, bpp);
}
