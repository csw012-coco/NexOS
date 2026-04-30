#include "bootx.h"

extern void rm_int(uint8_t int_no, struct rm_regs *out, const struct rm_regs *in);

struct bios_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed));

struct bios_dap {
    uint8_t size;
    uint8_t reserved;
    uint16_t count;
    uint16_t offset;
    uint16_t segment;
    uint64_t lba;
} __attribute__((packed));

enum {
    BIOS_XFER_SECTORS = 32
};

#define BIOS_RM_DAP_ADDR              0x1000u
#define BIOS_RM_E820_BUF_ADDR         0x1100u
#define BIOS_RM_VBE_MODE_INFO_ADDR    0x1200u
#define BIOS_RM_VBE_CTRL_INFO_ADDR    0x1400u
#define BIOS_RM_XFER_BUF_ADDR         0x2000u

static struct rm_regs bios_regs_in;
static struct rm_regs bios_regs_out;
static struct bios_dap *const bios_dap = (struct bios_dap *)(uintptr_t)BIOS_RM_DAP_ADDR;
static uint8_t *const bios_xfer_buf = (uint8_t *)(uintptr_t)BIOS_RM_XFER_BUF_ADDR;
static struct bios_e820_entry *const bios_e820_buf = (struct bios_e820_entry *)(uintptr_t)BIOS_RM_E820_BUF_ADDR;
static struct bootx_vbe_mode_info *const bios_vbe_mode_info =
    (struct bootx_vbe_mode_info *)(uintptr_t)BIOS_RM_VBE_MODE_INFO_ADDR;
static struct bootx_vbe_controller_info *const bios_vbe_controller_info =
    (struct bootx_vbe_controller_info *)(uintptr_t)BIOS_RM_VBE_CTRL_INFO_ADDR;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int ata_wait_ready(void) {
    for (uint32_t i = 0; i < 200000u; i++) {
        uint8_t status = inb(0x1F7);
        if ((status & 0x80u) == 0 && (status & 0x40u) != 0) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 200000u; i++) {
        uint8_t status = inb(0x1F7);
        if ((status & 0x01u) != 0) {
            return -1;
        }
        if ((status & 0x80u) == 0 && (status & 0x08u) != 0) {
            return 0;
        }
    }
    return -1;
}

static int ata_pio_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    uint16_t *dest = (uint16_t *)buffer;

    for (uint8_t sector = 0; sector < count; sector++) {
        if (ata_wait_ready() != 0) {
            return -1;
        }

        outb(0x1F6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        inb(0x3F6);
        inb(0x3F6);
        inb(0x3F6);
        inb(0x3F6);
        outb(0x1F2, 1);
        outb(0x1F3, (uint8_t)lba);
        outb(0x1F4, (uint8_t)(lba >> 8));
        outb(0x1F5, (uint8_t)(lba >> 16));
        outb(0x1F7, 0x20);

        if (ata_wait_drq() != 0) {
            return -1;
        }

        for (int i = 0; i < 256; i++) {
            *dest++ = inw(0x1F0);
        }
        lba++;
    }

    return 0;
}

int bios_interrupt(uint8_t int_no, struct rm_regs *out, const struct rm_regs *in) {
    memcpy(&bios_regs_in, in, sizeof(bios_regs_in));
    memset(&bios_regs_out, 0, sizeof(bios_regs_out));
    rm_int(int_no, &bios_regs_out, &bios_regs_in);
    memcpy(out, &bios_regs_out, sizeof(*out));
    return (out->eflags & 1u) ? -1 : 0;
}

int bios_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer) {
    uint8_t *dest = (uint8_t *)buffer;

    while (count != 0) {
        struct rm_regs r;
        uint16_t chunk = count;
        uintptr_t xfer_ptr = (uintptr_t)dest;
        int needs_bounce = 0;
        int used_bounce = 0;

        if (chunk > BIOS_XFER_SECTORS) {
            chunk = BIOS_XFER_SECTORS;
        }
        if (xfer_ptr >= 0x100000u) {
            needs_bounce = 1;
        } else if (((xfer_ptr & 0xFFFFu) + (uintptr_t)chunk * 512u) > 0x10000u) {
            needs_bounce = 1;
        }
        if (needs_bounce || xfer_ptr >= 0x10000u) {
            xfer_ptr = (uintptr_t)bios_xfer_buf;
            used_bounce = 1;
        }

        memset(bios_dap, 0, sizeof(*bios_dap));
        bios_dap->size = 0x10;
        bios_dap->count = chunk;
        bios_dap->offset = (uint16_t)xfer_ptr;
        bios_dap->segment = 0;
        bios_dap->lba = lba;

        memset(&r, 0, sizeof(r));
        r.eax = 0x4200u;
        r.edx = drive;
        r.esi = BIOS_RM_DAP_ADDR;
        r.ds = 0;
        r.eflags = 0x00000202u;

        if (bios_interrupt(0x13, &r, &r) != 0 || ((r.eax >> 8) & 0xFFu) == 0x42u) {
            debug_puts("bios: int13 fallback to ata\n");
            if (ata_pio_read_sectors(lba, (uint8_t)chunk, dest) != 0) {
                return -1;
            }
            dest += (size_t)chunk * 512u;
            lba += chunk;
            count = (uint8_t)(count - chunk);
            continue;
        }

        if (used_bounce) {
            memcpy(dest, (const void *)xfer_ptr, (size_t)chunk * 512u);
        }
        dest += (size_t)chunk * 512u;
        lba += chunk;
        count = (uint8_t)(count - chunk);
    }

    return 0;
}

