#pragma once

#include <stddef.h>
#include <stdint.h>

#define BOOTX_MAGIC 0x42545831u
#define BOOTX_PROTOCOL_VERSION 0x0003u
#define BOOTX_MAX_MODULES 8u
#define BOOTX_MAX_MEMMAP 64u

enum bootx_memmap_type {
    BOOTX_MEMMAP_USABLE = 1,
    BOOTX_MEMMAP_RESERVED = 2,
    BOOTX_MEMMAP_ACPI_RECLAIMABLE = 3,
    BOOTX_MEMMAP_ACPI_NVS = 4,
    BOOTX_MEMMAP_BAD = 5,
    BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE = 0x1000
};

enum bootx_console_type {
    BOOTX_CONSOLE_NONE = 0,
    BOOTX_CONSOLE_TEXT = 1,
    BOOTX_CONSOLE_FRAMEBUFFER = 2
};

struct bootx_proto_header {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
} __attribute__((packed));

struct bootx_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct bootx_console_info {
    uint32_t type;
    uint32_t flags;
    uint64_t framebuffer_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t framebuffer_bpp;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint16_t text_columns;
    uint16_t text_rows;
    uint8_t text_color;
    uint8_t reserved;
} __attribute__((packed));

struct bootx_module {
    char name[12];
    uint32_t address;
    uint32_t size;
} __attribute__((packed));

struct bootx_boot_info {
    struct bootx_proto_header hdr;
    uint8_t boot_drive;
    uint8_t reserved0[3];
    uint32_t partition_lba;
    uint32_t partition_sectors;
    uint32_t cmdline;
    uint32_t memmap_count;
    uint32_t memmap;
    struct bootx_console_info console;
    uint64_t kernel_phys_addr;
    uint64_t kernel_phys_size;
    uint64_t kernel_entry;
    uint32_t module_count;
    uint32_t modules;
} __attribute__((packed));
