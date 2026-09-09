#include "kernel/public/driver/driver.h"
#include "drivers/audio/audio.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "drivers/bus/pci.h"
#include "drivers/net/rtl8139.h"
#include "fs/vfs.h"
#include "kernel/public/core/kprint.h"
#include "kernel/internal/driver/driver_loader_internal.h"
#include "lib/string.h"

#define DRIVER_MAX_COUNT 32u
#define DRIVER_FILE_MAX_COUNT 32u
static struct kernel_driver_record g_driver_records[DRIVER_MAX_COUNT];
static struct kernel_driver_file g_driver_files[DRIVER_FILE_MAX_COUNT];
static uint32_t g_driver_count;
static uint32_t g_driver_file_count;
static int g_driver_boot_verbose;

static char driver_hex_digit_local(uint32_t value) {
    value &= 0xfu;
    return (char)(value < 10u ? '0' + value : 'a' + (value - 10u));
}

static void driver_device_clear_local(struct kernel_driver_record *record) {
    if (record != NULL) {
        record->device[0] = '-';
        record->device[1] = '\0';
    }
}

static void driver_device_set_pci_local(struct kernel_driver_record *record,
                                        uint32_t bus,
                                        uint32_t slot,
                                        uint32_t function,
                                        uint32_t vendor,
                                        uint32_t device) {
    uint32_t pos = 0u;

    if (record == NULL) {
        return;
    }
#define DRIVER_PUT_CH(ch) do { if (pos + 1u < sizeof(record->device)) record->device[pos++] = (char)(ch); } while (0)
#define DRIVER_PUT_HEX(value, digits) do { \
        for (uint32_t _shift = (uint32_t)(digits) * 4u; _shift != 0u; _shift -= 4u) { \
            DRIVER_PUT_CH(driver_hex_digit_local(((uint32_t)(value)) >> (_shift - 4u))); \
        } \
    } while (0)
    DRIVER_PUT_CH('p'); DRIVER_PUT_CH('c'); DRIVER_PUT_CH('i'); DRIVER_PUT_CH(' ');
    DRIVER_PUT_HEX(bus, 2u);
    DRIVER_PUT_CH(':');
    DRIVER_PUT_HEX(slot, 2u);
    DRIVER_PUT_CH('.');
    DRIVER_PUT_HEX(function, 1u);
    DRIVER_PUT_CH(' ');
    DRIVER_PUT_HEX(vendor, 4u);
    DRIVER_PUT_CH(':');
    DRIVER_PUT_HEX(device, 4u);
    record->device[pos] = '\0';
#undef DRIVER_PUT_HEX
#undef DRIVER_PUT_CH
}

static void driver_update_device_binding_local(struct kernel_driver_record *record) {
    if (record == NULL || record->driver == NULL || record->driver->name == NULL) {
        return;
    }
    driver_device_clear_local(record);
    if (streq(record->driver->name, "AC97")) {
        struct ac97_status status;

        if (ac97_query_status(&status)) {
            driver_device_set_pci_local(record,
                                        status.bus,
                                        status.slot,
                                        status.function,
                                        status.vendor_id,
                                        status.device_id);
        }
        return;
    }
    if (streq(record->driver->name, "HDA")) {
        struct hda_status status;

        if (hda_query_status(&status)) {
            driver_device_set_pci_local(record,
                                        status.bus,
                                        status.slot,
                                        status.function,
                                        status.vendor_id,
                                        status.device_id);
        }
        return;
    }
    if (streq(record->driver->name, "rtl8139")) {
        struct rtl8139_status status;

        if (rtl8139_query_status(&status)) {
            driver_device_set_pci_local(record,
                                        status.bus,
                                        status.slot,
                                        status.function,
                                        status.vendor_id,
                                        status.device_id);
        }
    }
}

