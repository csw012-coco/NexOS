#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static const char *sysinfo_skip_spaces_local(const char *text) {
    while (text != NULL && (*text == ' ' || *text == '\t')) {
        text++;
    }
    return text != NULL ? text : "";
}

static void sysinfo_write_date_time_local(const struct syscall_rtc_info *rtc) {
    write_dec(rtc->year);
    write_str("-");
    if (rtc->month < 10u) {
        write_str("0");
    }
    write_dec(rtc->month);
    write_str("-");
    if (rtc->day < 10u) {
        write_str("0");
    }
    write_dec(rtc->day);
    write_str(" ");
    if (rtc->hour < 10u) {
        write_str("0");
    }
    write_dec(rtc->hour);
    write_str(":");
    if (rtc->minute < 10u) {
        write_str("0");
    }
    write_dec(rtc->minute);
    write_str(":");
    if (rtc->second < 10u) {
        write_str("0");
    }
    write_dec(rtc->second);
}

static int sysinfo_memory_totals_local(uint64_t *total_out,
                                       uint64_t *usable_out,
                                       uint64_t *reserved_out,
                                       uint32_t *entries_out) {
    struct syscall_memmap_info mem;
    uint64_t total = 0;
    uint64_t usable = 0;
    uint64_t reserved = 0;
    uint32_t entries = 0;

    while (memmap_query(entries, &mem) > 0) {
        total += mem.length;
        if (mem.type == BOOTX_MEMMAP_USABLE ||
            mem.type == BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            usable += mem.length;
        } else {
            reserved += mem.length;
        }
        entries++;
    }
    if (total_out != NULL) {
        *total_out = total;
    }
    if (usable_out != NULL) {
        *usable_out = usable;
    }
    if (reserved_out != NULL) {
        *reserved_out = reserved;
    }
    if (entries_out != NULL) {
        *entries_out = entries;
    }
    return entries != 0u;
}

static void sysinfo_write_help_local(void) {
    write_str("usage: sysinfo [-a]\n");
    write_str("show NexOS system, kernel, CPU, memory, and boot information\n");
    write_str("  -a, --all   include CPUID and memory-map counters\n");
}

int cmd_sysinfo(int argc, char **argv) {
    struct syscall_machine_info machine;
    struct syscall_pmm_info pmm;
    struct syscall_boot_info boot;
    struct syscall_rtc_info rtc;
    uint64_t mem_total = 0;
    uint64_t mem_usable = 0;
    uint64_t mem_reserved = 0;
    uint32_t mem_entries = 0;
    int verbose = 0;

    if (argc > 2) {
        write_err_usage("sysinfo", " [-a]\n");
        return 1;
    }
    if (argc == 2) {
        if (streq_local(argv[1], "-h") || streq_local(argv[1], "--help")) {
            sysinfo_write_help_local();
            return 0;
        }
        if (streq_local(argv[1], "-a") || streq_local(argv[1], "--all")) {
            verbose = 1;
        } else {
            write_err_usage("sysinfo", " [-a]\n");
            return 1;
        }
    }

    if (machine_info_query(&machine) <= 0) {
        write_err_str("sysinfo: machine info query failed\n");
        return 1;
    }

    write_str("NexOS system information\n");
    write_str("os:       ");
    write_str(machine.os_name);
    write_str("\nkernel:   ");
    write_str(machine.kernel_name);
    write_str(" ");
    write_str(machine.kernel_version);
    write_str("\narch:     ");
    write_str(machine.arch_name);
    write_str("\nbuild:    ");
    write_str(machine.build_date);
    write_str("\nconsole:  ");
    write_dec(machine.text_columns);
    write_str("x");
    write_dec(machine.text_rows);
    write_str("\nuptime:   ");
    write_dec(ticks());
    write_str(" ticks\n");

    write_str("cpu:      ");
    write_str(sysinfo_skip_spaces_local(machine.cpu_brand));
    write_str("\nvendor:   ");
    write_str(machine.cpu_vendor);
    write_str("\n");

    if (pmm_query(&pmm) > 0) {
        write_str("memory:   ");
        write_human_size((uint64_t)pmm.free_pages * NOS_PAGE_SIZE);
        write_str(" free / ");
        write_human_size((uint64_t)pmm.total_pages * NOS_PAGE_SIZE);
        write_str(" total");
        write_str(" (");
        write_dec(pmm.used_pages);
        write_str(" pages used)\n");
    } else {
        write_str("memory:   unavailable\n");
    }

    if (rtc_query(&rtc) > 0 && rtc.present && rtc.valid) {
        write_str("rtc:      ");
        sysinfo_write_date_time_local(&rtc);
        write_str("\n");
    }

    if (boot_info_query(&boot) > 0) {
        write_str("boot:     drive=0x");
        write_hex_u32(boot.boot_drive);
        write_str(" lba=");
        write_dec(boot.partition_lba);
        write_str(" sectors=");
        write_dec(boot.partition_sectors);
        write_str(" modules=");
        write_dec(boot.module_count);
        write_str("\n");
    }

    if (verbose && sysinfo_memory_totals_local(&mem_total, &mem_usable, &mem_reserved, &mem_entries)) {
        write_str("memmap:   entries=");
        write_dec(mem_entries);
        write_str(" total=");
        write_human_size(mem_total);
        write_str(" usable=");
        write_human_size(mem_usable);
        write_str(" reserved=");
        write_human_size(mem_reserved);
        write_str("\ncpuid0:   eax=");
        write_hex_u32(machine.cpuid_leaf0_eax);
        write_str(" ebx=");
        write_hex_u32(machine.cpuid_leaf0_ebx);
        write_str(" ecx=");
        write_hex_u32(machine.cpuid_leaf0_ecx);
        write_str(" edx=");
        write_hex_u32(machine.cpuid_leaf0_edx);
        write_str("\ncpuid1:   eax=");
        write_hex_u32(machine.cpuid_leaf1_eax);
        write_str(" ebx=");
        write_hex_u32(machine.cpuid_leaf1_ebx);
        write_str(" ecx=");
        write_hex_u32(machine.cpuid_leaf1_ecx);
        write_str(" edx=");
        write_hex_u32(machine.cpuid_leaf1_edx);
        write_str("\n");
    }
    return 0;
}

