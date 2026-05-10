#include "bootx.h"

typedef uint16_t CHAR16;
typedef uint64_t EFI_STATUS;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef uint64_t UINTN;
typedef void *EFI_HANDLE;

#define EFI_SUCCESS 0ull
#define EFI_ERROR_BIT 0x8000000000000000ull
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5ull)
#define EFI_NOT_FOUND (EFI_ERROR_BIT | 14ull)
#define EFI_FILE_MODE_READ 0x0000000000000001ull
#define EFI_PAGE_SIZE 4096ull
#define EFI_SIZE_TO_PAGES(size) (((size) + EFI_PAGE_SIZE - 1ull) / EFI_PAGE_SIZE)

#define BOOTX_UEFI_DEFAULT_BOOT_LBA 2048u
#define BOOTX_UEFI_DEFAULT_BOOT_SECTORS 98304u
#define BOOTX_UEFI_IDENTITY_LIMIT (512ull * 1024ull * 1024ull * 1024ull)
#define BOOTX_UEFI_MAX_PATH 160u

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

typedef struct efi_guid EFI_GUID;

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

struct efi_simple_text_output_protocol {
    EFI_STATUS (*Reset)(struct efi_simple_text_output_protocol *self, uint8_t extended_verification);
    EFI_STATUS (*OutputString)(struct efi_simple_text_output_protocol *self, const CHAR16 *string);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    void *ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
};

struct efi_memory_descriptor {
    uint32_t Type;
    uint32_t Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

struct efi_boot_services {
    struct efi_table_header Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_STATUS (*AllocatePages)(uint32_t type,
                                uint32_t memory_type,
                                UINTN pages,
                                EFI_PHYSICAL_ADDRESS *memory);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS memory, UINTN pages);
    EFI_STATUS (*GetMemoryMap)(UINTN *memory_map_size,
                               struct efi_memory_descriptor *memory_map,
                               UINTN *map_key,
                               UINTN *descriptor_size,
                               uint32_t *descriptor_version);
    EFI_STATUS (*AllocatePool)(uint32_t pool_type, UINTN size, void **buffer);
    EFI_STATUS (*FreePool)(void *buffer);
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE handle, EFI_GUID *protocol, void **interface);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE image_handle, UINTN map_key);
    void *GetNextMonotonicCount;
    EFI_STATUS (*Stall)(UINTN microseconds);
    EFI_STATUS (*SetWatchdogTimer)(UINTN timeout, uint64_t watchdog_code, UINTN data_size, CHAR16 *watchdog_data);
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(EFI_GUID *protocol, void *registration, void **interface);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
};

struct efi_system_table {
    struct efi_table_header Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    struct efi_simple_text_output_protocol *ConOut;
    EFI_HANDLE StandardErrorHandle;
    struct efi_simple_text_output_protocol *StdErr;
    void *RuntimeServices;
    struct efi_boot_services *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
};

struct efi_loaded_image_protocol {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    struct efi_system_table *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    EFI_STATUS (*Unload)(EFI_HANDLE image_handle);
};

struct efi_file_protocol;

struct efi_file_protocol {
    uint64_t Revision;
    EFI_STATUS (*Open)(struct efi_file_protocol *self,
                       struct efi_file_protocol **new_handle,
                       const CHAR16 *file_name,
                       uint64_t open_mode,
                       uint64_t attributes);
    EFI_STATUS (*Close)(struct efi_file_protocol *self);
    EFI_STATUS (*Delete)(struct efi_file_protocol *self);
    EFI_STATUS (*Read)(struct efi_file_protocol *self, UINTN *buffer_size, void *buffer);
    EFI_STATUS (*Write)(struct efi_file_protocol *self, UINTN *buffer_size, void *buffer);
    EFI_STATUS (*GetPosition)(struct efi_file_protocol *self, uint64_t *position);
    EFI_STATUS (*SetPosition)(struct efi_file_protocol *self, uint64_t position);
    EFI_STATUS (*GetInfo)(struct efi_file_protocol *self,
                          EFI_GUID *information_type,
                          UINTN *buffer_size,
                          void *buffer);
    EFI_STATUS (*SetInfo)(struct efi_file_protocol *self,
                          EFI_GUID *information_type,
                          UINTN buffer_size,
                          void *buffer);
    EFI_STATUS (*Flush)(struct efi_file_protocol *self);
};

struct efi_simple_file_system_protocol {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(struct efi_simple_file_system_protocol *self,
                             struct efi_file_protocol **root);
};

struct efi_time {
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Pad1;
    uint32_t Nanosecond;
    int16_t TimeZone;
    uint8_t Daylight;
    uint8_t Pad2;
};

