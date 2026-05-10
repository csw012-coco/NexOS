#pragma once

#include <stdint.h>
#include "bootx/bootx.h"
#include "kernel/public/sys/system_limits.h"

struct tty;
struct vfs;

enum syscall_number {
    SYS_EXIT = 0,
    SYS_OPEN = 1,
    SYS_READ = 2,
    SYS_WRITE = 3,
    SYS_CLOSE = 4,
    SYS_DUP2 = 5,
    SYS_PIPE = 6,
    SYS_CLEAR = 7,
    SYS_TICKS = 8,

    SYS_EXEC = 10,
    SYS_EXEC_REPLACE = 11,
    SYS_SPAWN = 12,
    SYS_WAIT = 13,
    SYS_KILL = 14,
    SYS_GETPID = 15,
    SYS_YIELD = 16,
    SYS_SLEEP = 17,
    SYS_PROC_QUERY = 18,
    SYS_FG = 19,
    SYS_BG = 20,

    SYS_MKDIR = 21,
    SYS_RMDIR = 22,
    SYS_REMOVE = 23,
    SYS_CHDIR = 24,
    SYS_GETCWD = 25,
    SYS_OPENDIR = 26,
    SYS_READDIR = 27,
    SYS_MOUNT = 28,
    SYS_UMOUNT = 29,

    SYS_QUERY = 30,

    SYS_PAGE_ALLOC = 40,
    SYS_PAGE_FREE = 41,

    SYS_SWITCH_ROOT = 50,
    SYS_BLOCK_READ = 51,
    SYS_BLOCK_WRITE = 52,

    SYS_AUDIO_TONE = 60,
    SYS_AUDIO_PLAY = 61,
    SYS_RTL8139_TX_TEST = 62,
    SYS_RTL8139_RX_DUMP = 63,
    SYS_RTL8139_TX_SEND = 64,
    SYS_REBOOT = 65,
    SYS_MAX = 66
};

enum syscall_read_flags {
    SYS_READ_BLOCKING = 0,
    SYS_READ_NONBLOCK = 1,
    SYS_READ_CHAR = 2
};

enum syscall_open_flags {
    SYS_OPEN_CREAT = 1u,
    SYS_OPEN_TRUNC = 2u,
    SYS_OPEN_APPEND = 4u
};

enum syscall_fd {
    SYS_FD_STDIN = 0,
    SYS_FD_STDOUT = 1,
    SYS_FD_STDERR = 2
};

enum syscall_mount_kind {
    SYS_MOUNT_AUTO = 0,
    SYS_MOUNT_FAT32 = 1,
    SYS_MOUNT_NXFS = 2
};

enum syscall_mount_result {
    SYS_MOUNT_OK = 0,
    SYS_MOUNT_ERR_BAD_ARGS = 1,
    SYS_MOUNT_ERR_INVALID_SOURCE = 2,
    SYS_MOUNT_ERR_INVALID_TARGET = 3,
    SYS_MOUNT_ERR_RESERVED_TARGET = 4,
    SYS_MOUNT_ERR_TARGET_EXISTS = 5,
    SYS_MOUNT_ERR_NO_SLOTS = 6,
    SYS_MOUNT_ERR_DISK_NOT_FOUND = 7,
    SYS_MOUNT_ERR_PARTITION_NOT_FOUND = 8,
    SYS_MOUNT_ERR_FS_DETECT = 9,
    SYS_MOUNT_ERR_UNSUPPORTED_KIND = 10,
    SYS_MOUNT_ERR_FS_MOUNT = 11,
    SYS_MOUNT_ERR_PARTITION_REQUIRED = 12,
    SYS_MOUNT_ERR_TARGET_BUSY = 13,
    SYS_MOUNT_ERR_TARGET_NOT_FOUND = 14
};

enum syscall_mount_info_kind {
    SYS_MOUNT_INFO_NONE = 0,
    SYS_MOUNT_INFO_FAT32 = 1,
    SYS_MOUNT_INFO_NXFS = 2,
    SYS_MOUNT_INFO_DEVFS = 3,
    SYS_MOUNT_INFO_PROCFS = 4,
    SYS_MOUNT_INFO_EVENTFS = 5
};

enum syscall_spawn_mode {
    SYS_SPAWN_AUTO = 0,
    SYS_SPAWN_ELF = 1
};

enum syscall_spawn_flags {
    SYS_SPAWN_BACKGROUND = 1u
};

enum syscall_proc_query_kind {
    SYS_PROC_QUERY_ALL = 0,
    SYS_PROC_QUERY_JOBS = 1,
    SYS_PROC_QUERY_LAST_EXIT = 2
};

