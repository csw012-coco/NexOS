#include "block/blockdev.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "drivers/net/rtl8139.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/xhci.h"
#include "hal/hal.h"
#include "kernel/internal/core/i386_shared_services_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/boot_user_init.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"

extern int kernel_i386_run_command(const char *command,
                                   struct process_snapshot *process);

int i386_run_test32_selftest(void) {
    struct boot_user_init_config config;

    i386_boot_user_init_config(&config);
    return boot_user_init_run_selftest(&config);
}

int i386_run_nexbox32_full_smoke(void) {
    static const char *const commands[] = {
        "/cmd/nexbox echo nexbox32",
        "/cmd/nexbox help",
        "/cmd/nexbox pwd",
        "/cmd/nexbox tty",
        "/cmd/nexbox env",
        "/cmd/nexbox font --table",
        "/cmd/nexbox font sample",
        "/cmd/nexbox font --utf8-check",
        "/cmd/nexbox clipboard --utf8-check",
        "/cmd/nexbox actions",
        "/cmd/nexbox actions --table",
        "/cmd/nexbox action list",
        "/cmd/nexbox action caps",
        "/cmd/nexbox clipboard size",
        "/cmd/nexbox clipboard clear",
        "/cmd/nexbox clipboard set smoke-clipboard",
        "/cmd/nexbox clipboard get",
        "/cmd/nexbox clipboard size",
        "/cmd/nexbox clipboard clear",
        "/cmd/nexbox events",
        "/cmd/nexbox events jobs",
        "/cmd/nexbox config schema",
        "/cmd/nexbox config schema ring3_smoke",
        "/cmd/nexbox config validate",
        "/cmd/nexbox config list",
        "/cmd/nexbox config get ring3_smoke",
        "/cmd/nexbox config source ring3_smoke",
        "/cmd/nexbox config get mouse.cursor",
        "/cmd/nexbox config source mouse.cursor",
        "/cmd/nexbox which ls",
        "/cmd/nexbox type ls",
        "/cmd/nexbox uname -a",
        "/cmd/nexbox cpuinfo",
        "/cmd/nexbox sysinfo",
        "/cmd/nexbox meminfo",
        "/cmd/nexbox minfo",
        "/cmd/nexbox date --raw",
        "/cmd/nexbox hwclock",
        "/cmd/nexbox parts",
        "/cmd/nexbox mounts",
        "/cmd/nexbox df",
        "/cmd/nexbox blk",
        "/cmd/nexbox progs",
        "/cmd/nexbox stat --table /cmd/ush",
        "/cmd/nexbox file --table /cmd/nexbox",
        "/cmd/nexbox du -s /cmd",
        "/cmd/nexbox tree --table /cmd",
        "/cmd/nexbox hexdump /cmd/ls",
        "/cmd/nexbox wc /cmd/ls",
        "/cmd/nexbox head -c 64 /cmd/ls",
        "/cmd/nexbox tail /cmd/ls 2",
        "/cmd/nexbox grep arch=i386 /proc/cmdline",
        "/cmd/nexbox find /cmd ush",
        "/cmd/nexbox fatls",
        "/cmd/nexbox fatread BOOT/BOOTX.CFG",
        "/cmd/nexbox cat /boot/BOOT/BOOTX.CFG",
        "/cmd/nexbox fdisk",
        "/cmd/nexbox fdisk 0",
        "/cmd/nexbox mkdir /ram/nexbox32-smoke.dir",
        "/cmd/nexbox rmdir /ram/nexbox32-smoke.dir",
        "/cmd/nexbox touch /ram/nexbox32-smoke.tmp",
        "/cmd/nexbox stat --table /ram/nexbox32-smoke.tmp",
        "/cmd/nexbox cp /cmd/ush /ram/nexbox32-smoke.copy",
        "/cmd/nexbox stat --table /ram/nexbox32-smoke.copy",
        "/cmd/nexbox mv /ram/nexbox32-smoke.copy /ram/nexbox32-smoke.move",
        "/cmd/nexbox stat --table /ram/nexbox32-smoke.move",
        "/cmd/nexbox dd if=/cmd/ush of=/ram/nexbox32-smoke.dd bs=64 count=1",
        "/cmd/nexbox stat --table /ram/nexbox32-smoke.dd",
        "/cmd/nexbox as --check",
        "/cmd/nexbox pick --check",
        "/cmd/nexbox select --check",
        "/cmd/nexbox sort-by --check",
        "/cmd/nexbox count-by --check",
        "/cmd/nexbox to --check",
        "/cmd/nexbox view --check",
        "/cmd/nexbox run /cmd/nexbox echo run-ok",
        "/cmd/nexbox runelf /cmd/nexbox echo runelf-ok",
        "/cmd/nexbox runbg /cmd/nexbox echo runbg-ok",
        "/cmd/nexbox wait",
        "/cmd/nexbox timeout 1s /cmd/nexbox echo timeout-ok",
        "/cmd/nexbox alarm 1s /cmd/nexbox echo alarm-ok",
        "/cmd/nexbox cpio -o /ram/nexbox32-smoke.cpio /proc/version",
        "/cmd/nexbox cpio -t /ram/nexbox32-smoke.cpio",
        "/cmd/nexbox session save smoke32",
        "/cmd/nexbox session list",
        "/cmd/nexbox session info smoke32",
        "/cmd/nexbox session load smoke32",
        "/cmd/nexbox service define smoke32 /cmd/nexbox echo service-ok",
        "/cmd/nexbox service list",
        "/cmd/nexbox service info smoke32",
        "/cmd/nexbox rm -f /ram/nexbox32-smoke.tmp",
        "/cmd/nexbox rm -f /ram/nexbox32-smoke.move",
        "/cmd/nexbox rm -f /ram/nexbox32-smoke.dd",
        "/cmd/nexbox rm -f /ram/nexbox32-smoke.cpio",
        "/cmd/nexbox ps",
        "/cmd/nexbox jobs",
        "/cmd/nexbox drivers",
        "/cmd/nexbox cat /proc/devices",
        "/cmd/nexbox cat /proc/mounts",
        "/cmd/nexbox cat /proc/drivers",
        "/cmd/nexbox dmesg",
        "/cmd/nexbox lspci",
        "/cmd/nexbox doctor --table",
        "/cmd/nexbox config schema",
        "/cmd/nexbox config validate",
        "/cmd/nexbox config list",
        "/cmd/nexbox clipboard clear",
        "/cmd/nexbox clipboard set nexbox32-clipboard-smoke",
        "/cmd/nexbox clipboard size",
        "/cmd/nexbox clipboard get",
        "/cmd/nexbox clipboard clear",
        "/cmd/nexbox clipboard --utf8-check",
        "/cmd/nexbox events jobs",
        "/cmd/nexbox nexctl status",
        "/cmd/nexbox nexctl mounts",
        "/cmd/nexbox nexctl apps",
        "/cmd/nexbox nexctl storage",
        "/cmd/nexbox dbg ticks",
        "/cmd/nexbox dbg pmm",
        "/cmd/nexbox mapper info ifconfig",
        "/cmd/nexbox action info net.config",
        "/cmd/nexbox action policy explain net.config",
        "/cmd/nexbox mapper info fb",
        "/cmd/nexbox action info gfx.fb",
        "/cmd/nexbox action policy explain gfx.fb",
        "/cmd/nexbox fb",
        "/cmd/nexbox fb --smoke",
        "/cmd/nexbox fb --blit-smoke",
        "/cmd/nexbox fb --batch-smoke",
        "/cmd/nexbox cat /proc/fb",
        "/cmd/nexbox font --utf8-check",
        "/cmd/nexbox mapper info ed",
        "/cmd/nexbox mapper info vi",
        "/cmd/nexbox mapper info vim",
        "/cmd/nexbox action info editor.ed",
        "/cmd/nexbox action info editor.vi",
        "/cmd/nexbox action policy explain editor.ed",
        "/cmd/nexbox action policy explain editor.vi",
        "/cmd/nexbox ed --check",
        "/cmd/nexbox vi --check",
        "/cmd/nexbox vim --check",
        "/cmd/doom32 -iwad /home/doom/doom1.wad -nogui -mb 6 -smoke-frames 3",
        "/cmd/nexbox mapper info audio",
        "/cmd/nexbox mapper info tone",
        "/cmd/nexbox mapper info mplay",
        "/cmd/nexbox action info audio.list",
        "/cmd/nexbox action info audio.tone",
        "/cmd/nexbox action info audio.play_wav",
        "/cmd/nexbox action policy explain audio.list",
        "/cmd/nexbox action policy explain audio.tone",
        "/cmd/nexbox action policy explain audio.play_wav",
        "/cmd/nexbox ac97",
        "/cmd/nexbox hda",
        "/cmd/nexbox audio",
        "/cmd/nexbox tone --check",
        "/cmd/nexbox wav --check",
        "/cmd/nexbox mplay --check",
        "/cmd/nexbox sysinfo",
        "/cmd/nexbox meminfo",
        "/cmd/nexbox minfo",
        "/cmd/nexbox cpuinfo",
        "/cmd/nexbox uname",
        "/cmd/nexbox uname -a",
        "/cmd/nexbox rtl8139",
        "/cmd/nexbox route",
        "/cmd/nexbox netstat",
        "/cmd/nexbox ifconfig",
    };
    struct boot_user_init_config config;
    struct process_snapshot process;

    i386_boot_user_init_config(&config);
    if (config.ops == 0 || config.ops->run_command == 0) {
        i386_boot_log("nexbox32: applet smoke unavailable");
        return 0;
    }
    kprint("test32: RUN /cmd/test32 fork\n");
    if (!config.ops->run_command("/cmd/test32 fork", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork smoke PASS");
    kprint("test32: RUN /cmd/test32 fork-cow-ownership\n");
    if (!config.ops->run_command("/cmd/test32 fork-cow-ownership", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork-cow-ownership status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork COW ownership PASS");
    kprint("test32: RUN /cmd/test32 fork-cow-cleanup\n");
    if (!config.ops->run_command("/cmd/test32 fork-cow-cleanup", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork-cow-cleanup status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork COW cleanup PASS");
    kprint("test32: RUN /cmd/test32 fork-shared\n");
    if (!config.ops->run_command("/cmd/test32 fork-shared", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork-shared status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork shared mmap PASS");
    kprint("test32: RUN /cmd/test32 fork-map-table\n");
    if (!config.ops->run_command("/cmd/test32 fork-map-table", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork-map-table status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork mapping table PASS");
    kprint("test32: RUN /cmd/test32 fork-mmap-exec\n");
    if (!config.ops->run_command("/cmd/test32 fork-mmap-exec", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork-mmap-exec status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork mmap exec PASS");
    kprint("test32: RUN /cmd/test32 exec-fail-cleanup\n");
    if (!config.ops->run_command("/cmd/test32 exec-fail-cleanup", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 exec-fail-cleanup status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: exec failure cleanup PASS");
    kprint("test32: RUN /cmd/test32 shared-fault-cleanup\n");
    if (!config.ops->run_command("/cmd/test32 shared-fault-cleanup", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 shared-fault-cleanup status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: shared fault cleanup PASS");
    kprint("test32: RUN /cmd/test32 invalid-pointer-cleanup\n");
    if (!config.ops->run_command("/cmd/test32 invalid-pointer-cleanup", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 invalid-pointer-cleanup status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: invalid pointer cleanup PASS");
    kprint("test32: RUN /cmd/test32 shm-lifecycle\n");
    if (!config.ops->run_command("/cmd/test32 shm-lifecycle", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 shm-lifecycle status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: shm lifecycle PASS");
    kprint("test32: RUN /cmd/test32 fork-wait-exec\n");
    if (!config.ops->run_command("/cmd/test32 fork-wait-exec", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("test32: FAIL /cmd/test32 fork-wait-exec status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("test32: fork wait exec PASS");
    for (uint32_t i = 0u; i < sizeof(commands) / sizeof(commands[0]); i++) {
        kprint("nexbox32: RUN %s\n", commands[i]);
        if (!config.ops->run_command(commands[i], &process) ||
            process.state != PROCESS_STATE_EXITED ||
            process.exit_code != 0) {
            kprint("nexbox32: FAIL %s status=%d\n",
                   commands[i],
                   process.exit_code);
            return 0;
        }
        if (starts_with(commands[i], "/cmd/doom32 ")) {
            kprint("doomgeneric: smoke PASS frames=3\n");
        }
    }
    i386_boot_log("nexbox32: full applet smoke PASS");
    return 1;
}

static int i386_bytes_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < size; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static int i386_name_starts_with(const char *name, const char *prefix) {
    if (name == 0 || prefix == 0) {
        return 0;
    }
    while (*prefix != '\0') {
        if (*name != *prefix) {
            return 0;
        }
        name++;
        prefix++;
    }
    return 1;
}

static struct block_device *i386_find_block_device_with_prefix(const char *prefix) {
    for (uint32_t i = 0u; i < blockdev_count(); i++) {
        struct blockdev_info info;
        struct block_device *dev = blockdev_get(i);

        if (dev == 0 || blockdev_get_info(i, &info) != 0 || info.name == 0) {
            continue;
        }
        if (i386_name_starts_with(info.name, prefix)) {
            return dev;
        }
    }
    return 0;
}

static int i386_run_block_rw_smoke(const char *prefix,
                                   const char *read_log,
                                   const char *write_log,
                                   const char *flush_log,
                                   const char *restore_log,
                                   const char *ok_log) {
    static uint8_t original[512];
    static uint8_t pattern[512];
    static uint8_t verify[512];
    struct block_device *dev = i386_find_block_device_with_prefix(prefix);
    uint64_t lba;

    if (dev == 0 || dev->block_size != 512u || dev->block_count < 2u ||
        dev->write == 0 || dev->read == 0) {
        i386_boot_log("block: rw smoke unavailable");
        return 0;
    }
    lba = dev->block_count - 1u;
    if (blockdev_read(dev, lba, 1u, original) != 0) {
        i386_boot_log("block: rw smoke read failed");
        return 0;
    }
    i386_boot_log(read_log);
    for (uint32_t i = 0u; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)(0xa5u ^ (uint8_t)i);
    }
    if (blockdev_write(dev, lba, 1u, pattern) != 0) {
        i386_boot_log("block: rw smoke write failed");
        return 0;
    }
    i386_boot_log(write_log);
    if (blockdev_flush(dev) != 0) {
        (void)blockdev_write(dev, lba, 1u, original);
        (void)blockdev_flush(dev);
        i386_boot_log("block: rw smoke flush failed");
        return 0;
    }
    i386_boot_log(flush_log);
    if (blockdev_read(dev, lba, 1u, verify) != 0 ||
        !i386_bytes_equal(pattern, verify, sizeof(pattern))) {
        (void)blockdev_write(dev, lba, 1u, original);
        (void)blockdev_flush(dev);
        i386_boot_log("block: rw smoke write/readback failed");
        return 0;
    }
    if (blockdev_write(dev, lba, 1u, original) != 0 ||
        blockdev_flush(dev) != 0 ||
        blockdev_read(dev, lba, 1u, verify) != 0 ||
        !i386_bytes_equal(original, verify, sizeof(original))) {
        i386_boot_log("block: rw smoke restore failed");
        return 0;
    }
    i386_boot_log(restore_log);
    i386_boot_log(ok_log);
    return 1;
}

static int i386_run_ahci_rw_smoke(void) {
    return i386_run_block_rw_smoke("ahci",
                                   "ahci: read smoke OK",
                                   "ahci: write smoke OK",
                                   "ahci: flush smoke OK",
                                   "ahci: restore smoke OK",
                                   "ahci: rw smoke OK");
}

static int i386_run_usb_rw_smoke(void) {
    if (i386_run_block_rw_smoke("usbmsc",
                                "ehci: msc read smoke OK",
                                "ehci: msc write smoke OK",
                                "ehci: msc flush smoke OK",
                                "ehci: msc restore smoke OK",
                                "ehci: msc rw smoke OK")) {
        return 1;
    }
    return i386_run_block_rw_smoke("xusbmsc",
                                   "xhci: msc read smoke OK",
                                   "xhci: msc write smoke OK",
                                   "xhci: msc flush smoke OK",
                                   "xhci: msc restore smoke OK",
                                   "xhci: msc rw smoke OK");
}

static int i386_run_usb_hid_smoke(void) {
    uint32_t ehci_count;
    uint32_t xhci_count;

    for (uint32_t attempt = 0u; attempt < 128u; attempt++) {
        struct keyboard_event event;

        ehci_hotplug_scan_now();
        xhci_hotplug_scan_now();
        while (ehci_poll_keyboard_event(&event)) {
        }
        while (xhci_poll_keyboard_event(&event)) {
        }
        ehci_count = ehci_hid_keyboard_count();
        xhci_count = xhci_hid_keyboard_count();
        if (ehci_count != 0u || xhci_count != 0u) {
            if (ehci_count != 0u) {
                kprint("ehci: hid keyboard smoke OK count=%u\n",
                       ehci_count);
            }
            if (xhci_count != 0u) {
                kprint("xhci: hid keyboard smoke OK count=%u\n",
                       xhci_count);
            }
            kprint("usb: hid keyboard smoke OK ehci=%u xhci=%u\n",
                   ehci_count,
                   xhci_count);
            return 1;
        }
        for (uint32_t spin = 0u; spin < 20000u; spin++) {
            hal_cpu_relax();
        }
    }
    kprint("usb: hid keyboard smoke FAILED ehci=%u xhci=%u\n",
           ehci_hid_keyboard_count(),
           xhci_hid_keyboard_count());
    return 0;
}

static int i386_run_rtl8139_smoke(void) {
    struct rtl8139_status status;
    struct process_snapshot process;
    static const char *commands[] = {
        "/cmd/nexbox rtl8139",
        "/cmd/nexbox ifconfig",
        "/cmd/nexbox netstat",
        "/cmd/nexbox route",
    };

    memset(&status, 0, sizeof(status));
    if (!rtl8139_query_status(&status) || !status.present || !status.initialized) {
        kprint("rtl8139: tx/rx smoke unavailable present=%u init=%u\n",
               status.present,
               status.initialized);
        return 0;
    }
    if (!rtl8139_run_loopback_smoke()) {
        return 0;
    }
    for (uint32_t i = 0u; i < sizeof(commands) / sizeof(commands[0]); i++) {
        i386_boot_log("nexbox32: RUN net state command");
        kprint("nexbox32: RUN %s\n", commands[i]);
        if (!kernel_i386_run_command(commands[i], &process) ||
            process.state != PROCESS_STATE_EXITED ||
            process.exit_code != 0) {
            kprint("nexbox32: FAIL %s status=%d\n",
                   commands[i],
                   process.exit_code);
            return 0;
        }
    }
    i386_boot_log("rtl8139: command smoke OK");
    return 1;
}

static int i386_run_hda_smoke(void) {
    struct hda_status status;
    struct process_snapshot process;

    memset(&status, 0, sizeof(status));
    if (!hda_query_status(&status) || !status.present || !status.initialized) {
        kprint("hda: backend smoke unavailable present=%u init=%u\n",
               (uint32_t)status.present,
               (uint32_t)status.initialized);
        return 0;
    }
    kprint("hda: backend smoke OK bdf=%u:%u.%u codec=%x gcap=%x\n",
           (uint32_t)status.bus,
           (uint32_t)status.slot,
           (uint32_t)status.function,
           status.codec_mask,
           status.gcap);
    i386_boot_log("nexbox32: RUN /cmd/nexbox hda");
    if (!kernel_i386_run_command("/cmd/nexbox hda", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("nexbox32: FAIL /cmd/nexbox hda status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("nexbox32: PASS /cmd/nexbox hda");
    return 1;
}

static int i386_run_ac97_smoke(void) {
    struct ac97_status status;
    struct process_snapshot process;

    memset(&status, 0, sizeof(status));
    if (!ac97_query_status(&status) || !status.present || !status.initialized) {
        kprint("ac97: backend smoke unavailable present=%u init=%u\n",
               (uint32_t)status.present,
               (uint32_t)status.initialized);
        return 0;
    }
    kprint("ac97: backend smoke OK bdf=%u:%u.%u codec=%x io=%x:%x\n",
           (uint32_t)status.bus,
           (uint32_t)status.slot,
           (uint32_t)status.function,
           status.codec_id,
           status.nambar,
           status.nabmbar);
    i386_boot_log("nexbox32: RUN /cmd/nexbox ac97");
    if (!kernel_i386_run_command("/cmd/nexbox ac97", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("nexbox32: FAIL /cmd/nexbox ac97 status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("nexbox32: RUN /cmd/nexbox wav --smoke /system/audio-smoke.wav");
    if (!kernel_i386_run_command("/cmd/nexbox wav --smoke /system/audio-smoke.wav", &process) ||
        process.state != PROCESS_STATE_EXITED ||
        process.exit_code != 0) {
        kprint("nexbox32: FAIL /cmd/nexbox wav --smoke /system/audio-smoke.wav status=%d\n",
               process.exit_code);
        return 0;
    }
    i386_boot_log("nexbox32: PASS /cmd/nexbox wav --smoke /system/audio-smoke.wav");
    return 1;
}

static int i386_run_gfx_editor_smoke(void) {
    static const char *commands[] = {
        "/cmd/nexbox fb",
        "/cmd/nexbox fb --smoke",
        "/cmd/nexbox fb --blit-smoke",
        "/cmd/nexbox fb --batch-smoke",
        "/cmd/nexbox cat /proc/fb",
        "/cmd/nexbox font --utf8-check",
        "/cmd/nexbox ed --check",
        "/cmd/nexbox vi --check",
        "/cmd/nexbox vim --check",
    };
    struct boot_user_init_config config;
    struct process_snapshot process;

    i386_boot_user_init_config(&config);
    if (config.ops == 0 || config.ops->run_command == 0) {
        i386_boot_log("gfx/editor smoke unavailable");
        return 0;
    }
    for (uint32_t i = 0u; i < sizeof(commands) / sizeof(commands[0]); i++) {
        kprint("gfx/editor: RUN %s\n", commands[i]);
        if (!config.ops->run_command(commands[i], &process) ||
            process.state != PROCESS_STATE_EXITED ||
            process.exit_code != 0) {
            kprint("gfx/editor: FAIL %s status=%d\n",
                   commands[i],
                   process.exit_code);
            return 0;
        }
    }
    i386_boot_log("gfx/editor: smoke PASS");
    return 1;
}


int i386_run_backend_smoke(int ahci,
                           int usb,
                           int usb_hid,
                           int rtl8139,
                           int hda,
                           int ac97,
                           int gfx_editor) {
    if (ahci && !i386_run_ahci_rw_smoke()) {
        return 0;
    }
    if (usb && !i386_run_usb_rw_smoke()) {
        return 0;
    }
    if (usb_hid && !i386_run_usb_hid_smoke()) {
        return 0;
    }
    if (rtl8139 && !i386_run_rtl8139_smoke()) {
        return 0;
    }
    if (hda && !i386_run_hda_smoke()) {
        return 0;
    }
    if (ac97 && !i386_run_ac97_smoke()) {
        return 0;
    }
    if (gfx_editor && !i386_run_gfx_editor_smoke()) {
        return 0;
    }
    return 1;
}
