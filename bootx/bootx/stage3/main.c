#include "bootx.h"

#define MAX_MENU_ENTRIES 8
#define BOOTX_PATH_MAX 64

struct menu_entry {
    char label[32];
    char kernel[BOOTX_PATH_MAX];
    char cmdline[BOOTX_CMDLINE_MAX];
    char modules[BOOTX_MAX_MODULES][BOOTX_PATH_MAX];
    uint32_t module_count;
};

static struct menu_entry menu_entries[MAX_MENU_ENTRIES];
static uint32_t menu_count;
static struct fat16_context fat;

static struct bootx_boot_info *const proto = (struct bootx_boot_info *)BOOT_INFO_ADDR;
static struct bootx_memmap_entry *const proto_memmap =
    (struct bootx_memmap_entry *)(BOOT_INFO_ADDR + sizeof(struct bootx_boot_info));
static struct bootx_module *const proto_modules =
    (struct bootx_module *)(BOOT_INFO_ADDR + sizeof(struct bootx_boot_info) + sizeof(struct bootx_memmap_entry) * BOOTX_MAX_MEMMAP);
static char *const proto_cmdline =
    (char *)(BOOT_INFO_ADDR + sizeof(struct bootx_boot_info) + sizeof(struct bootx_memmap_entry) * BOOTX_MAX_MEMMAP
        + sizeof(struct bootx_module) * BOOTX_MAX_MODULES);
static uint32_t page_table_next_free;
static uint32_t g_boot_payload_end = BOOTX_HIGHER_HALF_LOAD_ADDR;

static void fail(const char *message) {
    debug_puts("stage3 fail: ");
    debug_puts(message);
    debug_puts("\n");
    console_set_color(0x0C);
    console_puts("stage3: ");
    console_puts(message);
    console_putc('\n');
    halt_forever();
}

static void copy_string(char *dest, const char *src, size_t limit) {
    size_t i = 0;
    for (; i + 1 < limit && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static void trim_right(char *line) {
    size_t len = strlen(line);
    while (len > 0) {
        char ch = line[len - 1];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            line[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }
}

static char *trim_left(char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return line;
}

static void init_entry(struct menu_entry *entry) {
    memset(entry, 0, sizeof(*entry));
}

static void write_line_at(uint16_t row, uint16_t col, uint8_t color, const char *text) {
    console_write_at(row, col, color, text);
}

static void write_line_clipped_at(uint16_t row, uint16_t col, uint8_t color, const char *text, uint16_t width) {
    char buffer[81];
    uint16_t i = 0;

    if (width >= sizeof(buffer)) {
        width = sizeof(buffer) - 1;
    }
    while (i < width && text[i] != '\0') {
        buffer[i] = text[i];
        i++;
    }
    if (text[i] != '\0' && width >= 3) {
        buffer[width - 3] = '.';
        buffer[width - 2] = '.';
        buffer[width - 1] = '.';
        i = width;
    }
    while (i < width) {
        buffer[i++] = ' ';
    }
    buffer[width] = '\0';
    write_line_at(row, col, color, buffer);
}

static void path_leaf_to_name_83(char out[12], const char *path) {
    const char *leaf = path;
    const char *cur = path;

    while (*cur != '\0') {
        if (*cur == '/' || *cur == '\\') {
            leaf = cur + 1;
        }
        cur++;
    }

    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    out[11] = '\0';

    int pos = 0;
    while (*leaf != '\0' && *leaf != '.' && pos < 8) {
        char ch = *leaf++;
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - ('a' - 'A'));
        }
        out[pos++] = ch;
    }
    if (*leaf == '.') {
        leaf++;
        pos = 8;
        while (*leaf != '\0' && pos < 11) {
            char ch = *leaf++;
            if (ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - ('a' - 'A'));
            }
            out[pos++] = ch;
        }
    }
}

static void draw_hline(uint16_t row, uint16_t col, uint16_t width, uint8_t color, char ch) {
    char text[81];

    if (width >= sizeof(text)) {
        width = sizeof(text) - 1;
    }
    for (uint16_t i = 0; i < width; i++) {
        text[i] = ch;
    }
    text[width] = '\0';
    write_line_at(row, col, color, text);
}