uint16_t bios_read_key(void) {
    uint8_t extended = 0;
    for (;;) {
        if ((inb(0x64) & 1u) == 0) {
            continue;
        }

        uint8_t code = inb(0x60);
        if (code == 0xE0) {
            extended = 1;
            continue;
        }
        if (code & 0x80) {
            extended = 0;
            continue;
        }
        if (code == 0x1C) {
            return '\r';
        }
        if (code >= 0x02 && code <= 0x0A) {
            return (uint16_t)('1' + (code - 0x02));
        }
        if (code == 0x11) {
            return 'w';
        }
        if (code == 0x1F) {
            return 's';
        }
        if (code == 0x24) {
            return 'j';
        }
        if (code == 0x25) {
            return 'k';
        }
        if (extended && code == 0x48) {
            extended = 0;
            return 0x4800;
        }
        if (extended && code == 0x50) {
            extended = 0;
            return 0x5000;
        }
    }
}

int bios_query_e820(struct bootx_memmap_entry *entries, uint32_t *entry_count, uint32_t max_entries) {
    struct rm_regs r;
    uint32_t count = 0;
    memset(&r, 0, sizeof(r));

    while (count < max_entries) {
        memset(bios_e820_buf, 0, sizeof(*bios_e820_buf));
        r.eax = 0xE820;
        r.ecx = 20;
        r.edx = 0x534D4150u;
        r.edi = BIOS_RM_E820_BUF_ADDR;
        r.es = 0;
        r.eflags = 0x00000202u;

        if (bios_interrupt(0x15, &r, &r) != 0) {
            debug_puts("e820: int15 failed cf eflags=");
            debug_put_hex(r.eflags);
            debug_puts(" eax=");
            debug_put_hex(r.eax);
            debug_puts(" ebx=");
            debug_put_hex(r.ebx);
            debug_puts("\n");
            break;
        }
        if (r.eax != 0x534D4150u) {
            debug_puts("e820: bad signature eax=");
            debug_put_hex(r.eax);
            debug_puts(" ebx=");
            debug_put_hex(r.ebx);
            debug_puts(" ecx=");
            debug_put_hex(r.ecx);
            debug_puts("\n");
            break;
        }
        if (bios_e820_buf->length != 0) {
            entries[count].base = bios_e820_buf->base;
            entries[count].length = bios_e820_buf->length;
            entries[count].type = bios_e820_buf->type;
            entries[count].reserved = 0;
            count++;
        }

        if (r.ebx == 0) {
            break;
        }
    }

    *entry_count = count;
    return count == 0 ? -1 : 0;
}

static uintptr_t vbe_far_ptr_to_linear(uint32_t far_ptr) {
    uint32_t offset = far_ptr & 0xFFFFu;
    uint32_t segment = far_ptr >> 16;
    return (uintptr_t)((segment << 4) + offset);
}

static int vbe_query_mode(uint16_t mode) {
    struct rm_regs r;

    memset(bios_vbe_mode_info, 0, sizeof(*bios_vbe_mode_info));
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F01u;
    r.ecx = mode;
    r.edi = BIOS_RM_VBE_MODE_INFO_ADDR;
    r.es = 0;
    r.eflags = 0x00000202u;

    return bios_interrupt(0x10, &r, &r);
}

static int vbe_mode_matches_request(uint16_t width, uint16_t height, uint8_t bpp) {
    if (bios_vbe_mode_info->framebuffer_addr == 0) {
        return 0;
    }
    if (bios_vbe_mode_info->memory_model != 6) {
        return 0;
    }
    if (bios_vbe_mode_info->bits_per_pixel < 24) {
        return 0;
    }
    if (width != 0 && bios_vbe_mode_info->width != width) {
        return 0;
    }
    if (height != 0 && bios_vbe_mode_info->height != height) {
        return 0;
    }
    if (bpp != 0 && bios_vbe_mode_info->bits_per_pixel != bpp) {
        return 0;
    }
    return 1;
}

