#ifndef BOOTX_H
#define BOOTX_H

#include <stdint.h>
#include <stddef.h>

#define BOOTX_MAGIC 0x42545831u
#define BOOTX_PROTOCOL_VERSION 0x0003u

#define STAGE3_LOAD_ADDR 0x00010000u
#define CONFIG_LOAD_ADDR 0x00020000u
#define KERNEL_LOAD_ADDR 0x00030000u
#define MODULE_LOAD_ADDR 0x02000000u
#define BOOT_INFO_ADDR 0x00018000u
#define BOOTX_PML4_ADDR 0x00021000u
#define BOOTX_PDPT_ADDR 0x00022000u
#define BOOTX_PD_ADDR   0x00023000u
#define BOOTX_PT_POOL_ADDR 0x00027000u
#define BOOTX_HIGHER_HALF_LOAD_ADDR 0x00800000u

#define KERNEL_SCRATCH_MAX (512 * 1024u)
#define BOOTX_CMDLINE_MAX 160u
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

struct bootx_bios_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

struct bootx_vbe_controller_info {
    char signature[4];
    uint16_t version;
    uint32_t oem_string_ptr;
    uint32_t capabilities;
    uint32_t video_mode_ptr;
    uint16_t total_memory;
    uint8_t reserved[236];
} __attribute__((packed));

struct bootx_vbe_mode_info {
    uint16_t mode_attributes;
    uint8_t win_a_attributes;
    uint8_t win_b_attributes;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scan_line;
    uint16_t width;
    uint16_t height;
    uint8_t char_width;
    uint8_t char_height;
    uint8_t planes;
    uint8_t bits_per_pixel;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask_size;
    uint8_t red_field_position;
    uint8_t green_mask_size;
    uint8_t green_field_position;
    uint8_t blue_mask_size;
    uint8_t blue_field_position;
    uint8_t reserved_mask_size;
    uint8_t reserved_field_position;
    uint8_t direct_color_mode_info;
    uint32_t framebuffer_addr;
    uint8_t reserved[216];
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

struct bootx_services {
    int (*bios_read_sectors)(uint8_t drive, uint32_t lba, uint8_t count, void *buffer);
    uint16_t (*bios_read_key)(void);
    int (*bios_init_framebuffer)(struct bootx_console_info *info, uint16_t width, uint16_t height, uint8_t bpp);
};

struct bootx_stage3_params {
    struct bootx_boot_info boot_info;
    uint32_t memmap_count;
    struct bootx_memmap_entry memmap[BOOTX_MAX_MEMMAP];
    const struct bootx_services *services;
};

struct rm_regs {
    uint16_t gs;
    uint16_t fs;
    uint16_t es;
    uint16_t ds;
    uint32_t eflags;
    uint32_t ebp;
    uint32_t edi;
    uint32_t esi;
    uint32_t edx;
    uint32_t ecx;
    uint32_t ebx;
    uint32_t eax;
} __attribute__((packed));

struct partition_entry {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

struct fat16_context {
    uint8_t drive;
    uint32_t partition_lba;
    uint8_t fat_type;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint32_t sectors_per_fat;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t data_lba;
    uint32_t root_cluster;
};

struct fat16_dirent {
    char name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct fat16_file {
    uint32_t first_cluster;
    uint32_t size;
    uint8_t attr;
};

struct elf32_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

enum {
    ELF_PT_LOAD = 1,
    ELF_CLASS_32 = 1,
    ELF_CLASS_64 = 2,
    ELF_MACHINE_386 = 3,
    ELF_MACHINE_X86_64 = 62
};

struct elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

void *memcpy(void *dest, const void *src, size_t len);
void *memset(void *dest, int value, size_t len);
int memcmp(const void *lhs, const void *rhs, size_t len);
size_t strlen(const char *str);
int strcmp(const char *lhs, const char *rhs);
int strncmp(const char *lhs, const char *rhs, size_t len);
char *strchr(const char *str, int ch);
void strtoupper(char *str);

void console_clear(void);
void console_putc(char ch);
void console_puts(const char *str);
void console_set_cursor(uint16_t row, uint16_t col);
void console_set_color(uint8_t color);
void console_write_at(uint16_t row, uint16_t col, uint8_t color, const char *str);
void console_write_hex(uint32_t value);
void debug_puts(const char *str);
void debug_put_hex(uint32_t value);

int bios_interrupt(uint8_t int_no, struct rm_regs *out, const struct rm_regs *in);
int bios_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer);
uint16_t bios_read_key(void);
int bios_query_e820(struct bootx_memmap_entry *entries, uint32_t *entry_count, uint32_t max_entries);
int bios_init_framebuffer(struct bootx_console_info *info, uint16_t width, uint16_t height, uint8_t bpp);
void stage3_set_services(const struct bootx_services *services);
void bootx_enter_long_mode(uint32_t pml4_phys, uint32_t entry_lo, uint32_t entry_hi, uint32_t boot_info_ptr);

int find_boot_partition(uint8_t drive, struct partition_entry *out);
int fat16_mount(struct fat16_context *ctx, uint8_t drive, uint32_t partition_lba);
int fat16_open_root(struct fat16_context *ctx, const char *name_83, struct fat16_file *out);
int fat16_open_path(struct fat16_context *ctx, const char *path, struct fat16_file *out);
int fat16_read_file(struct fat16_context *ctx, const struct fat16_file *file, void *buffer, uint32_t buffer_size);

void halt_forever(void);

#endif