static void render_menu(uint32_t selected) {
    uint16_t row = 0;
    const uint16_t list_width = 52;
    const uint16_t side_col = 56;
    const uint16_t side_width = 22;
    const uint16_t side_value_col = side_col + 7;

    console_set_color(0x01);
    console_clear();

    draw_hline(row, 0, 80, 0x70, ' ');
    write_line_at(row, 2, 0x70, "boot/x loader");
    write_line_at(row, 62, 0x70, "BIOS x86");
    row += 2;

    write_line_at(row++, 2, 0x0F, "Select a kernel image");
    write_line_at(row++, 2, 0x07, "Arrow keys or W/S move, Enter boots.");
    row++;

    for (uint32_t i = 0; i < menu_count; i++) {
        uint8_t line_color = (i == selected) ? 0x70 : 0x0F;
        uint8_t meta_color = (i == selected) ? 0x07 : 0x08;

        draw_hline(row, 2, list_width, line_color, ' ');
        write_line_at(row, 4, line_color, i == selected ? ">" : " ");
        if (i < 9) {
            char index_text[3];
            index_text[0] = (char)('1' + i);
            index_text[1] = '.';
            index_text[2] = '\0';
            write_line_at(row, 6, line_color, index_text);
        }
        write_line_clipped_at(row, 9, line_color, menu_entries[i].label, list_width - 9);
        row++;

        draw_hline(row, 2, list_width, meta_color, ' ');
        write_line_at(row, 6, meta_color, "kernel:");
        write_line_clipped_at(row, 14, meta_color, menu_entries[i].kernel, 24);
        if (menu_entries[i].module_count != 0) {
            write_line_at(row, 28, meta_color, "modules:");
            if (menu_entries[i].module_count == 1) {
                write_line_at(row, 37, meta_color, "1");
            } else if (menu_entries[i].module_count == 2) {
                write_line_at(row, 37, meta_color, "2");
            } else {
                write_line_at(row, 37, meta_color, "3+");
            }
        }
        row += 2;
    }

    draw_hline(6, side_col, side_width, 0x70, ' ');
    write_line_at(6, side_col + 3, 0x70, "entry info");
    draw_hline(7, side_col, side_width, 0x08, ' ');
    draw_hline(8, side_col, side_width, 0x01, ' ');
    draw_hline(9, side_col, side_width, 0x01, ' ');
    draw_hline(10, side_col, side_width, 0x01, ' ');
    draw_hline(11, side_col, side_width, 0x01, ' ');
    draw_hline(12, side_col, side_width, 0x01, ' ');
    draw_hline(13, side_col, side_width, 0x01, ' ');
    draw_hline(14, side_col, side_width, 0x01, ' ');
    draw_hline(15, side_col, side_width, 0x01, ' ');
    draw_hline(16, side_col, side_width, 0x01, ' ');
    draw_hline(17, side_col, side_width, 0x01, ' ');
    draw_hline(18, side_col, side_width, 0x01, ' ');
    write_line_at(8, side_col + 1, 0x07, "label:");
    write_line_clipped_at(8, side_value_col, 0x0F, menu_entries[selected].label, side_width - 8);
    write_line_at(10, side_col + 1, 0x07, "kernel:");
    write_line_clipped_at(10, side_value_col + 1, 0x0F, menu_entries[selected].kernel, side_width - 9);
    write_line_at(12, side_col + 1, 0x07, "cmdline:");
    write_line_clipped_at(13, side_col + 1, 0x0F, menu_entries[selected].cmdline[0] ? menu_entries[selected].cmdline : "(empty)", side_width - 2);
    write_line_at(15, side_col + 1, 0x07, "modules:");
    if (menu_entries[selected].module_count != 0) {
        for (uint32_t i = 0; i < menu_entries[selected].module_count && i < 3; i++) {
            write_line_clipped_at((uint16_t)(16 + i), side_col + 3, 0x0F, menu_entries[selected].modules[i], side_width - 4);
        }
    } else {
        write_line_at(16, side_col + 3, 0x08, "none");
    }

    draw_hline(22, 0, 80, 0x70, ' ');
    write_line_at(22, 2, 0x70, "Enter boot   Up/Down or W/S move   1-9 quick select");
    write_line_at(24, 2, 0x08, "boot/x protocol v2  higher-half ELF64 / FAT16 / FAT32");
}

