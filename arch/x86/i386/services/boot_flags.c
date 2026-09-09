#include "arch/x86/i386/services/shared_services.h"

#include <stdint.h>

static const char *g_boot_cmdline;

void boot_flags_init(const char *cmdline) {
    g_boot_cmdline = cmdline != 0 ? cmdline : "";
}

static int boot_flags_cmdline_has(const char *needle) {
    const char *cmdline = g_boot_cmdline;
    uint32_t len = 0u;

    if (needle == 0 || needle[0] == '\0') {
        return 0;
    }
    while (needle[len] != '\0') {
        len++;
    }
    while (cmdline != 0 && *cmdline != '\0') {
        while (*cmdline == ' ') {
            cmdline++;
        }
        if (cmdline[len] == '\0' || cmdline[len] == ' ') {
            int matched = 1;

            for (uint32_t i = 0u; i < len; i++) {
                if (cmdline[i] != needle[i]) {
                    matched = 0;
                    break;
                }
            }
            if (matched) {
                return 1;
            }
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    return 0;
}

int boot_flags_dev_selftest_enabled(void) {
    return boot_flags_cmdline_has("selftest=1") ||
           boot_flags_cmdline_has("i386.dev=1") ||
           boot_flags_cmdline_has("i386.selftest=1");
}

int boot_flags_full_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.fullsmoke=1") ||
           boot_flags_cmdline_has("nexbox32.fullsmoke=1");
}

int boot_flags_strict_mm_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.strictmm=1") ||
           boot_flags_cmdline_has("i386.strict-mm=1");
}

int boot_flags_ahci_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.ahcismoke=1");
}

int boot_flags_usb_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.usbsmoke=1");
}

int boot_flags_usb_hid_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.usbhidsmoke=1");
}

int boot_flags_rtl8139_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.rtl8139smoke=1");
}

int boot_flags_hda_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.hdasmoke=1");
}

int boot_flags_ac97_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.ac97smoke=1");
}

int boot_flags_gfx_editor_smoke_enabled(void) {
    return boot_flags_cmdline_has("i386.gfxeditorsmoke=1");
}

int boot_flags_driver_smoke_enabled(void) {
    return boot_flags_ahci_smoke_enabled() ||
           boot_flags_usb_smoke_enabled() ||
           boot_flags_usb_hid_smoke_enabled() ||
           boot_flags_rtl8139_smoke_enabled() ||
           boot_flags_hda_smoke_enabled() ||
           boot_flags_ac97_smoke_enabled();
}