/*
 * SYS_QUERY(kind, arg0, arg1, buffer)
 *   BOOT_INFO:         arg0=0              arg1=0          buffer=struct syscall_boot_info*
 *   MEMMAP:            arg0=index          arg1=0          buffer=struct syscall_memmap_info*
 *   PMM:               arg0=0              arg1=0          buffer=struct syscall_pmm_info*
 *   BLOCK:             arg0=index          arg1=0          buffer=struct syscall_block_info*
 *   PART:              arg0=disk_index     arg1=slot       buffer=struct syscall_partition_info*
 *   MOUNT:             arg0=index          arg1=0          buffer=struct syscall_mount_info*
 *   PROGRAM:           arg0=index          arg1=0          buffer=struct syscall_program_info*
 *   ROOT:              arg0=index          arg1=0          buffer=struct syscall_root_entry_info*
 *   ROOT_FIND:         arg0=user_name_addr arg1=0          buffer=struct syscall_root_entry_info*
 *   FAT_ROOT:          arg0=index          arg1=0          buffer=struct syscall_fat_entry_info*
 *   FAT_ROOT_FIND:     arg0=user_name_addr arg1=0          buffer=struct syscall_fat_entry_info*
 *   KMSG:              arg0=offset         arg1=0          buffer=struct syscall_kmsg_info*
 *   PCI:               arg0=0              arg1=0          buffer=struct syscall_pci_info*
 *   AC97:              arg0=0              arg1=0          buffer=struct syscall_ac97_info*
 *   RTL8139:           arg0=0              arg1=0          buffer=struct syscall_rtl8139_info*
 *   AUDIO:             arg0=index          arg1=0          buffer=struct syscall_audio_info*
 *   MACHINE_INFO:      arg0=0              arg1=0          buffer=struct syscall_machine_info*
 *   RTC:               arg0=0              arg1=0          buffer=struct syscall_rtc_info*
 */
enum syscall_query_kind {
    SYS_QUERY_BOOT_INFO = 0,
    SYS_QUERY_MEMMAP = 1,
    SYS_QUERY_PMM = 2,
    SYS_QUERY_BLOCK = 3,
    SYS_QUERY_PART = 4,
    SYS_QUERY_MOUNT = 5,
    SYS_QUERY_PROGRAM = 6,
    SYS_QUERY_ROOT = 7,
    SYS_QUERY_ROOT_FIND = 8,
    SYS_QUERY_FAT_ROOT = 9,
    SYS_QUERY_FAT_ROOT_FIND = 10,
    SYS_QUERY_KMSG = 11,
    SYS_QUERY_PCI = 12,
    SYS_QUERY_AC97 = 13,
    SYS_QUERY_RTL8139 = 14,
    SYS_QUERY_AUDIO = 15,
    SYS_QUERY_MACHINE_INFO = 16,
    SYS_QUERY_RTC = 17
};

enum syscall_process_state {
    SYS_PROC_STATE_FREE = 0,
    SYS_PROC_STATE_READY = 1,
    SYS_PROC_STATE_RUNNING = 2,
    SYS_PROC_STATE_SLEEPING = 3,
    SYS_PROC_STATE_STOPPED = 4,
    SYS_PROC_STATE_EXITED = 5,
    SYS_PROC_STATE_WAITING = 6
};

enum syscall_process_image_kind {
    SYS_PROC_IMAGE_NONE = 0,
    SYS_PROC_IMAGE_ELF = 1
};

enum {
    SYS_PROC_SLOTS_MAX = NOS_PROCESS_SLOT_MAX,
    SYS_WAIT_LAST_PID = 0xffffffffu
};

struct syscall_request {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
};

struct syscall_dirent {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t size;
    uint8_t attributes;
};

struct syscall_process_info {
    uint32_t pid;
    uint32_t slot;
    uint32_t state;
    int32_t exit_code;
    uint32_t wake_tick;
    uint32_t image_kind;
    char name[NOS_NAME_BUFFER_SIZE];
};

struct syscall_block_info {
    uint32_t index;
    uint32_t block_size;
    uint32_t partition_count;
    uint32_t writable;
    uint64_t block_count;
    char name[16];
};

struct syscall_partition_info {
    uint32_t disk_index;
    uint32_t slot;
    uint32_t part_index;
    uint64_t start_lba;
    uint64_t sector_count;
    uint32_t type;
    uint32_t bootable;
};

struct syscall_mount_info {
    uint32_t kind;
    uint32_t disk_index;
    uint32_t part_index;
    uint32_t source_known;
    char target[NOS_NAME_BUFFER_SIZE];
    uint32_t space_known;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
};

struct syscall_boot_info {
    uint32_t boot_drive;
    uint32_t partition_lba;
    uint32_t partition_sectors;
    uint32_t module_count;
};