struct efi_file_info {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    struct efi_time CreateTime;
    struct efi_time LastAccessTime;
    struct efi_time ModificationTime;
    uint64_t Attribute;
    CHAR16 FileName[1];
};

struct efi_pixel_bitmask {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
};

struct efi_graphics_output_mode_information {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelFormat;
    struct efi_pixel_bitmask PixelInformation;
    uint32_t PixelsPerScanLine;
};

struct efi_graphics_output_protocol_mode {
    uint32_t MaxMode;
    uint32_t Mode;
    struct efi_graphics_output_mode_information *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
};

struct efi_graphics_output_protocol {
    EFI_STATUS (*QueryMode)(struct efi_graphics_output_protocol *self,
                            uint32_t mode_number,
                            UINTN *size_of_info,
                            struct efi_graphics_output_mode_information **info);
    EFI_STATUS (*SetMode)(struct efi_graphics_output_protocol *self, uint32_t mode_number);
    void *Blt;
    struct efi_graphics_output_protocol_mode *Mode;
};

enum {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2
};

enum {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14
};

enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3
};

enum {
    PAGE_PRESENT = 1u << 0,
    PAGE_RW = 1u << 1,
    PAGE_PS = 1u << 7,
    PAGE_ADDR_MASK = 0x000ffffffffff000ull
};

struct boot_config {
    char kernel[BOOTX_UEFI_MAX_PATH];
    char cmdline[BOOTX_CMDLINE_MAX];
    char modules[BOOTX_MAX_MODULES][BOOTX_UEFI_MAX_PATH];
    uint32_t module_count;
};

struct uefi_boot_block {
    struct bootx_boot_info info;
    char cmdline[BOOTX_CMDLINE_MAX];
    struct bootx_memmap_entry memmap[BOOTX_MAX_MEMMAP];
    struct bootx_module modules[BOOTX_MAX_MODULES];
};

static struct efi_system_table *g_st;
static struct efi_boot_services *g_bs;
static struct efi_file_protocol *g_root;
static uint64_t *g_pml4;
static struct uefi_boot_block *g_boot_block;