static int fb_smoke_local(void) {
    struct syscall_gfx_info gfx;
    int32_t x;
    int32_t y;

    if (gfx_info(&gfx) != 0 || gfx.width < 32u || gfx.height < 32u) {
        write_str("fb: graphics unavailable\n");
        return 0;
    }
    x = (int32_t)(gfx.width > 96u ? gfx.width - 96u : 4u);
    y = 4;
    if (gfx_fill_rect(x, y, 72u, 40u, 0x00102030u) < 0 ||
        gfx_draw_rect(x, y, 72u, 40u, 0x00ffffffu) < 0 ||
        gfx_draw_line(x + 4, y + 4, x + 67, y + 35, 0x0000ff80u) < 0 ||
        gfx_draw_line(x + 67, y + 4, x + 4, y + 35, 0x00ff4080u) < 0 ||
        gfx_draw_pixel(x + 36, y + 20, 0x00ffff00u) < 0 ||
        gfx_draw_triangle(x + 8, y + 32, x + 24, y + 12, x + 40, y + 32, 0x0080c0ffu) < 0 ||
        gfx_fill_circle(x + 56, y + 20, 8u, 0x00ffb000u) < 0 ||
        gfx_present() < 0) {
        write_err_str("fb: graphics smoke failed\n");
        return 1;
    }
    write_str("fb: graphics smoke OK\n");
    return 0;
}

static int fb_blit_smoke_local(void) {
    struct syscall_gfx_info gfx;
    uint32_t pixels[32u * 24u];
    int32_t x;
    int32_t y;

    if (gfx_info(&gfx) != 0 || gfx.width < 40u || gfx.height < 32u) {
        write_str("fb: graphics unavailable\n");
        return 0;
    }
    for (uint32_t row = 0u; row < 24u; row++) {
        for (uint32_t col = 0u; col < 32u; col++) {
            uint32_t r = (col * 255u) / 31u;
            uint32_t g = (row * 255u) / 23u;
            uint32_t b = ((row + col) * 255u) / 55u;

            if (row == 0u || col == 0u || row == 23u || col == 31u) {
                pixels[row * 32u + col] = 0x00ffffffu;
            } else {
                pixels[row * 32u + col] = (r << 16) | (g << 8) | b;
            }
        }
    }
    x = (int32_t)(gfx.width > 140u ? gfx.width - 140u : 8u);
    y = 52;
    if (gfx_blit(pixels, 32u * sizeof(uint32_t), x, y, 32u, 24u) < 0 ||
        gfx_draw_rect(x - 1, y - 1, 34u, 26u, 0x0000ffffu) < 0 ||
        gfx_present() < 0) {
        write_err_str("fb: blit smoke failed\n");
        return 1;
    }
    write_str("fb: blit smoke OK\n");
    return 0;
}