static void set_default_menu(void) {
    menu_count = 1;
    init_entry(&menu_entries[0]);
    copy_string(menu_entries[0].label, "Demo Kernel32", sizeof(menu_entries[0].label));
    copy_string(menu_entries[0].kernel, "KERNEL.ELF", sizeof(menu_entries[0].kernel));
    copy_string(menu_entries[0].cmdline, "console=text demo=1", sizeof(menu_entries[0].cmdline));
}

static void parse_config(char *text) {
    char *line = text;
    menu_count = 0;
    init_entry(&menu_entries[0]);

    while (*line != '\0' && menu_count < MAX_MENU_ENTRIES) {
        char *next = strchr(line, '\n');
        if (next != 0) {
            *next = '\0';
        }

        trim_right(line);
        char *cur = trim_left(line);

        if (*cur == '\0') {
            if (menu_entries[menu_count].kernel[0] != '\0') {
                menu_count++;
                if (menu_count < MAX_MENU_ENTRIES) {
                    init_entry(&menu_entries[menu_count]);
                }
            }
        } else if (strncmp(cur, "LABEL=", 6) == 0) {
            if (menu_entries[menu_count].kernel[0] != '\0') {
                menu_count++;
                if (menu_count >= MAX_MENU_ENTRIES) {
                    break;
                }
                init_entry(&menu_entries[menu_count]);
            }
            copy_string(menu_entries[menu_count].label, cur + 6, sizeof(menu_entries[menu_count].label));
        } else if (strncmp(cur, "KERNEL=", 7) == 0) {
            if (menu_entries[menu_count].label[0] == '\0') {
                copy_string(menu_entries[menu_count].label, "Unnamed", sizeof(menu_entries[menu_count].label));
            }
            copy_string(menu_entries[menu_count].kernel, cur + 7, sizeof(menu_entries[menu_count].kernel));
        } else if (strncmp(cur, "CMDLINE=", 8) == 0) {
            copy_string(menu_entries[menu_count].cmdline, cur + 8, sizeof(menu_entries[menu_count].cmdline));
        } else if (strncmp(cur, "MODULE=", 7) == 0) {
            if (menu_entries[menu_count].module_count < BOOTX_MAX_MODULES) {
                copy_string(menu_entries[menu_count].modules[menu_entries[menu_count].module_count],
                            cur + 7,
                            sizeof(menu_entries[menu_count].modules[menu_entries[menu_count].module_count]));
                menu_entries[menu_count].module_count++;
            }
        }

        if (next == 0) {
            break;
        }
        line = next + 1;
    }

    if (menu_count < MAX_MENU_ENTRIES && menu_entries[menu_count].kernel[0] != '\0') {
        menu_count++;
    }

    if (menu_count == 0) {
        set_default_menu();
    }
}

static void load_config_or_default(void) {
    struct fat16_file cfg;
    char *buffer = (char *)CONFIG_LOAD_ADDR;
    if (fat16_open_path(&fat, "BOOT/BOOTX.CFG", &cfg) != 0 &&
        fat16_open_path(&fat, "BOOTX.CFG", &cfg) != 0) {
        set_default_menu();
        return;
    }
    if (cfg.size >= 64 * 1024u) {
        fail("BOOTX.CFG too large");
    }
    if (fat16_read_file(&fat, &cfg, buffer, 64 * 1024u) != 0) {
        fail("config read failed");
    }
    buffer[cfg.size] = '\0';
    memset(menu_entries, 0, sizeof(menu_entries));
    parse_config(buffer);
}