static EFI_GUID g_loaded_image_guid = {
    0x5b1b31a1u, 0x9562u, 0x11d2u, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static EFI_GUID g_simple_file_system_guid = {
    0x964e5b22u, 0x6459u, 0x11d2u, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static EFI_GUID g_file_info_guid = {
    0x09576e92u, 0x6d3fu, 0x11d2u, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static EFI_GUID g_gop_guid = {
    0x9042a9deu, 0x23dcu, 0x4a38u, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

void *memcpy(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int value, size_t len) {
    uint8_t *d = (uint8_t *)dest;

    for (size_t i = 0; i < len; i++) {
        d[i] = (uint8_t)value;
    }
    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t len) {
    const uint8_t *l = (const uint8_t *)lhs;
    const uint8_t *r = (const uint8_t *)rhs;

    for (size_t i = 0; i < len; i++) {
        if (l[i] != r[i]) {
            return (int)l[i] - (int)r[i];
        }
    }
    return 0;
}

size_t strlen(const char *str) {
    size_t len = 0;

    while (str != 0 && str[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }
    return (int)(uint8_t)*lhs - (int)(uint8_t)*rhs;
}

int strncmp(const char *lhs, const char *rhs, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (lhs[i] != rhs[i] || lhs[i] == '\0' || rhs[i] == '\0') {
            return (int)(uint8_t)lhs[i] - (int)(uint8_t)rhs[i];
        }
    }
    return 0;
}

char *strchr(const char *str, int ch) {
    while (*str != '\0') {
        if (*str == (char)ch) {
            return (char *)str;
        }
        str++;
    }
    return ch == 0 ? (char *)str : 0;
}

void strtoupper(char *str) {
    while (str != 0 && *str != '\0') {
        if (*str >= 'a' && *str <= 'z') {
            *str = (char)(*str - ('a' - 'A'));
        }
        str++;
    }
}

static int efi_is_error(EFI_STATUS status) {
    return (status & EFI_ERROR_BIT) != 0;
}

static void efi_puts(const char *str) {
    CHAR16 chunk[96];
    UINTN out = 0;

    if (g_st == 0 || g_st->ConOut == 0 || str == 0) {
        return;
    }
    while (*str != '\0') {
        out = 0;
        while (*str != '\0' && out < (sizeof(chunk) / sizeof(chunk[0])) - 2u) {
            if (*str == '\n') {
                chunk[out++] = '\r';
            }
            chunk[out++] = (CHAR16)(uint8_t)*str++;
        }
        chunk[out] = 0;
        g_st->ConOut->OutputString(g_st->ConOut, chunk);
    }
}

static void efi_put_hex64(uint64_t value) {
    char out[17];

    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (uint8_t)(value & 0xfu);
        out[i] = (char)(nibble < 10u ? '0' + nibble : 'A' + (nibble - 10u));
        value >>= 4;
    }
    out[16] = '\0';
    efi_puts(out);
}

static __attribute__((noreturn)) void efi_halt(void) {
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static __attribute__((noreturn)) void efi_fail(const char *message) {
    efi_puts("\nbootx64: ");
    efi_puts(message);
    efi_puts("\n");
    efi_halt();
}

static uint64_t align_down_u64(uint64_t value, uint64_t align) {
    return value & ~(align - 1ull);
}

static uint64_t align_up_u64(uint64_t value, uint64_t align) {
    return (value + align - 1ull) & ~(align - 1ull);
}

static uint32_t ptr_to_u32(const void *ptr) {
    uint64_t value = (uint64_t)(uintptr_t)ptr;

    if (value > 0xffffffffull) {
        efi_fail("low allocation above 4G");
    }
    return (uint32_t)value;
}

static void *uefi_alloc_low_pages(uint64_t bytes) {
    EFI_PHYSICAL_ADDRESS addr = 0xffffffffull;
    UINTN pages = EFI_SIZE_TO_PAGES(bytes == 0 ? 1 : bytes);
    EFI_STATUS status = g_bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &addr);

    if (efi_is_error(status) || addr == 0 || addr > 0xffffffffull) {
        return 0;
    }
    memset((void *)(uintptr_t)addr, 0, (size_t)(pages * EFI_PAGE_SIZE));
    return (void *)(uintptr_t)addr;
}

static void str_copy_limit(char *dest, size_t cap, const char *src) {
    size_t i = 0;

    if (dest == 0 || cap == 0) {
        return;
    }
    if (src != 0) {
        while (src[i] != '\0' && i + 1u < cap) {
            dest[i] = src[i];
            i++;
        }
    }
    dest[i] = '\0';
}

static void config_init_default(struct boot_config *config) {
    memset(config, 0, sizeof(*config));
    str_copy_limit(config->kernel, sizeof(config->kernel), "BOOT/NEX.ELF");
    str_copy_limit(config->cmdline,
                   sizeof(config->cmdline),
                   "console=framebuffer video=800x600x32 root=NexOS arch=x86_64 init=/INIT.SH");
    str_copy_limit(config->modules[config->module_count++],
                   sizeof(config->modules[0]),
                   "BOOT/RAMDISK.IMG");
}

static int key_equals(const char *key, uint32_t key_len, const char *target) {
    uint32_t i = 0;

    while (target[i] != '\0') {
        if (i >= key_len || key[i] != target[i]) {
            return 0;
        }
        i++;
    }
    return i == key_len;
}

static void trim_span(const char **start, const char **end) {
    while (*start < *end && (**start == ' ' || **start == '\t')) {
        (*start)++;
    }
    while (*end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t' ||
                             (*end)[-1] == '\r' || (*end)[-1] == '\n')) {
        (*end)--;
    }
}

static void copy_span(char *dest, size_t cap, const char *start, const char *end) {
    size_t len;

    if (cap == 0) {
        return;
    }
    trim_span(&start, &end);
    len = (size_t)(end - start);
    if (len >= cap) {
        len = cap - 1u;
    }
    memcpy(dest, start, len);
    dest[len] = '\0';
}

static void parse_config(struct boot_config *config, const char *text, uint32_t size) {
    const char *cur = text;
    const char *end = text + size;

    while (cur < end) {
        const char *line = cur;
        const char *line_end;
        const char *eq;
        const char *key_start;
        const char *key_end;
        const char *value_start;
        const char *value_end;

        while (cur < end && *cur != '\n') {
            cur++;
        }
        line_end = cur;
        if (cur < end && *cur == '\n') {
            cur++;
        }
        eq = line;
        while (eq < line_end && *eq != '=') {
            eq++;
        }
        if (eq >= line_end || line == line_end || *line == '#') {
            continue;
        }
        key_start = line;
        key_end = eq;
        value_start = eq + 1;
        value_end = line_end;
        trim_span(&key_start, &key_end);
        trim_span(&value_start, &value_end);
        if (key_equals(key_start, (uint32_t)(key_end - key_start), "KERNEL")) {
            copy_span(config->kernel, sizeof(config->kernel), value_start, value_end);
        } else if (key_equals(key_start, (uint32_t)(key_end - key_start), "CMDLINE")) {
            copy_span(config->cmdline, sizeof(config->cmdline), value_start, value_end);
        } else if (key_equals(key_start, (uint32_t)(key_end - key_start), "MODULE") &&
                   config->module_count < BOOTX_MAX_MODULES) {
            copy_span(config->modules[config->module_count],
                      sizeof(config->modules[config->module_count]),
                      value_start,
                      value_end);
            config->module_count++;
        }
    }
}

static void ascii_to_efi_path(const char *path, CHAR16 *out, UINTN cap) {
    UINTN pos = 0;

    while (*path == '/' || *path == '\\') {
        path++;
    }
    while (*path != '\0' && pos + 1u < cap) {
        char ch = *path++;
        out[pos++] = (CHAR16)(ch == '/' ? '\\' : ch);
    }
    out[pos] = 0;
}

static struct efi_file_protocol *open_file(const char *path) {
    CHAR16 path16[BOOTX_UEFI_MAX_PATH];
    struct efi_file_protocol *file = 0;
    EFI_STATUS status;

    ascii_to_efi_path(path, path16, sizeof(path16) / sizeof(path16[0]));
    status = g_root->Open(g_root, &file, path16, EFI_FILE_MODE_READ, 0);
    if (efi_is_error(status)) {
        return 0;
    }
    return file;
}

static int file_size(struct efi_file_protocol *file, uint64_t *out_size) {
    uint8_t info_buffer[512];
    UINTN info_size = sizeof(info_buffer);
    EFI_STATUS status = file->GetInfo(file, &g_file_info_guid, &info_size, info_buffer);
    struct efi_file_info *info = (struct efi_file_info *)info_buffer;

    if (efi_is_error(status)) {
        return -1;
    }
    *out_size = info->FileSize;
    return 0;
}

static void *read_file_alloc(const char *path, uint32_t *out_size) {
    struct efi_file_protocol *file = open_file(path);
    uint64_t size = 0;
    UINTN read_size;
    void *buffer;
    EFI_STATUS status;

    if (file == 0) {
        return 0;
    }
    if (file_size(file, &size) != 0 || size > 0xffffffffull) {
        file->Close(file);
        return 0;
    }
    buffer = uefi_alloc_low_pages(size + 1u);
    if (buffer == 0) {
        file->Close(file);
        return 0;
    }
    read_size = (UINTN)size;
    status = file->Read(file, &read_size, buffer);
    file->Close(file);
    if (efi_is_error(status) || read_size != (UINTN)size) {
        return 0;
    }
    ((char *)buffer)[size] = '\0';
    if (out_size != 0) {
        *out_size = (uint32_t)size;
    }
    return buffer;
}

static void path_leaf_to_name_83(char out[12], const char *path) {
    const char *leaf = path;
    const char *cur = path;
    int pos = 0;

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

static uint64_t *alloc_page_table(void) {
    uint64_t *table = (uint64_t *)uefi_alloc_low_pages(EFI_PAGE_SIZE);

    if (table == 0) {
        efi_fail("page table allocation failed");
    }
    return table;
}

static uint64_t *page_table_child(uint64_t *table, uint64_t index) {
    if ((table[index] & PAGE_PRESENT) == 0) {
        uint64_t *child = alloc_page_table();
        table[index] = ((uint64_t)(uintptr_t)child & PAGE_ADDR_MASK) | PAGE_PRESENT | PAGE_RW;
    }
    return (uint64_t *)(uintptr_t)(table[index] & PAGE_ADDR_MASK);
}

static void map_page_4k(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_index = (virt >> 39) & 0x1ffu;
    uint64_t pdpt_index = (virt >> 30) & 0x1ffu;
    uint64_t pd_index = (virt >> 21) & 0x1ffu;
    uint64_t pt_index = (virt >> 12) & 0x1ffu;
    uint64_t *pdpt = page_table_child(g_pml4, pml4_index);
    uint64_t *pd = page_table_child(pdpt, pdpt_index);
    uint64_t *pt;

    if ((pd[pd_index] & PAGE_PS) != 0) {
        return;
    }
    pt = page_table_child(pd, pd_index);
    pt[pt_index] = (phys & PAGE_ADDR_MASK) | flags | PAGE_PRESENT;
}

static void map_identity_range_4k(uint64_t base, uint64_t size) {
    uint64_t start = align_down_u64(base, EFI_PAGE_SIZE);
    uint64_t end = align_up_u64(base + size, EFI_PAGE_SIZE);

    if (end <= BOOTX_UEFI_IDENTITY_LIMIT) {
        return;
    }
    if (start < BOOTX_UEFI_IDENTITY_LIMIT) {
        start = BOOTX_UEFI_IDENTITY_LIMIT;
    }
    for (uint64_t addr = start; addr < end; addr += EFI_PAGE_SIZE) {
        map_page_4k(addr, addr, PAGE_RW);
    }
}

static void build_identity_page_tables(void) {
    uint64_t addr = 0;
    uint64_t *pdpt;

    g_pml4 = alloc_page_table();
    pdpt = alloc_page_table();
    g_pml4[0] = ((uint64_t)(uintptr_t)pdpt & PAGE_ADDR_MASK) | PAGE_PRESENT | PAGE_RW;
    for (uint32_t pdpt_index = 0; pdpt_index < 512u; pdpt_index++) {
        uint64_t *pd = alloc_page_table();
        pdpt[pdpt_index] = ((uint64_t)(uintptr_t)pd & PAGE_ADDR_MASK) | PAGE_PRESENT | PAGE_RW;
        for (uint32_t pd_index = 0; pd_index < 512u; pd_index++) {
            pd[pd_index] = (addr & 0x000fffffffe00000ull) | PAGE_PRESENT | PAGE_RW | PAGE_PS;
            addr += 0x200000ull;
        }
    }
}

static int parse_uint(const char **cursor, uint32_t *out) {
    uint32_t value = 0;
    int any = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10u + (uint32_t)(**cursor - '0');
        (*cursor)++;
        any = 1;
    }
    *out = value;
    return any;
}

static int parse_video_mode(const char *cmdline, uint32_t *width, uint32_t *height, uint32_t *bpp) {
    const char *cur = cmdline;

    while (cur != 0 && *cur != '\0') {
        if (strncmp(cur, "video=", 6) == 0) {
            cur += 6;
            if (!parse_uint(&cur, width) || *cur++ != 'x') {
                return 0;
            }
            if (!parse_uint(&cur, height) || *cur++ != 'x') {
                return 0;
            }
            return parse_uint(&cur, bpp);
        }
        cur++;
    }
    return 0;
}

static uint8_t mask_shift(uint32_t mask) {
    uint8_t shift = 0;

    if (mask == 0) {
        return 0;
    }
    while ((mask & 1u) == 0) {
        shift++;
        mask >>= 1;
    }
    return shift;
}

static uint8_t mask_size(uint32_t mask) {
    uint8_t size = 0;

    mask >>= mask_shift(mask);
    while ((mask & 1u) != 0) {
        size++;
        mask >>= 1;
    }
    return size;
}

static void fill_gop_console(struct bootx_console_info *console,
                             struct efi_graphics_output_protocol *gop) {
    struct efi_graphics_output_mode_information *info;

    memset(console, 0, sizeof(*console));
    if (gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0 ||
        gop->Mode->Info->PixelFormat == PixelBltOnly) {
        console->type = BOOTX_CONSOLE_NONE;
        return;
    }
    info = gop->Mode->Info;
    console->type = BOOTX_CONSOLE_FRAMEBUFFER;
    console->framebuffer_addr = gop->Mode->FrameBufferBase;
    console->width = info->HorizontalResolution;
    console->height = info->VerticalResolution;
    console->pitch = info->PixelsPerScanLine * 4u;
    console->framebuffer_bpp = 32;
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        console->red_mask_shift = 0;
        console->green_mask_shift = 8;
        console->blue_mask_shift = 16;
        console->red_mask_size = 8;
        console->green_mask_size = 8;
        console->blue_mask_size = 8;
    } else if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        console->red_mask_shift = 16;
        console->green_mask_shift = 8;
        console->blue_mask_shift = 0;
        console->red_mask_size = 8;
        console->green_mask_size = 8;
        console->blue_mask_size = 8;
    } else {
        console->red_mask_shift = mask_shift(info->PixelInformation.RedMask);
        console->green_mask_shift = mask_shift(info->PixelInformation.GreenMask);
        console->blue_mask_shift = mask_shift(info->PixelInformation.BlueMask);
        console->red_mask_size = mask_size(info->PixelInformation.RedMask);
        console->green_mask_size = mask_size(info->PixelInformation.GreenMask);
        console->blue_mask_size = mask_size(info->PixelInformation.BlueMask);
    }
    map_identity_range_4k(console->framebuffer_addr, gop->Mode->FrameBufferSize);
}