static void fb_batch_entry_local(struct syscall_gfx_batch_entry *entry,
                                 uint32_t op,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t radius,
                                 uint32_t rgb) {
    for (uint32_t i = 0u; i < sizeof(*entry); i++) {
        ((uint8_t *)entry)[i] = 0u;
    }
    entry->op = op;
    entry->command.x0 = x0;
    entry->command.y0 = y0;
    entry->command.x1 = x1;
    entry->command.y1 = y1;
    entry->command.width = width;
    entry->command.height = height;
    entry->command.radius = radius;
    entry->command.rgb = rgb;
}

static int fb_batch_smoke_local(void) {
    struct syscall_gfx_info gfx;
    struct syscall_gfx_batch_entry entries[8];
    int32_t x;
    int32_t y;

    if (gfx_info(&gfx) != 0 || gfx.width < 64u || gfx.height < 64u) {
        write_str("fb: graphics unavailable\n");
        return 0;
    }
    x = (int32_t)(gfx.width > 180u ? gfx.width - 180u : 16u);
    y = 84;
    fb_batch_entry_local(&entries[0], SYS_GFX_FILL_RECT, x, y, 0, 0, 96u, 48u, 0u, 0x00081018u);
    fb_batch_entry_local(&entries[1], SYS_GFX_RECT, x, y, 0, 0, 96u, 48u, 0u, 0x00ffffffu);
    fb_batch_entry_local(&entries[2], SYS_GFX_LINE, x + 4, y + 4, x + 91, y + 43, 0u, 0u, 0u, 0x0000ff80u);
    fb_batch_entry_local(&entries[3], SYS_GFX_LINE, x + 91, y + 4, x + 4, y + 43, 0u, 0u, 0u, 0x00ff4080u);
    fb_batch_entry_local(&entries[4], SYS_GFX_FILL_TRIANGLE, x + 12, y + 38, x + 32, y + 12, 0u, 0u, 0u, 0x0080c0ffu);
    entries[4].command.x2 = x + 52;
    entries[4].command.y2 = y + 38;
    fb_batch_entry_local(&entries[5], SYS_GFX_CIRCLE, x + 72, y + 24, 0, 0, 0u, 0u, 12u, 0x00ffb000u);
    fb_batch_entry_local(&entries[6], SYS_GFX_FILL_CIRCLE, x + 72, y + 24, 0, 0, 0u, 0u, 7u, 0x00ffe080u);
    fb_batch_entry_local(&entries[7], SYS_GFX_PIXEL, x + 48, y + 24, 0, 0, 0u, 0u, 0u, 0x00ffffffu);

    if (gfx_batch_begin(entries, 8u) != 0 ||
        gfx_batch_submit(SYS_GFX_BATCH_PRESENT) != 0) {
        gfx_batch_cancel();
        write_err_str("fb: batch smoke failed\n");
        return 1;
    }
    write_str("fb: batch smoke OK\n");
    return 0;
}

int cmd_fb(int argc, char **argv) {
    struct syscall_gfx_info gfx;
#ifdef __i386__
    struct syscall_framebuffer_info fb;
#endif

    if (argc == 2 && streq_local(argv[1], "--smoke")) {
        return fb_smoke_local();
    }
    if (argc == 2 && streq_local(argv[1], "--blit-smoke")) {
        return fb_blit_smoke_local();
    }
    if (argc == 2 && streq_local(argv[1], "--batch-smoke")) {
        return fb_batch_smoke_local();
    }
    if (argc > 1) {
        write_err_usage("fb", " [--smoke|--blit-smoke|--batch-smoke]\n");
        return 1;
    }
    (void)argv;
    if (gfx_info(&gfx) != 0 || gfx.width == 0u || gfx.height == 0u) {
        write_str("fb: graphics unavailable\n");
        return 0;
    }
    write_str("framebuffer\n");
    write_str("  size ");
    write_dec(gfx.width);
    write_str("x");
    write_dec(gfx.height);
    write_str(" pitch=");
    write_dec(gfx.pitch);
    write_str(" bpp=");
    write_dec(gfx.bpp);
    write_str("\n");
    write_str("  text ");
    write_dec(gfx.text_columns);
    write_str("x");
    write_dec(gfx.text_rows);
    write_str("\n");
#ifdef __i386__
    if (framebuffer_query(&fb) > 0 && fb.present) {
        write_str("  boot addr=");
        write_hex_u64(fb.addr);
        write_str(" type=");
        write_dec(fb.type);
        write_str(" color=");
        write_hex_u32(fb.text_color);
        write_str("\n");
    }
#endif
    return 0;
}