static uint32_t choose_entry(void) {
    uint32_t selected = 0;

    if (menu_count == 1) {
        return 0;
    }

    render_menu(selected);

    for (;;) {
        uint16_t key = bios_read_key();
        uint8_t ascii = (uint8_t)(key & 0xFF);
        uint8_t scan = (uint8_t)(key >> 8);

        if (ascii >= '1' && ascii < '1' + menu_count) {
            selected = (uint32_t)(ascii - '1');
            render_menu(selected);
            continue;
        }
        if (ascii == '\r') {
            return selected;
        }
        if (ascii == 'w' || ascii == 'W' || ascii == 'k' || ascii == 'K') {
            if (selected > 0) {
                selected--;
                render_menu(selected);
            }
            continue;
        }
        if (ascii == 's' || ascii == 'S' || ascii == 'j' || ascii == 'J') {
            if (selected + 1 < menu_count) {
                selected++;
                render_menu(selected);
            }
            continue;
        }
        if (ascii == 0x1B) {
            selected = 0;
            render_menu(selected);
            continue;
        }
        if (scan == 0x48 && selected > 0) {
            selected--;
            render_menu(selected);
        } else if (scan == 0x50 && selected + 1 < menu_count) {
            selected++;
            render_menu(selected);
        }
    }
}

static void build_memory_map(const struct bootx_stage3_params *params) {
    proto->memmap = (uint32_t)(uintptr_t)proto_memmap;
    proto->memmap_count = 0;

    if (params->memmap_count != 0) {
        proto->memmap_count = params->memmap_count;
        memcpy(proto_memmap, params->memmap, sizeof(struct bootx_memmap_entry) * params->memmap_count);
        return;
    }

    proto->memmap_count = 5;
    proto_memmap[0].base = 0x00000000ull;
    proto_memmap[0].length = 0x0009FC00ull;
    proto_memmap[0].type = BOOTX_MEMMAP_USABLE;
    proto_memmap[1].base = 0x0009FC00ull;
    proto_memmap[1].length = 0x00060400ull;
    proto_memmap[1].type = BOOTX_MEMMAP_RESERVED;
    proto_memmap[2].base = 0x00100000ull;
    proto_memmap[2].length = 0x03F00000ull;
    proto_memmap[2].type = BOOTX_MEMMAP_USABLE;
    proto_memmap[3].base = 0x00010000ull;
    proto_memmap[3].length = 0x00090000ull;
    proto_memmap[3].type = BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE;
    proto_memmap[4].base = MODULE_LOAD_ADDR;
    proto_memmap[4].length = 0x00200000ull;
    proto_memmap[4].type = BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE;
}

static int string_contains(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);

    if (needle_len == 0) {
        return 1;
    }

    while (*haystack != '\0') {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return 1;
        }
        haystack++;
    }

    return 0;
}

struct video_request {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
};

static int parse_decimal_field(const char **cursor, uint32_t *value) {
    uint32_t parsed = 0;
    int saw_digit = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        saw_digit = 1;
        parsed = parsed * 10u + (uint32_t)(**cursor - '0');
        (*cursor)++;
    }

    if (!saw_digit) {
        return -1;
    }

    *value = parsed;
    return 0;
}

static int find_cmdline_token_value(const char *cmdline, const char *key, const char **value_start) {
    size_t key_len = strlen(key);
    const char *cur = cmdline;

    while (*cur != '\0') {
        while (*cur == ' ') {
            cur++;
        }
        if (*cur == '\0') {
            break;
        }
        if (strncmp(cur, key, key_len) == 0) {
            *value_start = cur + key_len;
            return 0;
        }
        while (*cur != '\0' && *cur != ' ') {
            cur++;
        }
    }

    return -1;
}

static int parse_video_request(const struct menu_entry *entry, struct video_request *request) {
    const char *value;
    const char *cursor;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bpp = 0;

    memset(request, 0, sizeof(*request));

    if (find_cmdline_token_value(entry->cmdline, "video=", &value) != 0) {
        return -1;
    }

    cursor = value;
    if (strncmp(cursor, "vesa", 4) == 0) {
        cursor += 4;
        if (*cursor == '\0' || *cursor == ' ') {
            return 0;
        }
        if (*cursor != ':') {
            return -1;
        }
        cursor++;
    }

    if (*cursor == '\0' || *cursor == ' ') {
        return 0;
    }
    if (parse_decimal_field(&cursor, &width) != 0 || *cursor != 'x') {
        return -1;
    }
    cursor++;
    if (parse_decimal_field(&cursor, &height) != 0) {
        return -1;
    }
    if (*cursor == 'x') {
        cursor++;
        if (parse_decimal_field(&cursor, &bpp) != 0) {
            return -1;
        }
    }
    if (*cursor != '\0' && *cursor != ' ') {
        return -1;
    }
    if (width > 0xFFFFu || height > 0xFFFFu || bpp > 0xFFu) {
        return -1;
    }

    request->width = (uint16_t)width;
    request->height = (uint16_t)height;
    request->bpp = (uint8_t)bpp;
    return 0;
}

