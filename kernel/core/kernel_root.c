#include <stdint.h>
#include "janus/janus.h"
#include "drivers/storage/ata.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/xhci.h"
#include "fs/vfs.h"
#include "hal/hal.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "kernel/public/core/kprint.h"
#include "lib/string.h"

enum {
    KERNEL_ROOT_USB_RETRY_COUNT = 8u,
    KERNEL_ROOT_USB_RETRY_RELAX_LOOPS = 200000u
};

void kernel_log_ata_info(void) {
    struct ata_device *primary = ata_get_primary_master();

    if (primary != 0 && primary->present != 0u) {
        kprint("ata: primary master sectors=%u model=%s state=%u resets=%u r=%u w=%u f=%u errors=%u last=%u status=%x\n",
               primary->sector_count,
               primary->model,
               (uint32_t)primary->state,
               primary->reset_count,
               primary->read_count,
               primary->write_count,
               primary->flush_count,
               primary->error_count,
               primary->last_error,
               (uint32_t)primary->last_status);
    } else {
        kprint("ata: primary master not found\n");
    }
}

static int kernel_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int kernel_parse_uuid(const char *text, uint32_t len, uint8_t uuid_out[16]) {
    uint32_t digits = 0;
    int high = -1;

    if (text == 0 || uuid_out == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < 16u; i++) {
        uuid_out[i] = 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        int value;

        if (text[i] == '-') {
            continue;
        }
        value = kernel_hex_value(text[i]);
        if (value < 0 || digits >= 32u) {
            return 0;
        }
        if ((digits & 1u) == 0u) {
            high = value;
        } else {
            uuid_out[digits / 2u] = (uint8_t)((uint32_t)(high << 4) | (uint32_t)value);
            high = -1;
        }
        digits++;
    }
    return digits == 32u;
}

static int kernel_extract_root_uuid(const char *cmdline, uint8_t uuid_out[16]) {
    if (cmdline == 0 || uuid_out == 0) {
        return 0;
    }
    while (*cmdline != '\0') {
        uint32_t len = 0;

        cmdline = skip_spaces(cmdline);
        if (*cmdline == '\0') {
            break;
        }
        if (starts_with(cmdline, "root=UUID=")) {
            const char *uuid_text = cmdline + 10;

            while (uuid_text[len] != '\0' && uuid_text[len] != ' ') {
                len++;
            }
            return kernel_parse_uuid(uuid_text, len, uuid_out);
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    return 0;
}

static int kernel_cmdline_has_token(const char *cmdline, const char *token) {
    uint32_t token_len;

    if (cmdline == 0 || token == 0 || token[0] == '\0') {
        return 0;
    }
    token_len = str_len(token);
    while (*cmdline != '\0') {
        cmdline = skip_spaces(cmdline);
        if (*cmdline == '\0') {
            break;
        }
        if (starts_with(cmdline, token) &&
            (cmdline[token_len] == '\0' || cmdline[token_len] == ' ')) {
            return 1;
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    return 0;
}

static int kernel_root_usb_wait_requested(const char *cmdline) {
    return kernel_cmdline_has_token(cmdline, "rootwait=usb") ||
           kernel_cmdline_has_token(cmdline, "root.usbwait=1");
}

static void kernel_root_usb_retry_delay(void) {
    for (uint32_t i = 0u; i < KERNEL_ROOT_USB_RETRY_RELAX_LOOPS; i++) {
        hal_cpu_relax();
    }
}

static int kernel_switch_root_to_uuid_with_usb_wait(struct vfs *vfs,
                                                    const uint8_t uuid[16],
                                                    int usb_wait) {
    if (vfs_switch_root_to_nxfs_uuid(vfs, uuid) == 0) {
        return 1;
    }
    if (!usb_wait) {
        return 0;
    }
    for (uint32_t attempt = 0u; attempt < KERNEL_ROOT_USB_RETRY_COUNT; attempt++) {
        ehci_hotplug_scan_now();
        xhci_hotplug_scan_now();
        if (vfs_switch_root_to_nxfs_uuid(vfs, uuid) == 0) {
            kprint("root: switched by UUID after USB scan\n");
            return 1;
        }
        kernel_root_usb_retry_delay();
    }
    return 0;
}

int kernel_apply_root_cmdline(struct vfs *vfs, const struct janus_boot_info *boot_info) {
    uint8_t uuid[16];
    const char *cmdline;

    if (vfs == 0 || boot_info == 0 || boot_info->cmdline == 0) {
        return 0;
    }
    cmdline = (const char *)(uintptr_t)boot_info->cmdline;
    if (!kernel_extract_root_uuid(cmdline, uuid)) {
        return 0;
    }
    if (!kernel_switch_root_to_uuid_with_usb_wait(vfs,
                                                  uuid,
                                                  kernel_root_usb_wait_requested(cmdline))) {
        kprint("root: UUID target not found\n");
        return -1;
    }
    kprint("root: switched by UUID\n");
    return 1;
}
