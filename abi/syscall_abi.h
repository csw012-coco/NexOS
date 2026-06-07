#pragma once

#include <stdint.h>

enum {
    NOS_PAGE_SIZE = 4096u,
    NOS_KERNEL_STACK_SIZE = 16384u,
    NOS_TTY_LINE_MAX = 255u,
    NOS_TTY_LINE_BUFFER_SIZE = NOS_TTY_LINE_MAX + 1u,
    NOS_PATH_MAX = 255u,
    NOS_PATH_BUFFER_SIZE = NOS_PATH_MAX + 1u,
    NOS_NAME_MAX = 255u,
    NOS_NAME_BUFFER_SIZE = NOS_NAME_MAX + 1u,
    NOS_MOUNT_SLOT_MAX = 4u,
    NOS_PROCESS_SLOT_MAX = 8u,
    NOS_PROCESS_FILE_MAX = 16u,
    NOS_ELF_FILE_BUFFER_SIZE = 524288u,
    NOS_USER_STACK_SIZE = NOS_PAGE_SIZE
};

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
    SYS_AUDIO_PLAY_FD = 70,
    SYS_RTL8139_TX_TEST = 62,
    SYS_RTL8139_RX_DUMP = 63,
    SYS_RTL8139_TX_SEND = 64,
    SYS_REBOOT = 65,
    SYS_CAPABILITY_EVENT = 66,
    SYS_GFX = 67,
    SYS_GUI_EVENT = 68,
    SYS_CLIPBOARD = 69,
    SYS_MAX = 71
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

enum syscall_capability_decision {
    SYS_CAP_DECISION_ALLOW = 1,
    SYS_CAP_DECISION_DENY = 2
};

enum syscall_capability_reason {
    SYS_CAP_REASON_MASK = 1,
    SYS_CAP_REASON_ALLOW_ACTION = 2,
    SYS_CAP_REASON_DENY_ACTION = 3
};

struct syscall_capability_event {
    char source[32];
    char action[32];
    char caps[128];
    uint32_t required;
    uint32_t allowed;
    uint32_t missing;
    uint32_t decision;
    uint32_t reason;
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
    SYS_QUERY_RTC = 17,
    SYS_QUERY_HDA = 18,
    SYS_QUERY_TTY = 19
};

enum syscall_tty_kind {
    SYS_TTY_KIND_NONE = 0,
    SYS_TTY_KIND_VIRTUAL = 1,
    SYS_TTY_KIND_SERIAL = 2
};

struct syscall_tty_info {
    uint32_t kind;
    uint32_t index;
    uint32_t active;
    char path[32];
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

struct syscall_request {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
};

enum syscall_gfx_op {
    SYS_GFX_INFO = 0,
    SYS_GFX_CLEAR = 1,
    SYS_GFX_PIXEL = 2,
    SYS_GFX_LINE = 3,
    SYS_GFX_RECT = 4,
    SYS_GFX_FILL_RECT = 5,
    SYS_GFX_TRIANGLE = 6,
    SYS_GFX_FILL_TRIANGLE = 7,
    SYS_GFX_CIRCLE = 8,
    SYS_GFX_FILL_CIRCLE = 9,
    SYS_GFX_PRESENT = 10,
    SYS_GFX_BATCH = 11
};

enum syscall_gfx_batch_flags {
    SYS_GFX_BATCH_PRESENT = 1u << 0,
    SYS_GFX_BATCH_MAX_COMMANDS = 256
};

struct syscall_gfx_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t text_columns;
    uint32_t text_rows;
};

struct syscall_gfx_command {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    uint32_t width;
    uint32_t height;
    uint32_t radius;
    uint32_t rgb;
};

struct syscall_gfx_batch_entry {
    uint32_t op;
    uint32_t reserved;
    struct syscall_gfx_command command;
};

struct syscall_gfx_batch {
    uint64_t entries_addr;
    uint32_t count;
    uint32_t flags;
};

enum syscall_clipboard_op {
    SYS_CLIPBOARD_GET = 0,
    SYS_CLIPBOARD_SET = 1,
    SYS_CLIPBOARD_CLEAR = 2,
    SYS_CLIPBOARD_SIZE = 3
};

struct syscall_clipboard_transfer {
    uint64_t data_addr;
    uint32_t bytes;
    uint32_t size;
};

enum syscall_gui_event_op {
    SYS_GUI_EVENT_CURSOR_INIT = 0,
    SYS_GUI_EVENT_POLL = 1
};

enum syscall_gui_event_type {
    SYS_GUI_EVENT_NONE = 0,
    SYS_GUI_EVENT_KEY = 1,
    SYS_GUI_EVENT_MOUSE = 2
};

enum syscall_gui_event_result {
    SYS_GUI_EVENT_EMPTY = 0,
    SYS_GUI_EVENT_READY = 1
};