static int entry_wants_framebuffer(const struct menu_entry *entry) {
    return string_contains(entry->cmdline, "console=framebuffer")
        || string_contains(entry->cmdline, "console=fb")
        || string_contains(entry->cmdline, "video=vesa")
        || string_contains(entry->cmdline, "video=");
}

static void build_console_info(const struct bootx_console_info *selected_console) {
    memset(&proto->console, 0, sizeof(proto->console));
    if (selected_console != 0 && selected_console->type != BOOTX_CONSOLE_NONE) {
        memcpy(&proto->console, selected_console, sizeof(proto->console));
        return;
    }

    proto->console.type = BOOTX_CONSOLE_TEXT;
    proto->console.text_columns = 80;
    proto->console.text_rows = 25;
    proto->console.text_color = 0x07;
}

static void build_identity_page_tables_4g(void) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)BOOTX_PML4_ADDR;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)BOOTX_PDPT_ADDR;
    uint64_t *pd = (uint64_t *)(uintptr_t)BOOTX_PD_ADDR;

    memset(pml4, 0, 0x1000);
    memset(pdpt, 0, 0x1000);
    memset(pd, 0, 0x4000);
    memset((void *)(uintptr_t)BOOTX_PT_POOL_ADDR, 0, 0x9000);
    page_table_next_free = BOOTX_PT_POOL_ADDR;

    pml4[0] = (uint64_t)BOOTX_PDPT_ADDR | 0x03;

    for (uint32_t table = 0; table < 4; table++) {
        uint64_t *cur_pd = pd + table * 512;
        pdpt[table] = (uint64_t)(BOOTX_PD_ADDR + table * 0x1000) | 0x03;

        for (uint32_t i = 0; i < 512; i++) {
            uint64_t base = ((uint64_t)table * 512ull + (uint64_t)i) * 0x200000ull;
            cur_pd[i] = base | 0x83;
        }
    }
}

static uint64_t *alloc_page_table(void) {
    uint64_t *table = (uint64_t *)(uintptr_t)page_table_next_free;
    if (page_table_next_free >= 0x00030000u) {
        fail("page table pool exhausted");
    }
    memset(table, 0, 0x1000);
    page_table_next_free += 0x1000;
    return table;
}

static uint64_t *ensure_table(uint64_t *parent, uint32_t index) {
    if ((parent[index] & 1u) == 0) {
        uint64_t *table = alloc_page_table();
        parent[index] = (uint64_t)(uintptr_t)table | 0x03;
    }
    return (uint64_t *)(uintptr_t)((uint32_t)(parent[index] & 0xFFFFF000u));
}

static void map_page_4k(uint64_t virt, uint32_t phys, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)BOOTX_PML4_ADDR;
    uint32_t pml4_index = (uint32_t)((virt >> 39) & 0x1FFu);
    uint32_t pdpt_index = (uint32_t)((virt >> 30) & 0x1FFu);
    uint32_t pd_index = (uint32_t)((virt >> 21) & 0x1FFu);
    uint32_t pt_index = (uint32_t)((virt >> 12) & 0x1FFu);
    uint64_t *pdpt = ensure_table(pml4, pml4_index);
    uint64_t *pd = ensure_table(pdpt, pdpt_index);
    uint64_t *pt;

    if (pd[pd_index] & 0x80u) {
        fail("cannot remap huge identity page");
    }
    if ((pd[pd_index] & 1u) == 0) {
        pt = alloc_page_table();
        pd[pd_index] = (uint64_t)(uintptr_t)pt | 0x03;
    } else {
        pt = (uint64_t *)(uintptr_t)((uint32_t)(pd[pd_index] & 0xFFFFF000u));
    }

    pt[pt_index] = ((uint64_t)phys & ~0xFFFu) | flags | 0x03;
}

