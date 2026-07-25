#include <nexos/audio.h>
#include <nexos/fs.h>
#include <nexos/gfx.h>
#include <nexos/net.h>

#include "test32_query.h"

int test32_query_syscalls_case(void) {
    struct syscall_tty_info tty_info;
    struct syscall_fd_info fd_info;
    struct syscall_machine_info machine_info;
    struct syscall_memmap_info memmap_info;
    struct syscall_pmm_info pmm_info;
    struct syscall_block_info block_info;
    struct syscall_partition_info part_info;
    struct syscall_mount_info mount_info;

    if (tty_query(STDOUT_FILENO, &tty_info) <= 0 ||
        tty_info.kind != SYS_TTY_KIND_VIRTUAL ||
        tty_info.active != 1u ||
        strcmp(tty_info.path, "/dev/tty0") != 0 ||
        tty_query(9u, &tty_info) != 0) {
        return 66;
    }
    if (fd_query(STDOUT_FILENO, &fd_info) <= 0 ||
        fd_info.fd != STDOUT_FILENO ||
        fd_info.kind != SYS_FD_KIND_TTY_STDOUT ||
        fd_info.writable != 1u ||
        fd_info.readable != 0u ||
        fd_query(15u, &fd_info) != 0) {
        return 70;
    }
    if (machine_info_query(&machine_info) <= 0 ||
        strcmp(machine_info.os_name, "NexOS") != 0 ||
        strcmp(machine_info.arch_name, "i386") != 0 ||
        machine_info.text_columns < 80u ||
        machine_info.text_rows < 25u ||
        machine_info.text_cell_width != 8u ||
        machine_info.text_cell_height == 0u) {
        return 67;
    }
    if (memmap_query(0u, &memmap_info) <= 0 ||
        memmap_info.length == 0u ||
        pmm_query(&pmm_info) <= 0 ||
        pmm_info.total_pages == 0u ||
        pmm_info.free_pages == 0u) {
        return 68;
    }
    if (block_query(0u, &block_info) <= 0 ||
        block_info.index != 0u ||
        block_info.block_size != 512u ||
        block_info.partition_count == 0u ||
        block_query(1u, &block_info) <= 0 ||
        block_info.index != 1u ||
        block_info.block_size != 512u ||
        block_info.partition_count == 0u ||
        part_query(0u, 0u, &part_info) <= 0 ||
        part_info.disk_index != 0u ||
        part_info.part_index != 0u ||
        part_query(1u, 0u, &part_info) <= 0 ||
        part_info.disk_index != 1u ||
        part_info.part_index != 0u) {
        return 81;
    }
    if (mount_query(0u, &mount_info) <= 0 ||
        mount_info.kind != SYS_MOUNT_INFO_FAT32 ||
        strcmp(mount_info.target, "boot") != 0 ||
        mount_info.space_known != 1u ||
        mount_info.block_size == 0u) {
        return 82;
    }
    if (puts("[test32] libc32 query syscalls OK") == EOF) {
        return 69;
    }
    return 0;
}

int test32_query_backend_case(void) {
    struct syscall_block_read_info block_read_info;
    struct syscall_block_write_info block_write_info;
    struct syscall_gfx_info gfx_info_data;
    struct syscall_audio_info audio_info;
    struct syscall_rtl8139_info rtl_info;

    memset(&block_read_info, 0, sizeof(block_read_info));
    if (block_read(0u, 0u, &block_read_info) != 1 ||
        block_read_info.disk_index != 0u ||
        block_read_info.block_size != 512u ||
        block_read_info.bytes_read != 512u) {
        return 107;
    }
    memset(&block_write_info, 0, sizeof(block_write_info));
    block_write_info.bytes_to_write = 1u;
    block_write_info.data[0] = block_read_info.data[0];
    if (block_write(0u, 0u, &block_write_info) != 0 ||
        block_write_info.bytes_written != 0u ||
        block_flush(0u) != 1 ||
        block_flush(0xffffffffu) != 0) {
        return 146;
    }
    memset(&gfx_info_data, 0, sizeof(gfx_info_data));
    if (gfx_info(&gfx_info_data) != 0 ||
        gfx_info_data.width < 80u ||
        gfx_info_data.height < 25u ||
        gfx_info_data.bpp == 0u) {
        return 108;
    }
    memset(&audio_info, 0xa5, sizeof(audio_info));
    if (audio_query(0u, &audio_info) < 0 ||
        (audio_info.present == 0u &&
         (audio_info.initialized != 0u ||
          audio_info.caps != 0u ||
          audio_info.driver_kind != SYS_AUDIO_DRIVER_NONE))) {
        return 109;
    }
    memset(&rtl_info, 0xa5, sizeof(rtl_info));
    if (rtl8139_query(&rtl_info) < 0 ||
        (rtl_info.present == 0u &&
         (rtl_info.initialized != 0u ||
          rtl_info.vendor_id != 0u ||
          rtl_info.device_id != 0u))) {
        return 110;
    }
    if (puts("[test32] libc32 gfx/audio/net/block backend OK") == EOF) {
        return 111;
    }
    return 0;
}
