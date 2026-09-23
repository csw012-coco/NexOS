#pragma once

#include <stddef.h>
#include <stdint.h>

#define JANUS_MAGIC 0x4A4E5331u
#define JANUS_PROTOCOL_VERSION 0x0004u
#define JANUS_MAX_MODULES 8u
#define JANUS_MAX_MEMMAP 64u

enum janus_memmap_type {
    JANUS_MEMMAP_USABLE = 1,
    JANUS_MEMMAP_RESERVED = 2,
    JANUS_MEMMAP_ACPI_RECLAIMABLE = 3,
    JANUS_MEMMAP_ACPI_NVS = 4,
    JANUS_MEMMAP_BAD = 5,
    JANUS_MEMMAP_BOOTLOADER_RECLAIMABLE = 0x1000
};

enum janus_console_type {
    JANUS_CONSOLE_NONE = 0,
    JANUS_CONSOLE_TEXT = 1,
    JANUS_CONSOLE_FRAMEBUFFER = 2
};

struct janus_proto_header {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
} __attribute__((packed));

struct janus_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct janus_console_info {
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

struct janus_module {
    char name[12];
    uint32_t address;
    uint32_t size;
} __attribute__((packed));

struct janus_boot_info {
    struct janus_proto_header hdr;
    uint8_t boot_drive;
    uint8_t reserved0[3];
    uint32_t partition_lba;
    uint32_t partition_sectors;
    uint32_t cmdline;
    uint32_t memmap_count;
    uint32_t memmap;
    struct janus_console_info console;
    uint64_t kernel_phys_addr;
    uint64_t kernel_phys_size;
    uint64_t kernel_entry;
    uint32_t module_count;
    uint32_t modules;
    uint64_t acpi_rsdp_addr;
} __attribute__((packed));