static struct efi_graphics_output_protocol *init_gop(const char *cmdline,
                                                     struct bootx_console_info *console) {
    struct efi_graphics_output_protocol *gop = 0;
    uint32_t want_width = 0;
    uint32_t want_height = 0;
    uint32_t want_bpp = 0;

    if (efi_is_error(g_bs->LocateProtocol(&g_gop_guid, 0, (void **)&gop)) || gop == 0) {
        memset(console, 0, sizeof(*console));
        console->type = BOOTX_CONSOLE_NONE;
        return 0;
    }
    if (parse_video_mode(cmdline, &want_width, &want_height, &want_bpp) && want_bpp == 32u) {
        for (uint32_t mode = 0; mode < gop->Mode->MaxMode; mode++) {
            struct efi_graphics_output_mode_information *info = 0;
            UINTN info_size = 0;
            EFI_STATUS status = gop->QueryMode(gop, mode, &info_size, &info);

            if (!efi_is_error(status) && info != 0 &&
                info->HorizontalResolution == want_width &&
                info->VerticalResolution == want_height &&
                info->PixelFormat != PixelBltOnly) {
                gop->SetMode(gop, mode);
                if (g_bs->FreePool != 0) {
                    g_bs->FreePool(info);
                }
                break;
            }
            if (info != 0 && g_bs->FreePool != 0) {
                g_bs->FreePool(info);
            }
        }
    }
    fill_gop_console(console, gop);
    return gop;
}