static void map_region_4k(uint64_t virt, uint32_t phys, uint64_t size, uint64_t flags) {
    uint64_t page_offset = virt & 0xFFFu;
    uint64_t cur_virt = virt & ~0xFFFull;
    uint32_t cur_phys = phys & ~0xFFFu;
    uint64_t total = (size + page_offset + 0xFFFu) & ~0xFFFull;

    for (uint64_t offset = 0; offset < total; offset += 0x1000u) {
        map_page_4k(cur_virt + offset, cur_phys + (uint32_t)offset, flags);
    }
}

static uint32_t load_modules(const struct menu_entry *entry) {
    uint32_t next_addr = MODULE_LOAD_ADDR;
    proto->modules = (uint32_t)(uintptr_t)proto_modules;
    proto->module_count = 0;

    for (uint32_t i = 0; i < entry->module_count; i++) {
        struct fat16_file file;
        char module_name[12];
        if (fat16_open_path(&fat, entry->modules[i], &file) != 0) {
            fail("module not found");
        }
        if (fat16_read_file(&fat, &file, (void *)next_addr, file.size) != 0) {
            fail("module read failed");
        }

        path_leaf_to_name_83(module_name, entry->modules[i]);
        memcpy(proto_modules[proto->module_count].name, module_name, sizeof(proto_modules[proto->module_count].name));
        proto_modules[proto->module_count].address = next_addr;
        proto_modules[proto->module_count].size = file.size;
        proto->module_count++;

        next_addr += (file.size + 0xFFFu) & ~0xFFFu;
    }

    return next_addr;
}

static void prepare_proto(const struct bootx_stage3_params *params, const struct menu_entry *entry,
                         const struct bootx_console_info *selected_console) {
    const struct bootx_boot_info *base_info = &params->boot_info;
    memset(proto, 0, sizeof(*proto));
    memset(proto_memmap, 0, sizeof(struct bootx_memmap_entry) * BOOTX_MAX_MEMMAP);
    memset(proto_modules, 0, sizeof(struct bootx_module) * BOOTX_MAX_MODULES);
    memset(proto_cmdline, 0, BOOTX_CMDLINE_MAX);

    proto->hdr.magic = BOOTX_MAGIC;
    proto->hdr.version = BOOTX_PROTOCOL_VERSION;
    proto->hdr.size = sizeof(*proto);
    proto->boot_drive = base_info->boot_drive;
    proto->partition_lba = base_info->partition_lba;
    proto->partition_sectors = base_info->partition_sectors;
    proto->cmdline = (uint32_t)(uintptr_t)proto_cmdline;
    copy_string(proto_cmdline, entry->cmdline, BOOTX_CMDLINE_MAX);

    build_memory_map(params);
    build_console_info(selected_console);
    g_boot_payload_end = load_modules(entry);
}