struct syscall_memmap_info {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct syscall_pmm_info {
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t used_pages;
    uint32_t dropped_pages;
};

struct syscall_kmsg_info {
    uint32_t total_size;
    uint32_t offset;
    uint32_t bytes_copied;
    char data[160];
};

struct syscall_pci_info {
    uint32_t present;
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    uint32_t prog_if;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
};

struct syscall_ac97_info {
    uint32_t present;
    uint32_t initialized;
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    uint32_t prog_if;
    uint32_t irq_line;
    uint32_t irq_pin;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t nambar;
    uint32_t nabmbar;
    uint32_t mixer_reset;
    uint32_t powerdown;
    uint32_t ext_audio_id;
    uint32_t ext_audio_ctrl;
    uint32_t codec_id;
    uint32_t global_status;
    uint32_t global_control;
};

struct syscall_rtl8139_info {
    uint32_t present;
    uint32_t initialized;
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    uint32_t prog_if;
    uint32_t irq_line;
    uint32_t irq_pin;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t io_base;
    uint32_t pci_command;
    uint32_t chip_cmd;
    uint32_t media_status;
    uint32_t intr_mask;
    uint32_t intr_status;
    uint32_t tx_config;
    uint32_t rx_config;
    uint32_t link_up;
    uint32_t speed_mbps;
    uint8_t mac[6];
    uint8_t reserved0[2];
    uint32_t capr;
    uint32_t cbr;
    uint32_t rx_read_offset;
};

struct syscall_rtl8139_rx_info {
    uint32_t packet_status;
    uint32_t packet_length;
    uint32_t bytes_copied;
    uint8_t data[2048];
};

struct syscall_rtl8139_tx_info {
    uint32_t bytes;
    uint32_t reserved;
    uint64_t data_addr;
};

enum {
    SYS_AUDIO_CAP_PLAYBACK = 1u << 0,
    SYS_AUDIO_CAP_TONE = 1u << 1
};

enum {
    SYS_AUDIO_DRIVER_NONE = 0,
    SYS_AUDIO_DRIVER_AC97 = 1
};

struct syscall_audio_info {
    uint32_t present;
    uint32_t initialized;
    uint32_t caps;
    uint32_t driver_kind;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    char name[32];
};

struct syscall_audio_play_info {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t bytes;
    uint64_t data_addr;
};

struct syscall_rtc_info {
    uint32_t present;
    uint32_t updating;
    uint32_t valid;
    uint32_t binary_mode;
    uint32_t hour_24;
    uint32_t status_a;
    uint32_t status_b;
    uint32_t century;
    uint32_t raw_year;
    uint32_t second;
    uint32_t minute;
    uint32_t hour;
    uint32_t weekday;
    uint32_t day;
    uint32_t month;
    uint32_t year;
    uint32_t unix_time;
};

struct syscall_machine_info {
    char os_name[16];
    char kernel_name[16];
    char kernel_version[16];
    char build_date[32];
    char arch_name[16];
    char cpu_vendor[16];
    char cpu_brand[64];
    uint32_t cpuid_leaf0_eax;
    uint32_t cpuid_leaf0_ebx;
    uint32_t cpuid_leaf0_ecx;
    uint32_t cpuid_leaf0_edx;
    uint32_t cpuid_leaf1_eax;
    uint32_t cpuid_leaf1_ebx;
    uint32_t cpuid_leaf1_ecx;
    uint32_t cpuid_leaf1_edx;
    uint32_t text_columns;
    uint32_t text_rows;
};

struct syscall_block_read_info {
    uint32_t disk_index;
    uint32_t block_size;
    uint32_t bytes_read;
    uint32_t reserved;
    uint64_t lba;
    uint8_t data[512];
};

struct syscall_block_write_info {
    uint32_t disk_index;
    uint32_t block_size;
    uint32_t bytes_to_write;
    uint32_t bytes_written;
    uint64_t lba;
    uint8_t data[512];
};

struct syscall_program_info {
    char name[NOS_NAME_BUFFER_SIZE];
};

struct syscall_fat_entry_info {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t first_cluster;
    uint32_t size;
    uint32_t attributes;
};

struct syscall_root_entry_info {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t native_id;
    uint32_t size;
    uint32_t attributes;
};

struct syscall_frame {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void syscall_init(struct tty *tty,
                  volatile uint32_t *timer_ticks,
                  struct vfs *vfs,
                  const struct bootx_boot_info *boot_info,
                  const struct bootx_memmap_entry *memmap,
                  uint32_t memmap_count);
uint64_t syscall_dispatch(struct syscall_frame *frame);