static void load_modules(const struct boot_config *config) {
    g_boot_block->info.modules = ptr_to_u32(g_boot_block->modules);
    g_boot_block->info.module_count = 0;
    for (uint32_t i = 0; i < config->module_count && i < BOOTX_MAX_MODULES; i++) {
        uint32_t size = 0;
        void *data = read_file_alloc(config->modules[i], &size);
        struct bootx_module *module = &g_boot_block->modules[g_boot_block->info.module_count];

        if (data == 0) {
            efi_puts("bootx64: module missing: ");
            efi_puts(config->modules[i]);
            efi_puts("\n");
            continue;
        }
        path_leaf_to_name_83(module->name, config->modules[i]);
        module->address = ptr_to_u32(data);
        module->size = size;
        g_boot_block->info.module_count++;
    }
}

static void map_kernel_segments(const uint8_t *kernel,
                                uint32_t kernel_size,
                                const struct elf64_ehdr *ehdr,
                                uint64_t kernel_phys_base,
                                uint64_t min_vaddr) {
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(kernel + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        uint64_t seg_start;
        uint64_t seg_end;

        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        if (ph->p_filesz > ph->p_memsz ||
            ph->p_offset > kernel_size ||
            ph->p_filesz > (uint64_t)kernel_size - ph->p_offset) {
            efi_fail("bad kernel segment");
        }
        memcpy((void *)(uintptr_t)(kernel_phys_base + (ph->p_vaddr - min_vaddr)),
               kernel + ph->p_offset,
               (size_t)ph->p_filesz);
        seg_start = align_down_u64(ph->p_vaddr, EFI_PAGE_SIZE);
        seg_end = align_up_u64(ph->p_vaddr + ph->p_memsz, EFI_PAGE_SIZE);
        for (uint64_t va = seg_start; va < seg_end; va += EFI_PAGE_SIZE) {
            uint64_t pa = kernel_phys_base + (va - min_vaddr);
            uint64_t flags = (ph->p_flags & 2u) != 0 ? PAGE_RW : 0;
            map_page_4k(va, pa, flags);
        }
    }
}