int bios_init_framebuffer(struct bootx_console_info *info, uint16_t width, uint16_t height, uint8_t bpp) {
    struct rm_regs r;
    uintptr_t mode_list_addr;
    const uint16_t *mode_list;
    uint16_t selected_mode = 0;
    uint8_t selected_bpp = 0;
    uint16_t target_width = width;
    uint16_t target_height = height;
    uint8_t target_bpp = bpp;

    if (info == 0) {
        return -1;
    }

    if (target_width == 0 && target_height == 0) {
        target_width = 1024;
        target_height = 768;
    }

    debug_puts("bios: framebuffer request ");
    debug_put_hex(target_width);
    debug_puts("x");
    debug_put_hex(target_height);
    debug_puts(" bpp=");
    debug_put_hex(target_bpp);
    debug_puts("\n");

    memset(info, 0, sizeof(*info));
    memset(bios_vbe_controller_info, 0, sizeof(*bios_vbe_controller_info));
    memcpy(bios_vbe_controller_info->signature, "VBE2", 4);
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F00u;
    r.edi = BIOS_RM_VBE_CTRL_INFO_ADDR;
    r.es = 0;
    r.eflags = 0x00000202u;

    if (bios_interrupt(0x10, &r, &r) == 0) {
    } else {
        debug_puts("bios: vbe controller query failed\n");
        goto fallback_mode13;
    }

    if (memcmp(bios_vbe_controller_info->signature, "VESA", 4) != 0) {
        debug_puts("bios: vbe signature missing\n");
        goto fallback_mode13;
    }

    mode_list_addr = vbe_far_ptr_to_linear(bios_vbe_controller_info->video_mode_ptr);
    if (mode_list_addr < 0x500u || mode_list_addr >= 0xA0000u) {
        debug_puts("bios: bad vbe mode list ptr\n");
        goto fallback_mode13;
    }
    mode_list = (const uint16_t *)(uintptr_t)mode_list_addr;

    for (uint32_t i = 0; i < 256; i++) {
        uint16_t mode = mode_list[i];
        if (mode == 0xFFFFu) {
            break;
        }
        if (vbe_query_mode(mode) != 0) {
            continue;
        }
        if (!vbe_mode_matches_request(target_width, target_height, target_bpp)) {
            continue;
        }
        if (bios_vbe_mode_info->bits_per_pixel > selected_bpp) {
            selected_mode = mode;
            selected_bpp = bios_vbe_mode_info->bits_per_pixel;
        }
        if (target_bpp != 0 || selected_bpp >= 32) {
            break;
        }
    }

    if (selected_mode != 0 && vbe_query_mode(selected_mode) == 0) {
        memset(&r, 0, sizeof(r));
        r.eax = 0x4F02u;
        r.ebx = 0x4000u | selected_mode;
        r.eflags = 0x00000202u;

        if (bios_interrupt(0x10, &r, &r) != 0) {
            debug_puts("bios: vbe set mode failed\n");
            goto fallback_mode13;
        }

        info->type = BOOTX_CONSOLE_FRAMEBUFFER;
        info->flags = 1;
        info->framebuffer_addr = bios_vbe_mode_info->framebuffer_addr;
        info->width = bios_vbe_mode_info->width;
        info->height = bios_vbe_mode_info->height;
        info->pitch = bios_vbe_mode_info->bytes_per_scan_line;
        info->framebuffer_bpp = bios_vbe_mode_info->bits_per_pixel;
        info->red_mask_size = bios_vbe_mode_info->red_mask_size;
        info->red_mask_shift = bios_vbe_mode_info->red_field_position;
        info->green_mask_size = bios_vbe_mode_info->green_mask_size;
        info->green_mask_shift = bios_vbe_mode_info->green_field_position;
        info->blue_mask_size = bios_vbe_mode_info->blue_mask_size;
        info->blue_mask_shift = bios_vbe_mode_info->blue_field_position;
        debug_puts("bios: vbe mode ");
        debug_put_hex(selected_mode);
        debug_puts(" ");
        debug_put_hex(info->width);
        debug_puts("x");
        debug_put_hex(info->height);
        debug_puts(" bpp=");
        debug_put_hex(info->framebuffer_bpp);
        debug_puts("\n");
        return 0;
    }

fallback_mode13:
    debug_puts("bios: vbe framebuffer unavailable\n");

    memset(&r, 0, sizeof(r));
    r.eax = 0x0013u;
    r.eflags = 0x00000202u;
    if (bios_interrupt(0x10, &r, &r) != 0) {
        debug_puts("bios: mode13 fallback failed\n");
        return -1;
    }

    info->type = BOOTX_CONSOLE_FRAMEBUFFER;
    info->flags = 2;
    info->framebuffer_addr = 0xA0000u;
    info->width = 320;
    info->height = 200;
    info->pitch = 320;
    info->framebuffer_bpp = 8;
    debug_puts("bios: mode13 framebuffer fallback active\n");
    return 0;
}