static void load_elf32_and_jump(const struct bootx_stage3_params *params, const struct menu_entry *entry, uint8_t *kernel) {
    uint32_t lowest_addr = 0xFFFFFFFFu;
    uint32_t highest_addr = 0;
    struct bootx_console_info selected_console;
    struct video_request video;
    int using_framebuffer = 0;

    struct elf32_ehdr *ehdr = (struct elf32_ehdr *)kernel;
    if (memcmp(ehdr->e_ident, "\x7F""ELF", 4) != 0 || ehdr->e_ident[4] != ELF_CLASS_32 || ehdr->e_machine != ELF_MACHINE_386) {
        fail("invalid ELF32 kernel");
    }

    memset(&selected_console, 0, sizeof(selected_console));
    memset(&video, 0, sizeof(video));
    int wants_fb = entry_wants_framebuffer(entry);

    if (wants_fb) {
        if (parse_video_request(entry, &video) != 0) {
            fail("bad video= option");
        }

        if (bios_init_framebuffer(&selected_console,
                                video.width,
                                video.height,
                                video.bpp) == 0) {
            using_framebuffer = 1;
            debug_puts("stage3: framebuffer enabled\n");
        } else {
            debug_puts("stage3: framebuffer request failed\n");
        }
    }

    prepare_proto(params, entry, &selected_console);

    struct elf32_phdr *phdr = (struct elf32_phdr *)(kernel + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        uint32_t load_addr;
        if (phdr[i].p_type != ELF_PT_LOAD) {
            continue;
        }
        load_addr = phdr[i].p_paddr ? phdr[i].p_paddr : phdr[i].p_vaddr;
        memcpy((void *)load_addr, kernel + phdr[i].p_offset, phdr[i].p_filesz);
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            memset((void *)(load_addr + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
        }
        if (load_addr < lowest_addr) {
            lowest_addr = load_addr;
        }
        if (load_addr + phdr[i].p_memsz > highest_addr) {
            highest_addr = load_addr + phdr[i].p_memsz;
        }
    }

    proto->kernel_phys_addr = lowest_addr;
    proto->kernel_phys_size = highest_addr - lowest_addr;
    proto->kernel_entry = ehdr->e_entry;

    if (!using_framebuffer) {
        console_clear();
        console_set_color(0x07);
        console_puts("booting kernel...\n");
    }

    ((void (*)(const struct bootx_boot_info *))ehdr->e_entry)(proto);
    fail("kernel returned");
}

static void load_elf64_and_jump(const struct bootx_stage3_params *params, const struct menu_entry *entry, uint8_t *kernel) {
    struct elf64_ehdr *ehdr = (struct elf64_ehdr *)kernel;
    uint32_t lowest_addr = 0xFFFFFFFFu;
    uint32_t highest_addr = 0;
    uint32_t next_phys;
    struct bootx_console_info selected_console;
    struct video_request video;
    int using_framebuffer = 0;

    if (memcmp(ehdr->e_ident, "\x7F""ELF", 4) != 0 || ehdr->e_ident[4] != ELF_CLASS_64 || ehdr->e_machine != ELF_MACHINE_X86_64) {
        fail("invalid ELF64 kernel");
    }

    memset(&selected_console, 0, sizeof(selected_console));
    memset(&video, 0, sizeof(video));
    int wants_fb = entry_wants_framebuffer(entry);

    if (wants_fb) {
        if (parse_video_request(entry, &video) != 0) {
            fail("bad video= option");
        }

        if (bios_init_framebuffer(&selected_console,
                                video.width,
                                video.height,
                                video.bpp) == 0) {
            using_framebuffer = 1;
            debug_puts("stage3: framebuffer enabled\n");
        } else {
            debug_puts("stage3: framebuffer request failed\n");
        }
    }

    prepare_proto(params, entry, &selected_console);
    next_phys = g_boot_payload_end > BOOTX_HIGHER_HALF_LOAD_ADDR
                    ? g_boot_payload_end
                    : BOOTX_HIGHER_HALF_LOAD_ADDR;

    struct elf64_phdr *phdr = (struct elf64_phdr *)(kernel + (uint32_t)ehdr->e_phoff);
    build_identity_page_tables_4g();

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        uint64_t load_addr64;
        uint32_t load_addr;
        uint32_t physical_load;
        uint64_t virt_load;
        uint64_t flags = 0;

        if (phdr[i].p_type != ELF_PT_LOAD) {
            continue;
        }
        if (phdr[i].p_memsz == 0) {
            debug_puts("stage3: skip empty load seg ");
            debug_put_hex(i);
            debug_puts("\n");
            continue;
        }

        load_addr64 = phdr[i].p_paddr ? phdr[i].p_paddr : phdr[i].p_vaddr;
        if (phdr[i].p_offset > 0xFFFFFFFFull || phdr[i].p_filesz > 0xFFFFFFFFull || phdr[i].p_memsz > 0xFFFFFFFFull) {
            fail("large ELF64 segment not supported yet");
        }

        virt_load = phdr[i].p_vaddr;
        if (phdr[i].p_flags & 0x2u) {
            flags |= 0x2u;
        }

        if (virt_load > 0xFFFFFFFFull || load_addr64 > 0xFFFFFFFFull) {
            physical_load = (next_phys + 0xFFFu) & ~0xFFFu;
            next_phys = physical_load + ((uint32_t)phdr[i].p_memsz + 0xFFFu);
            memcpy((void *)(uintptr_t)physical_load, kernel + (uint32_t)phdr[i].p_offset, (size_t)phdr[i].p_filesz);
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((void *)(uintptr_t)(physical_load + (uint32_t)phdr[i].p_filesz), 0, (size_t)(phdr[i].p_memsz - phdr[i].p_filesz));
            }
            map_region_4k(virt_load, physical_load, phdr[i].p_memsz, flags);
            load_addr = physical_load;
        } else {
            load_addr = (uint32_t)load_addr64;
            memcpy((void *)(uintptr_t)load_addr, kernel + (uint32_t)phdr[i].p_offset, (size_t)phdr[i].p_filesz);
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((void *)(uintptr_t)(load_addr + (uint32_t)phdr[i].p_filesz), 0, (size_t)(phdr[i].p_memsz - phdr[i].p_filesz));
            }
        }

        if (load_addr < lowest_addr) {
            lowest_addr = load_addr;
        }
        if (load_addr + (uint32_t)phdr[i].p_memsz > highest_addr) {
            highest_addr = load_addr + (uint32_t)phdr[i].p_memsz;
        }
    }

    if (ehdr->e_entry > 0xFFFFFFFFull) {
        debug_puts("stage3: higher-half entry ");
        debug_put_hex((uint32_t)(ehdr->e_entry >> 32));
        debug_put_hex((uint32_t)ehdr->e_entry);
        debug_puts("\n");
    }

    proto->kernel_phys_addr = lowest_addr;
    proto->kernel_phys_size = highest_addr - lowest_addr;
    proto->kernel_entry = ehdr->e_entry;
    debug_puts("stage3: kernel phys range low=");
    debug_put_hex(lowest_addr);
    debug_puts(" high=");
    debug_put_hex(highest_addr);
    debug_puts(" size=");
    debug_put_hex(highest_addr - lowest_addr);
    debug_puts("\n");

    if (!using_framebuffer) {
        console_clear();
        console_set_color(0x07);
        console_puts("entering long mode...\n");
    }
    debug_puts("stage3: enter long mode\n");

    bootx_enter_long_mode(BOOTX_PML4_ADDR, (uint32_t)ehdr->e_entry, (uint32_t)(ehdr->e_entry >> 32), BOOT_INFO_ADDR);
    fail("long mode returned");
}