enum syscall_gui_keycode {
    SYS_KEY_NONE = 0,
    SYS_KEY_ESC,
    SYS_KEY_TAB,
    SYS_KEY_ENTER,
    SYS_KEY_BACKSPACE,
    SYS_KEY_SPACE,
    SYS_KEY_HOME,
    SYS_KEY_END,
    SYS_KEY_DELETE,
    SYS_KEY_PAGE_UP,
    SYS_KEY_PAGE_DOWN,
    SYS_KEY_UP,
    SYS_KEY_DOWN,
    SYS_KEY_LEFT,
    SYS_KEY_RIGHT,
    SYS_KEY_LEFT_SHIFT,
    SYS_KEY_RIGHT_SHIFT,
    SYS_KEY_LEFT_CTRL,
    SYS_KEY_RIGHT_CTRL,
    SYS_KEY_CAPS_LOCK,
    SYS_KEY_NUM_LOCK,
    SYS_KEY_SCROLL_LOCK,
    SYS_KEY_A,
    SYS_KEY_B,
    SYS_KEY_C,
    SYS_KEY_D,
    SYS_KEY_E,
    SYS_KEY_F,
    SYS_KEY_G,
    SYS_KEY_H,
    SYS_KEY_I,
    SYS_KEY_J,
    SYS_KEY_K,
    SYS_KEY_L,
    SYS_KEY_M,
    SYS_KEY_N,
    SYS_KEY_O,
    SYS_KEY_P,
    SYS_KEY_Q,
    SYS_KEY_R,
    SYS_KEY_S,
    SYS_KEY_T,
    SYS_KEY_U,
    SYS_KEY_V,
    SYS_KEY_W,
    SYS_KEY_X,
    SYS_KEY_Y,
    SYS_KEY_Z,
    SYS_KEY_0,
    SYS_KEY_1,
    SYS_KEY_2,
    SYS_KEY_3,
    SYS_KEY_4,
    SYS_KEY_5,
    SYS_KEY_6,
    SYS_KEY_7,
    SYS_KEY_8,
    SYS_KEY_9,
    SYS_KEY_MINUS,
    SYS_KEY_EQUAL,
    SYS_KEY_LEFT_BRACKET,
    SYS_KEY_RIGHT_BRACKET,
    SYS_KEY_BACKSLASH,
    SYS_KEY_SEMICOLON,
    SYS_KEY_APOSTROPHE,
    SYS_KEY_GRAVE,
    SYS_KEY_COMMA,
    SYS_KEY_PERIOD,
    SYS_KEY_SLASH
};

enum syscall_gui_mouse_buttons {
    SYS_GUI_MOUSE_LEFT = 1u,
    SYS_GUI_MOUSE_RIGHT = 2u,
    SYS_GUI_MOUSE_MIDDLE = 4u
};

struct syscall_gui_event_cursor {
    uint32_t keyboard_seq;
    uint32_t mouse_seq;
};

struct syscall_gui_event {
    uint32_t type;
    uint32_t seq;
    uint32_t tick;
    int32_t dx;
    int32_t dy;
    uint32_t buttons;
    uint32_t keycode;
    char ascii;
    uint8_t pressed;
    uint8_t released;
    uint8_t shift;
    uint8_t ctrl;
};

struct syscall_gui_event_poll {
    struct syscall_gui_event_cursor cursor;
    struct syscall_gui_event event;
    uint32_t keyboard_dropped;
    uint32_t mouse_dropped;
};

enum {
    SYS_PROC_SLOTS_MAX = NOS_PROCESS_SLOT_MAX,
    SYS_WAIT_LAST_PID = 0xffffffffu
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
    uint32_t class_code;
    uint32_t subclass;
    uint32_t prog_if;
    uint32_t irq_line;
    uint32_t irq_pin;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
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

struct syscall_hda_info {
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
    uint32_t mmio_base_lo;
    uint32_t mmio_base_hi;
    uint32_t pci_command;
    uint32_t gcap;
    uint32_t vmaj;
    uint32_t vmin;
    uint32_t outpay;
    uint32_t inpay;
    uint32_t gctl;
    uint32_t statests;
    uint32_t wakeen;
    uint32_t corb_size;
    uint32_t rirb_size;
    uint32_t codec_mask;
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
    SYS_AUDIO_CAP_TONE = 1u << 1,
    SYS_AUDIO_CAP_STREAM = 1u << 2
};

enum {
    SYS_AUDIO_DRIVER_NONE = 0,
    SYS_AUDIO_DRIVER_AC97 = 1,
    SYS_AUDIO_DRIVER_HDA = 2
};

enum {
    SYS_AUDIO_PLAY_F_ASYNC = 1u << 0
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
    uint32_t flags;
    uint32_t reserved;
};

struct syscall_audio_stream_info {
    uint32_t fd;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t data_bytes;
    uint32_t flags;
    uint32_t reserved;
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
    uint32_t text_cell_width;
    uint32_t text_cell_height;
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