static void driver_copy_text_local(char *dst, const char *src, uint32_t dst_size);
static char driver_ascii_upper_local(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static int driver_name_has_drv_suffix_local(const char *name) {
    uint32_t len;

    if (name == NULL) {
        return 0;
    }
    len = str_len(name);
    if (len < 5u) {
        return 0;
    }
    return driver_ascii_upper_local(name[len - 4u]) == '.' &&
           driver_ascii_upper_local(name[len - 3u]) == 'D' &&
           driver_ascii_upper_local(name[len - 2u]) == 'R' &&
           driver_ascii_upper_local(name[len - 1u]) == 'V';
}

static void driver_copy_text_local(char *dst, const char *src, uint32_t dst_size) {
    uint32_t i = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int driver_join_path_local(char *out,
                                  uint32_t out_size,
                                  const char *directory,
                                  const char *name) {
    uint32_t pos = 0;
    uint32_t i = 0;

    if (out == NULL || out_size < 2u || directory == NULL ||
        name == NULL || name[0] == '\0') {
        return 0;
    }
    while (directory[i] != '\0') {
        if (pos + 1u >= out_size) {
            return 0;
        }
        out[pos++] = directory[i++];
    }
    if (pos != 0u && out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return 0;
        }
        out[pos++] = '/';
    }
    i = 0;
    while (name[i] != '\0') {
        if (pos + 1u >= out_size) {
            return 0;
        }
        out[pos++] = name[i++];
    }
    out[pos] = '\0';
    return 1;
}

static int driver_file_exists_local(const char *path) {
    uint32_t i;

    if (path == NULL) {
        return 0;
    }
    for (i = 0; i < g_driver_file_count; i++) {
        if (streq(g_driver_files[i].path, path)) {
            return 1;
        }
    }
    return 0;
}

static int driver_name_valid_local(const char *name) {
    uint32_t len = 0;

    if (name == NULL) {
        return 0;
    }
    while (name[len] != '\0') {
        len++;
        if (len > KERNEL_DRIVER_NAME_MAX) {
            return 0;
        }
    }
    return len != 0;
}

static struct kernel_driver_record *driver_find_mutable_local(const char *name) {
    uint32_t i;

    if (!driver_name_valid_local(name)) {
        return NULL;
    }
    for (i = 0; i < g_driver_count; i++) {
        if (g_driver_records[i].driver != NULL &&
            streq(g_driver_records[i].driver->name, name)) {
            return &g_driver_records[i];
        }
    }
    return NULL;
}

static const char *driver_record_state_name_local(enum kernel_driver_state state) {
    switch (state) {
        case KERNEL_DRIVER_STATE_REGISTERED:
            return "registered";
        case KERNEL_DRIVER_STATE_ACTIVE:
            return "active";
        case KERNEL_DRIVER_STATE_INACTIVE:
            return "inactive";
        case KERNEL_DRIVER_STATE_FAILED:
            return "failed";
        default:
            return "empty";
    }
}

static void driver_sync_loaded_file_records_local(void) {
    uint32_t i;

    for (i = 0; i < g_driver_file_count; i++) {
        struct kernel_driver_file *file = &g_driver_files[i];
        const struct kernel_driver_record *record;

        if (file->state != KERNEL_DRIVER_FILE_LOADED ||
            file->driver_name[0] == '\0') {
            continue;
        }
        record = driver_find_mutable_local(file->driver_name);
        if (record == NULL) {
            continue;
        }
        if (record->state == KERNEL_DRIVER_STATE_ACTIVE ||
            record->state == KERNEL_DRIVER_STATE_INACTIVE ||
            record->state == KERNEL_DRIVER_STATE_FAILED) {
            file->reason_code = record->reason_code;
            file->reason = record->reason;
            if (g_driver_boot_verbose) {
                kprint("driver: file %s driver=%s state=%s reason=%s\n",
                       file->path,
                       file->driver_name,
                       driver_record_state_name_local(record->state),
                       record->reason != NULL ? record->reason : "-");
            }
        }
    }
}

void driver_manager_init(void) {
    uint32_t i;

    for (i = 0; i < DRIVER_MAX_COUNT; i++) {
        g_driver_records[i].driver = NULL;
        g_driver_records[i].state = KERNEL_DRIVER_STATE_EMPTY;
        g_driver_records[i].init_result = 0;
        g_driver_records[i].source = "builtin";
        g_driver_records[i].path = "-";
        g_driver_records[i].reason_code = KERNEL_DRIVER_REASON_EMPTY;
        g_driver_records[i].reason = "empty";
        driver_device_clear_local(&g_driver_records[i]);
    }
    for (i = 0; i < DRIVER_FILE_MAX_COUNT; i++) {
        g_driver_files[i].name[0] = '\0';
        g_driver_files[i].path[0] = '\0';
        g_driver_files[i].driver_name[0] = '\0';
        g_driver_files[i].size = 0;
        g_driver_files[i].elf_class = 0;
        g_driver_files[i].elf_data = 0;
        g_driver_files[i].elf_type = 0;
        g_driver_files[i].elf_machine = 0;
        g_driver_files[i].state = KERNEL_DRIVER_FILE_DISCOVERED;
        g_driver_files[i].reason_code = KERNEL_DRIVER_REASON_EMPTY;
        g_driver_files[i].reason = "empty";
    }
    g_driver_count = 0;
    g_driver_file_count = 0;
    g_driver_boot_verbose = 0;
}

void driver_set_boot_verbose(int verbose) {
    g_driver_boot_verbose = verbose != 0;
}

int driver_boot_verbose_enabled(void) {
    return g_driver_boot_verbose;
}

int driver_register_source(const struct kernel_driver *driver,
                           const char *source,
                           const char *path) {
    struct kernel_driver_record *record;

    if (driver == NULL || !driver_name_valid_local(driver->name) || driver->init == NULL) {
        return 0;
    }
    if (g_driver_count >= DRIVER_MAX_COUNT) {
        return 0;
    }
    record = driver_find_mutable_local(driver->name);
    if (record != NULL) {
        return 0;
    }

    g_driver_records[g_driver_count].driver = driver;
    g_driver_records[g_driver_count].state = KERNEL_DRIVER_STATE_REGISTERED;
    g_driver_records[g_driver_count].init_result = 0;
    g_driver_records[g_driver_count].source = source != NULL ? source : "builtin";
    g_driver_records[g_driver_count].path = path != NULL ? path : "-";
    g_driver_records[g_driver_count].reason_code = KERNEL_DRIVER_REASON_REGISTERED;
    g_driver_records[g_driver_count].reason = "registered";
    driver_device_clear_local(&g_driver_records[g_driver_count]);
    g_driver_count++;
    return 1;
}

int driver_register(const struct kernel_driver *driver) {
    return driver_register_source(driver, "builtin", "-");
}

uint32_t driver_init_all(void) {
    uint32_t active_count = 0;
    uint32_t i;
    int result;

    for (i = 0; i < g_driver_count; i++) {
        if (g_driver_records[i].state != KERNEL_DRIVER_STATE_REGISTERED ||
            g_driver_records[i].driver == NULL ||
            g_driver_records[i].driver->init == NULL) {
            continue;
        }

        if (g_driver_boot_verbose) {
            kprint("driver: init %s\n", g_driver_records[i].driver->name);
        }
        result = g_driver_records[i].driver->init();
        g_driver_records[i].init_result = result;
        if (result > 0) {
            g_driver_records[i].state = KERNEL_DRIVER_STATE_ACTIVE;
            g_driver_records[i].reason_code = KERNEL_DRIVER_REASON_INIT_OK;
            g_driver_records[i].reason = "init-ok";
            active_count++;
        } else if (result == 0) {
            g_driver_records[i].state = KERNEL_DRIVER_STATE_INACTIVE;
            g_driver_records[i].reason_code = KERNEL_DRIVER_REASON_MISSING_HARDWARE;
            g_driver_records[i].reason = "missing-hardware";
        } else {
            g_driver_records[i].state = KERNEL_DRIVER_STATE_FAILED;
            g_driver_records[i].reason_code = KERNEL_DRIVER_REASON_INIT_FAILED;
            g_driver_records[i].reason = "init-failed";
        }
        if (g_driver_boot_verbose) {
            kprint("driver: init %s result=%d reason=%s\n",
                   g_driver_records[i].driver->name,
                   result,
                   g_driver_records[i].reason != NULL ? g_driver_records[i].reason : "-");
        }
        driver_update_device_binding_local(&g_driver_records[i]);
    }
    driver_sync_loaded_file_records_local();
    return active_count;
}

const struct kernel_driver_record *driver_find(const char *name) {
    return driver_find_mutable_local(name);
}

const struct kernel_driver_record *driver_get(uint32_t index) {
    if (index >= g_driver_count) {
        return NULL;
    }
    return &g_driver_records[index];
}

uint32_t driver_count(void) {
    return g_driver_count;
}

enum kernel_driver_file_state driver_arch_probe_file(struct vfs *vfs,
                                                     struct vfs_node *node,
                                                     struct kernel_driver_file *file)
    __attribute__((weak));
enum kernel_driver_file_state driver_arch_probe_file(struct vfs *vfs,
                                                     struct vfs_node *node,
                                                     struct kernel_driver_file *file) {
    (void)vfs;
    (void)node;
    if (file != NULL) {
        file->elf_class = 0u;
        file->elf_data = 0u;
        file->elf_type = 0u;
        file->elf_machine = 0u;
    }
    return KERNEL_DRIVER_FILE_ELF_INVALID;
}

int driver_arch_load_file(struct vfs *vfs, struct kernel_driver_file *file)
    __attribute__((weak));
int driver_arch_load_file(struct vfs *vfs, struct kernel_driver_file *file) {
    (void)vfs;
    (void)file;
    return 0;
}

uint32_t driver_discover_root(struct vfs *vfs, const char *directory) {
    struct vfs_node dir_node;
    struct vfs_node file_node;
    struct vfs_dirent entry;
    uint32_t index = 0;
    uint32_t found = 0;
    char path[NOS_PATH_BUFFER_SIZE];

    if (vfs == NULL || directory == NULL || directory[0] == '\0') {
        return 0;
    }
    if (vfs_opendir(vfs, directory, &dir_node) != 0) {
        if (g_driver_boot_verbose) {
            kprint("driver: directory not found %s\n", directory);
        }
        return 0;
    }

    while (vfs_readdir(vfs, &dir_node, &index, &entry) == 1) {
        if (!driver_name_has_drv_suffix_local(entry.name)) {
            continue;
        }
        if (!driver_join_path_local(path, sizeof(path), directory, entry.name)) {
            continue;
        }
        if (driver_file_exists_local(path)) {
            continue;
        }
        if (vfs_open(vfs, path, 0, &file_node) != 0 || file_node.kind != VFS_NODE_FILE) {
            continue;
        }
        if (g_driver_file_count >= DRIVER_FILE_MAX_COUNT) {
            kprint("driver: .DRV table full while scanning %s\n", directory);
            break;
        }
        driver_copy_text_local(g_driver_files[g_driver_file_count].name,
                               entry.name,
                               sizeof(g_driver_files[g_driver_file_count].name));
        driver_copy_text_local(g_driver_files[g_driver_file_count].path,
                               path,
                               sizeof(g_driver_files[g_driver_file_count].path));
        g_driver_files[g_driver_file_count].size = vfs_node_file_size(&file_node);
        g_driver_files[g_driver_file_count].state =
            driver_arch_probe_file(vfs, &file_node, &g_driver_files[g_driver_file_count]);
        if (g_driver_files[g_driver_file_count].state == KERNEL_DRIVER_FILE_ELF_RELOC) {
            g_driver_files[g_driver_file_count].reason_code =
                KERNEL_DRIVER_REASON_PROBE_OK;
            g_driver_files[g_driver_file_count].reason = "probe-ok";
        } else {
            g_driver_files[g_driver_file_count].reason_code =
                KERNEL_DRIVER_REASON_UNSUPPORTED_ELF;
            g_driver_files[g_driver_file_count].reason = "unsupported-elf";
        }
        if (g_driver_boot_verbose) {
            kprint("driver: discovered %s size=%u state=%u\n",
                   g_driver_files[g_driver_file_count].path,
                   g_driver_files[g_driver_file_count].size,
                   (uint32_t)g_driver_files[g_driver_file_count].state);
        }
        g_driver_file_count++;
        found++;
    }
    if (found == 0u && g_driver_boot_verbose) {
        kprint("driver: no .DRV files in %s\n", directory);
    }
    return found;
}

uint32_t driver_load_all(struct vfs *vfs) {
    uint32_t loaded = 0;

    if (vfs == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < g_driver_file_count; i++) {
        if (g_driver_files[i].state != KERNEL_DRIVER_FILE_ELF_RELOC) {
            continue;
        }
        if (driver_arch_load_file(vfs, &g_driver_files[i])) {
            g_driver_files[i].state = KERNEL_DRIVER_FILE_LOADED;
            g_driver_files[i].reason_code = KERNEL_DRIVER_REASON_LOADED;
            g_driver_files[i].reason = "loaded";
            loaded++;
        } else {
            g_driver_files[i].state = KERNEL_DRIVER_FILE_LOAD_FAILED;
            if (g_driver_files[i].reason_code == KERNEL_DRIVER_REASON_NONE ||
                g_driver_files[i].reason_code == KERNEL_DRIVER_REASON_PROBE_OK) {
                g_driver_files[i].reason_code = KERNEL_DRIVER_REASON_LOAD_FAILED;
            }
            g_driver_files[i].reason = "load-failed";
            kprint("driver: load failed %s\n", g_driver_files[i].path);
        }
    }
    return loaded;
}

const struct kernel_driver_file *driver_get_file(uint32_t index) {
    if (index >= g_driver_file_count) {
        return NULL;
    }
    return &g_driver_files[index];
}

uint32_t driver_file_count(void) {
    return g_driver_file_count;
}