static void load_kernel_elf64(void *kernel_data, uint32_t kernel_size) {
    const uint8_t *kernel = (const uint8_t *)kernel_data;
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr *)kernel;
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    uint64_t span;
    void *kernel_phys;

    if (kernel_size < sizeof(*ehdr) ||
        ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F' ||
        ehdr->e_ident[4] != ELF_CLASS_64 ||
        ehdr->e_machine != ELF_MACHINE_X86_64 ||
        ehdr->e_phoff > kernel_size ||
        ehdr->e_phentsize < sizeof(struct elf64_phdr) ||
        (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > (uint64_t)kernel_size - ehdr->e_phoff) {
        efi_fail("kernel is not a valid ELF64 image");
    }
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(kernel + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        uint64_t seg_start;
        uint64_t seg_end;

        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        seg_start = align_down_u64(ph->p_vaddr, EFI_PAGE_SIZE);
        seg_end = align_up_u64(ph->p_vaddr + ph->p_memsz, EFI_PAGE_SIZE);
        if (seg_start < min_vaddr) {
            min_vaddr = seg_start;
        }
        if (seg_end > max_vaddr) {
            max_vaddr = seg_end;
        }
    }
    if (min_vaddr == UINT64_MAX || max_vaddr <= min_vaddr) {
        efi_fail("kernel has no loadable segments");
    }
    span = max_vaddr - min_vaddr;
    kernel_phys = uefi_alloc_low_pages(span);
    if (kernel_phys == 0) {
        efi_fail("kernel allocation failed");
    }
    map_kernel_segments(kernel, kernel_size, ehdr, (uint64_t)(uintptr_t)kernel_phys, min_vaddr);
    g_boot_block->info.kernel_phys_addr = (uint64_t)(uintptr_t)kernel_phys;
    g_boot_block->info.kernel_phys_size = span;
    g_boot_block->info.kernel_entry = ehdr->e_entry;
}