static void load_kernel_and_jump(const struct bootx_stage3_params *params, const struct menu_entry *entry) {
    struct fat16_file kernel_file;
    uint8_t *kernel = (uint8_t *)KERNEL_LOAD_ADDR;

    if (fat16_open_path(&fat, entry->kernel, &kernel_file) != 0) {
        fail("kernel not found");
    }
    if (kernel_file.size > KERNEL_SCRATCH_MAX) {
        fail("kernel too large");
    }
    if (fat16_read_file(&fat, &kernel_file, kernel, KERNEL_SCRATCH_MAX) != 0) {
        fail("kernel read failed");
    }

    if (memcmp(kernel, "\x7F""ELF", 4) != 0) {
        fail("kernel is not ELF");
    }

    if (kernel[4] == ELF_CLASS_32) {
        load_elf32_and_jump(params, entry, kernel);
        return;
    }
    if (kernel[4] == ELF_CLASS_64) {
        load_elf64_and_jump(params, entry, kernel);
        return;
    }

    fail("unsupported ELF class");
}

void stage3_main(const struct bootx_boot_info *raw_params) {
    const struct bootx_stage3_params *params = (const struct bootx_stage3_params *)raw_params;
    const struct bootx_boot_info *boot_info = &params->boot_info;

    if (params == 0 || params->services == 0) {
        fail("missing services");
    }
    stage3_set_services(params->services);

    if (boot_info->hdr.magic != BOOTX_MAGIC) {
        fail("bad boot info");
    }

    console_set_color(0x07);
    console_clear();
    console_puts("boot/x stage3\n");
    debug_puts("stage3: entered\n");

    if (fat16_mount(&fat, boot_info->boot_drive, boot_info->partition_lba) != 0) {
        fail("mount failed");
    }

    load_config_or_default();
    uint32_t selected = choose_entry();
    debug_puts("stage3: boot kernel\n");
    load_kernel_and_jump(params, &menu_entries[selected]);
}
