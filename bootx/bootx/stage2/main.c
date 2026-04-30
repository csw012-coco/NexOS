#include "bootx.h"

typedef void (*stage3_entry_t)(const struct bootx_boot_info *boot_info);

extern uint16_t early_e820_count;
extern struct bootx_bios_e820_entry early_e820_entries[];

static struct bootx_stage3_params stage3_params;
static const struct bootx_services stage3_services = {
    .bios_read_sectors = bios_read_sectors,
    .bios_read_key = bios_read_key,
    .bios_init_framebuffer = bios_init_framebuffer,
};

static void fail(const char *message) {
    debug_puts("stage2 fail: ");
    debug_puts(message);
    debug_puts("\n");
    console_set_color(0x0C);
    console_puts("stage2: ");
    console_puts(message);
    console_putc('\n');
    halt_forever();
}

void stage2_main(uint32_t drive) {
    struct partition_entry part;
    struct fat16_context fat;
    struct fat16_file stage3;

    console_set_color(0x07);
    console_clear();
    console_puts("boot/x stage2\n");
    debug_puts("stage2: entered\n");

    if (find_boot_partition((uint8_t)drive, &part) != 0) {
        fail("no FAT partition");
    }
    debug_puts("stage2: found partition\n");
    debug_puts("stage2: partition lba ");
    debug_put_hex(part.lba_start);
    debug_puts("\n");
    if (fat16_mount(&fat, (uint8_t)drive, part.lba_start) != 0) {
        fail("mount failed");
    }
    debug_puts("stage2: mounted FAT\n");
    if (fat16_open_path(&fat, "BOOT/STAGE3.SYS", &stage3) != 0 &&
        fat16_open_path(&fat, "STAGE3.SYS", &stage3) != 0) {
        fail("STAGE3.SYS missing");
    }
    debug_puts("stage2: found stage3\n");
    if (fat16_read_file(&fat, &stage3, (void *)STAGE3_LOAD_ADDR, 128 * 1024u) != 0) {
        fail("stage3 load failed");
    }
    debug_puts("stage2: loaded stage3\n");

    memset(&stage3_params, 0, sizeof(stage3_params));
    stage3_params.boot_info.hdr.magic = BOOTX_MAGIC;
    stage3_params.boot_info.hdr.version = BOOTX_PROTOCOL_VERSION;
    stage3_params.boot_info.hdr.size = sizeof(stage3_params.boot_info);
    stage3_params.boot_info.boot_drive = (uint8_t)drive;
    stage3_params.boot_info.partition_lba = part.lba_start;
    stage3_params.boot_info.partition_sectors = part.sector_count;
    if (early_e820_count == 0) {
        debug_puts("stage2: E820 unavailable, using fallback later\n");
        stage3_params.memmap_count = 0;
    } else {
        stage3_params.memmap_count = early_e820_count;
        for (uint32_t i = 0; i < stage3_params.memmap_count; i++) {
            stage3_params.memmap[i].base = early_e820_entries[i].base;
            stage3_params.memmap[i].length = early_e820_entries[i].length;
            stage3_params.memmap[i].type = early_e820_entries[i].type;
            stage3_params.memmap[i].reserved = 0;
        }
        debug_puts("stage2: E820 entries ");
        debug_put_hex(stage3_params.memmap_count);
        debug_puts("\n");
    }
    stage3_params.services = &stage3_services;

    console_puts("jumping to stage3...\n");
    debug_puts("stage2: jump stage3\n");
    ((stage3_entry_t)STAGE3_LOAD_ADDR)((const struct bootx_boot_info *)&stage3_params);

    fail("stage3 returned");
}