static uint32_t efi_mem_type_to_boot(uint32_t type, uint64_t base) {
    if (type == EfiConventionalMemory && base < BOOTX_UEFI_IDENTITY_LIMIT) {
        return BOOTX_MEMMAP_USABLE;
    }
    if (type == EfiACPIReclaimMemory) {
        return BOOTX_MEMMAP_ACPI_RECLAIMABLE;
    }
    if (type == EfiACPIMemoryNVS) {
        return BOOTX_MEMMAP_ACPI_NVS;
    }
    if (type == EfiUnusableMemory) {
        return BOOTX_MEMMAP_BAD;
    }
    if (type == EfiLoaderCode || type == EfiLoaderData ||
        type == EfiBootServicesCode || type == EfiBootServicesData) {
        return BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE;
    }
    return BOOTX_MEMMAP_RESERVED;
}

static void append_memmap_entry(uint64_t base, uint64_t length, uint32_t type) {
    struct bootx_memmap_entry *entries = g_boot_block->memmap;
    uint32_t count = g_boot_block->info.memmap_count;

    if (length == 0) {
        return;
    }
    if (count != 0) {
        struct bootx_memmap_entry *prev = &entries[count - 1u];
        if (prev->type == type && prev->base + prev->length == base) {
            prev->length += length;
            return;
        }
    }
    if (count >= BOOTX_MAX_MEMMAP) {
        return;
    }
    entries[count].base = base;
    entries[count].length = length;
    entries[count].type = type;
    entries[count].reserved = 0;
    g_boot_block->info.memmap_count = count + 1u;
}

static void convert_memory_map(struct efi_memory_descriptor *map, UINTN map_size, UINTN desc_size) {
    UINTN count = desc_size == 0 ? 0 : map_size / desc_size;

    g_boot_block->info.memmap = ptr_to_u32(g_boot_block->memmap);
    g_boot_block->info.memmap_count = 0;
    for (UINTN i = 0; i < count; i++) {
        struct efi_memory_descriptor *desc =
            (struct efi_memory_descriptor *)((uint8_t *)map + i * desc_size);
        uint64_t base = desc->PhysicalStart;
        uint64_t length = desc->NumberOfPages * EFI_PAGE_SIZE;
        uint64_t end = base + length;
        uint32_t type = efi_mem_type_to_boot(desc->Type, base);

        if (desc->Type == EfiConventionalMemory && base < BOOTX_UEFI_IDENTITY_LIMIT &&
            end > BOOTX_UEFI_IDENTITY_LIMIT) {
            append_memmap_entry(base, BOOTX_UEFI_IDENTITY_LIMIT - base, BOOTX_MEMMAP_USABLE);
            append_memmap_entry(BOOTX_UEFI_IDENTITY_LIMIT,
                                end - BOOTX_UEFI_IDENTITY_LIMIT,
                                BOOTX_MEMMAP_RESERVED);
        } else {
            append_memmap_entry(base, length, type);
        }
    }
}

static struct efi_memory_descriptor *get_memory_map(UINTN *map_size,
                                                    UINTN *map_key,
                                                    UINTN *desc_size,
                                                    uint32_t *desc_version,
                                                    UINTN *capacity) {
    struct efi_memory_descriptor *map;
    EFI_STATUS status;

    *map_size = 0;
    *desc_size = 0;
    status = g_bs->GetMemoryMap(map_size, 0, map_key, desc_size, desc_version);
    if (status != EFI_BUFFER_TOO_SMALL || *desc_size == 0) {
        efi_fail("GetMemoryMap size query failed");
    }
    *capacity = *map_size + (*desc_size * 32u) + EFI_PAGE_SIZE;
    map = (struct efi_memory_descriptor *)uefi_alloc_low_pages(*capacity);
    if (map == 0) {
        efi_fail("memory map allocation failed");
    }
    for (int attempt = 0; attempt < 4; attempt++) {
        *map_size = *capacity;
        status = g_bs->GetMemoryMap(map_size, map, map_key, desc_size, desc_version);
        if (!efi_is_error(status)) {
            return map;
        }
    }
    efi_fail("GetMemoryMap failed");
}

static void exit_boot_services(EFI_HANDLE image_handle) {
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINTN capacity = 0;
    uint32_t desc_version = 0;
    struct efi_memory_descriptor *map =
        get_memory_map(&map_size, &map_key, &desc_size, &desc_version, &capacity);

    (void)capacity;
    for (int attempt = 0; attempt < 3; attempt++) {
        EFI_STATUS status;

        convert_memory_map(map, map_size, desc_size);
        status = g_bs->ExitBootServices(image_handle, map_key);
        if (!efi_is_error(status)) {
            return;
        }
        map_size = capacity;
        status = g_bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
        if (efi_is_error(status)) {
            break;
        }
    }
    efi_fail("ExitBootServices failed");
}

static __attribute__((noreturn)) void enter_kernel(uint64_t pml4_phys,
                                                   uint64_t entry,
                                                   const struct bootx_boot_info *boot_info) {
    __asm__ __volatile__(
        "cli\n"
        "mov %0, %%cr3\n"
        "mov %2, %%rdi\n"
        "xor %%rbp, %%rbp\n"
        "jmp *%1\n"
        :
        : "r"(pml4_phys), "r"(entry), "r"(boot_info)
        : "memory", "rdi");
    efi_halt();
}

static void open_boot_volume(EFI_HANDLE image_handle) {
    struct efi_loaded_image_protocol *loaded = 0;
    struct efi_simple_file_system_protocol *fs = 0;

    if (efi_is_error(g_bs->HandleProtocol(image_handle, &g_loaded_image_guid, (void **)&loaded)) ||
        loaded == 0) {
        efi_fail("LoadedImage protocol missing");
    }
    if (efi_is_error(g_bs->HandleProtocol(loaded->DeviceHandle,
                                          &g_simple_file_system_guid,
                                          (void **)&fs)) ||
        fs == 0) {
        efi_fail("SimpleFileSystem protocol missing");
    }
    if (efi_is_error(fs->OpenVolume(fs, &g_root)) || g_root == 0) {
        efi_fail("OpenVolume failed");
    }
}

EFI_STATUS efi_main(EFI_HANDLE image_handle, struct efi_system_table *system_table) {
    struct boot_config config;
    void *cfg_data;
    uint32_t cfg_size = 0;
    void *kernel_data;
    uint32_t kernel_size = 0;

    g_st = system_table;
    g_bs = system_table->BootServices;
    if (g_bs->SetWatchdogTimer != 0) {
        g_bs->SetWatchdogTimer(0, 0, 0, 0);
    }
    efi_puts("boot/x UEFI\n");
    open_boot_volume(image_handle);
    config_init_default(&config);
    cfg_data = read_file_alloc("BOOT/BOOTX.CFG", &cfg_size);
    if (cfg_data == 0) {
        cfg_data = read_file_alloc("BOOTX.CFG", &cfg_size);
    }
    if (cfg_data != 0) {
        memset(config.modules, 0, sizeof(config.modules));
        config.module_count = 0;
        parse_config(&config, (const char *)cfg_data, cfg_size);
    }
    g_boot_block = (struct uefi_boot_block *)uefi_alloc_low_pages(sizeof(*g_boot_block));
    if (g_boot_block == 0) {
        efi_fail("boot info allocation failed");
    }
    g_boot_block->info.hdr.magic = BOOTX_MAGIC;
    g_boot_block->info.hdr.version = BOOTX_PROTOCOL_VERSION;
    g_boot_block->info.hdr.size = sizeof(g_boot_block->info);
    g_boot_block->info.boot_drive = 0x80u;
    g_boot_block->info.partition_lba = BOOTX_UEFI_DEFAULT_BOOT_LBA;
    g_boot_block->info.partition_sectors = BOOTX_UEFI_DEFAULT_BOOT_SECTORS;
    str_copy_limit(g_boot_block->cmdline, sizeof(g_boot_block->cmdline), config.cmdline);
    g_boot_block->info.cmdline = ptr_to_u32(g_boot_block->cmdline);
    build_identity_page_tables();
    init_gop(g_boot_block->cmdline, &g_boot_block->info.console);
    efi_puts("bootx64: kernel ");
    efi_puts(config.kernel);
    efi_puts("\n");
    kernel_data = read_file_alloc(config.kernel, &kernel_size);
    if (kernel_data == 0) {
        efi_fail("kernel read failed");
    }
    load_kernel_elf64(kernel_data, kernel_size);
    load_modules(&config);
    efi_puts("bootx64: entry=0x");
    efi_put_hex64(g_boot_block->info.kernel_entry);
    efi_puts(" modules=0x");
    efi_put_hex64(g_boot_block->info.module_count);
    efi_puts("\n");
    exit_boot_services(image_handle);
    enter_kernel((uint64_t)(uintptr_t)g_pml4,
                 g_boot_block->info.kernel_entry,
                 &g_boot_block->info);
}
