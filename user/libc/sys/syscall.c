#include "user/libc/include/nlibc.h"
#include "user/libc/sys/syscall.h"
#include "user/libc/include/nexos/file.h"

uint64_t syscall4(uint64_t number,
                  uint64_t arg0,
                  uint64_t arg1,
                  uint64_t arg2,
                  uint64_t arg3) {
    return syscall4_raw(number, arg0, arg1, arg2, arg3);
}

ssize_t write(int fd, const void *data, size_t len) {
    return (ssize_t)syscall4(SYS_WRITE, (uint64_t)fd, (uint64_t)(uintptr_t)data, (uint64_t)len, 0);
}

ssize_t write_stdout(const void *data, size_t len) {
    return write(STDOUT_FILENO, data, len);
}

ssize_t write_stderr(const void *data, size_t len) {
    return write(STDERR_FILENO, data, len);
}

uint32_t write_fd(uint32_t fd, const char *data, uint32_t len) {
    return (uint32_t)write((int)fd, data, len);
}

uint32_t write_str(const char *text) {
    return (uint32_t)write_stdout(text, strlen(text));
}

uint32_t write_err(const char *data, uint32_t len) {
    return (uint32_t)write_stderr(data, len);
}

uint32_t write_err_str(const char *text) {
    return write_err(text, strlen(text));
}

ssize_t nex_read(int fd, void *data, size_t max_len, uint32_t flags) {
    return (ssize_t)syscall4(SYS_READ, (uint64_t)fd, (uint64_t)(uintptr_t)data, (uint64_t)max_len, flags);
}

ssize_t read(int fd, void *data, size_t max_len) {
    return nex_read(fd, data, max_len, SYS_READ_BLOCKING);
}

uint32_t read_char_nonblock(char *ch) {
    return (uint32_t)nex_read(STDIN_FILENO, ch, 1u, SYS_READ_NONBLOCK | SYS_READ_CHAR);
}

int open(const char *name, int flags) {
    return (int)syscall4(SYS_OPEN, (uint64_t)(uintptr_t)name, flags, 0, 0);
}

int opendir(const char *path) {
    return (int)syscall4(SYS_OPENDIR, (uint64_t)(uintptr_t)path, 0, 0, 0);
}

int readdir(uint32_t fd, struct syscall_dirent *entry) {
    return (int)syscall4(SYS_READDIR, fd, (uint64_t)(uintptr_t)entry, 0, 0);
}

int close(int fd) {
    return (int)syscall4(SYS_CLOSE, (uint64_t)fd, 0, 0, 0);
}

int mkdir(const char *path) {
    return (int)syscall4(SYS_MKDIR, (uint64_t)(uintptr_t)path, 0, 0, 0);
}

int rmdir(const char *path) {
    return (int)syscall4(SYS_RMDIR, (uint64_t)(uintptr_t)path, 0, 0, 0);
}

int remove(const char *path) {
    return (int)syscall4(SYS_REMOVE, (uint64_t)(uintptr_t)path, 0, 0, 0);
}

int chdir(const char *path) {
    return (int)syscall4(SYS_CHDIR, (uint64_t)(uintptr_t)path, 0, 0, 0);
}

int getcwd(char *buffer, uint32_t size) {
    return (int)syscall4(SYS_GETCWD, (uint64_t)(uintptr_t)buffer, size, 0, 0);
}

int mount(const char *source, const char *target, uint32_t kind) {
    return (int)syscall4(SYS_MOUNT,
                         (uint64_t)(uintptr_t)source,
                         (uint64_t)(uintptr_t)target,
                         kind,
                         0);
}

int umount(const char *target) {
    return (int)syscall4(SYS_UMOUNT, (uint64_t)(uintptr_t)target, 0, 0, 0);
}

int switch_root(const char *target) {
    return (int)syscall4(SYS_SWITCH_ROOT, (uint64_t)(uintptr_t)target, 0, 0, 0);
}

int spawn(const char *name, uint32_t mode, uint32_t flags) {
    int rc = (int)syscall4(SYS_SPAWN, (uint64_t)(uintptr_t)name, mode, flags, (uint64_t)(uintptr_t)environ);

    return rc < 0 ? rc : 0;
}

int exec_replace(const char *name) {
    int rc = (int)syscall4(SYS_EXEC_REPLACE, (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)environ, 0, 0);

    return rc < 0 ? rc : 0;
}

int sys_query(uint32_t kind, uint64_t arg0, uint64_t arg1, void *buffer) {
    return (int)syscall4(SYS_QUERY, kind, arg0, arg1, (uint64_t)(uintptr_t)buffer);
}

int block_query(uint32_t index, struct syscall_block_info *info) {
    return sys_query(SYS_QUERY_BLOCK, index, 0, info);
}

int part_query(uint32_t disk_index, uint32_t slot, struct syscall_partition_info *info) {
    return sys_query(SYS_QUERY_PART, disk_index, slot, info);
}

int mount_query(uint32_t index, struct syscall_mount_info *info) {
    return sys_query(SYS_QUERY_MOUNT, index, 0, info);
}

int boot_info_query(struct syscall_boot_info *info) {
    return sys_query(SYS_QUERY_BOOT_INFO, 0, 0, info);
}

int memmap_query(uint32_t index, struct syscall_memmap_info *info) {
    return sys_query(SYS_QUERY_MEMMAP, index, 0, info);
}

int pmm_query(struct syscall_pmm_info *info) {
    return sys_query(SYS_QUERY_PMM, 0, 0, info);
}

int block_read(uint32_t disk_index, uint64_t lba, struct syscall_block_read_info *info) {
    return (int)syscall4(SYS_BLOCK_READ, disk_index, lba, (uint64_t)(uintptr_t)info, 0);
}

int block_write(uint32_t disk_index, uint64_t lba, struct syscall_block_write_info *info) {
    return (int)syscall4(SYS_BLOCK_WRITE, disk_index, lba, (uint64_t)(uintptr_t)info, 0);
}

int program_query(uint32_t index, struct syscall_program_info *info) {
    return sys_query(SYS_QUERY_PROGRAM, index, 0, info);
}

int root_query(uint32_t index, struct syscall_root_entry_info *info) {
    return sys_query(SYS_QUERY_ROOT, index, 0, info);
}

int root_find(const char *name, struct syscall_root_entry_info *info) {
    return sys_query(SYS_QUERY_ROOT_FIND, (uint64_t)(uintptr_t)name, 0, info);
}

int fat_root_query(uint32_t index, struct syscall_fat_entry_info *info) {
    return sys_query(SYS_QUERY_FAT_ROOT, index, 0, info);
}

int fat_root_find(const char *name, struct syscall_fat_entry_info *info) {
    return sys_query(SYS_QUERY_FAT_ROOT_FIND, (uint64_t)(uintptr_t)name, 0, info);
}

int kmsg_query(uint32_t offset, struct syscall_kmsg_info *info) {
    return sys_query(SYS_QUERY_KMSG, offset, 0, info);
}

int pci_query(struct syscall_pci_info *info) {
    return sys_query(SYS_QUERY_PCI, 0, 0, info);
}

int ac97_query(struct syscall_ac97_info *info) {
    return sys_query(SYS_QUERY_AC97, 0, 0, info);
}

int hda_query(struct syscall_hda_info *info) {
    return sys_query(SYS_QUERY_HDA, 0, 0, info);
}

int rtl8139_query(struct syscall_rtl8139_info *info) {
    return sys_query(SYS_QUERY_RTL8139, 0, 0, info);
}

int rtl8139_tx_test(void) {
    return (int)syscall4(SYS_RTL8139_TX_TEST, 0, 0, 0, 0);
}

int rtl8139_rx_dump(struct syscall_rtl8139_rx_info *info) {
    return (int)syscall4(SYS_RTL8139_RX_DUMP, (uint64_t)(uintptr_t)info, 0, 0, 0);
}

int rtl8139_tx_send(const void *data, uint32_t bytes) {
    struct syscall_rtl8139_tx_info info;

    info.bytes = bytes;
    info.reserved = 0u;
    info.data_addr = (uint64_t)(uintptr_t)data;
    return (int)syscall4(SYS_RTL8139_TX_SEND, (uint64_t)(uintptr_t)&info, 0, 0, 0);
}

int audio_query(uint32_t index, struct syscall_audio_info *info) {
    return sys_query(SYS_QUERY_AUDIO, index, 0, info);
}

int audio_tone(uint32_t index, uint32_t hz, uint32_t duration_ms) {
    return (int)syscall4(SYS_AUDIO_TONE, index, hz, duration_ms, 0);
}

int audio_play(uint32_t index, const struct syscall_audio_play_info *info) {
    return (int)syscall4(SYS_AUDIO_PLAY, index, (uint64_t)(uintptr_t)info, 0, 0);
}

int machine_info_query(struct syscall_machine_info *info) {
    return sys_query(SYS_QUERY_MACHINE_INFO, 0, 0, info);
}

int rtc_query(struct syscall_rtc_info *info) {
    return sys_query(SYS_QUERY_RTC, 0, 0, info);
}

int tty_query(uint32_t fd, struct syscall_tty_info *info) {
    return sys_query(SYS_QUERY_TTY, fd, 0, info);
}

int exec(const char *name) {
    int rc = (int)syscall4(SYS_EXEC, (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)environ, 0, 0);

    return rc < 0 ? rc : 0;
}

int proc_query(uint32_t kind, uint32_t index, struct syscall_process_info *info) {
    return (int)syscall4(SYS_PROC_QUERY, kind, index, (uint64_t)(uintptr_t)info, 0);
}

int wait(uint32_t pid, struct syscall_process_info *info) {
    return (int)syscall4(SYS_WAIT, pid, (uint64_t)(uintptr_t)info, 0, 0);
}

int kill(uint32_t pid) {
    return (int)syscall4(SYS_KILL, pid, 0, 0, 0);
}

pid_t getpid(void) {
    return (pid_t)syscall4(SYS_GETPID, 0, 0, 0, 0);
}

int fg(uint32_t pid) {
    return (int)syscall4(SYS_FG, pid, 0, 0, 0);
}

int bg(uint32_t pid) {
    return (int)syscall4(SYS_BG, pid, 0, 0, 0);
}

int dup2(int src_fd, int dst_fd) {
    return (int)syscall4(SYS_DUP2, (uint64_t)src_fd, (uint64_t)dst_fd, 0, 0);
}

int pipe(int pair[2]) {
    return (int)syscall4(SYS_PIPE, (uint64_t)(uintptr_t)pair, 0, 0, 0);
}

uint32_t ticks(void) {
    return (uint32_t)syscall4(SYS_TICKS, 0, 0, 0, 0);
}

uint64_t page_alloc(void) {
    return syscall4(SYS_PAGE_ALLOC, 0, 0, 0, 0);
}

int page_free(uint64_t user_page_addr) {
    return (int)syscall4(SYS_PAGE_FREE, user_page_addr, 0, 0, 0);
}

void yield(void) {
    (void)syscall4(SYS_YIELD, 0, 0, 0, 0);
}

void sleep(uint32_t tick_count) {
    (void)syscall4(SYS_SLEEP, tick_count, 0, 0, 0);
}

int reboot(void) {
    return (int)syscall4(SYS_REBOOT, 0, 0, 0, 0);
}

int capability_event(const struct syscall_capability_event *event) {
    return (int)syscall4(SYS_CAPABILITY_EVENT, (uint64_t)(uintptr_t)event, 0, 0, 0);
}

int clipboard_get(char *buffer, uint32_t size) {
    struct syscall_clipboard_transfer transfer;
    int rc;

    transfer.data_addr = (uint64_t)(uintptr_t)buffer;
    transfer.bytes = size;
    transfer.size = 0u;
    rc = (int)syscall4(SYS_CLIPBOARD, SYS_CLIPBOARD_GET, (uint64_t)(uintptr_t)&transfer, 0, 0);
    if (rc >= 0 && buffer != 0 && size > 0u && (uint32_t)rc < size) {
        buffer[(uint32_t)rc] = '\0';
    }
    return rc;
}

int clipboard_set(const char *text, uint32_t len) {
    struct syscall_clipboard_transfer transfer;

    transfer.data_addr = (uint64_t)(uintptr_t)text;
    transfer.bytes = len;
    transfer.size = 0u;
    return (int)syscall4(SYS_CLIPBOARD, SYS_CLIPBOARD_SET, (uint64_t)(uintptr_t)&transfer, 0, 0);
}

int clipboard_clear(void) {
    return (int)syscall4(SYS_CLIPBOARD, SYS_CLIPBOARD_CLEAR, 0, 0, 0);
}

int clipboard_size(void) {
    struct syscall_clipboard_transfer transfer;

    transfer.data_addr = 0u;
    transfer.bytes = 0u;
    transfer.size = 0u;
    return (int)syscall4(SYS_CLIPBOARD, SYS_CLIPBOARD_SIZE, (uint64_t)(uintptr_t)&transfer, 0, 0);
}

static int gfx_command(uint32_t op, const struct syscall_gfx_command *cmd) {
    return (int)syscall4(SYS_GFX, op, (uint64_t)(uintptr_t)cmd, 0, 0);
}

int gfx_info(struct syscall_gfx_info *info) {
    return (int)syscall4(SYS_GFX, SYS_GFX_INFO, (uint64_t)(uintptr_t)info, 0, 0);
}

int gfx_clear(uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_CLEAR, &cmd);
}

int gfx_draw_pixel(int32_t x, int32_t y, uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = x;
    cmd.y0 = y;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_PIXEL, &cmd);
}

int gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = x0;
    cmd.y0 = y0;
    cmd.x1 = x1;
    cmd.y1 = y1;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_LINE, &cmd);
}

int gfx_draw_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = x;
    cmd.y0 = y;
    cmd.width = width;
    cmd.height = height;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_RECT, &cmd);
}

int gfx_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = x;
    cmd.y0 = y;
    cmd.width = width;
    cmd.height = height;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_FILL_RECT, &cmd);
}

int gfx_draw_triangle(int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      int32_t x2,
                      int32_t y2,
                      uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = x0;
    cmd.y0 = y0;
    cmd.x1 = x1;
    cmd.y1 = y1;
    cmd.x2 = x2;
    cmd.y2 = y2;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_TRIANGLE, &cmd);
}

int gfx_fill_triangle(int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      int32_t x2,
                      int32_t y2,
                      uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = x0;
    cmd.y0 = y0;
    cmd.x1 = x1;
    cmd.y1 = y1;
    cmd.x2 = x2;
    cmd.y2 = y2;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_FILL_TRIANGLE, &cmd);
}

int gfx_draw_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = cx;
    cmd.y0 = cy;
    cmd.radius = radius;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_CIRCLE, &cmd);
}

int gfx_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    struct syscall_gfx_command cmd = {0};

    cmd.x0 = cx;
    cmd.y0 = cy;
    cmd.radius = radius;
    cmd.rgb = rgb;
    return gfx_command(SYS_GFX_FILL_CIRCLE, &cmd);
}

int gui_event_cursor_init(struct syscall_gui_event_cursor *cursor) {
    if (cursor == 0) {
        return -1;
    }
    return (int)syscall4(SYS_GUI_EVENT, SYS_GUI_EVENT_CURSOR_INIT, (uint64_t)(uintptr_t)cursor, 0, 0);
}

int gui_poll_event_with_cursor(struct syscall_gui_event_cursor *cursor, struct syscall_gui_event *event) {
    struct syscall_gui_event_poll poll;
    int rc;

    if (cursor == 0 || event == 0) {
        return -1;
    }
    poll.cursor = *cursor;
    poll.event.type = SYS_GUI_EVENT_NONE;
    poll.event.seq = 0u;
    poll.event.tick = 0u;
    poll.event.dx = 0;
    poll.event.dy = 0;
    poll.event.buttons = 0u;
    poll.event.keycode = 0u;
    poll.event.ascii = 0;
    poll.event.pressed = 0u;
    poll.event.released = 0u;
    poll.event.shift = 0u;
    poll.event.ctrl = 0u;
    poll.keyboard_dropped = 0u;
    poll.mouse_dropped = 0u;

    rc = (int)syscall4(SYS_GUI_EVENT, SYS_GUI_EVENT_POLL, (uint64_t)(uintptr_t)&poll, 0, 0);
    if (rc < 0) {
        return rc;
    }
    *cursor = poll.cursor;
    *event = poll.event;
    return rc;
}

int gui_poll_event(struct syscall_gui_event *event) {
    static struct syscall_gui_event_cursor cursor;
    static int initialized;

    if (event == 0) {
        return -1;
    }
    if (!initialized) {
        if (gui_event_cursor_init(&cursor) != 0) {
            return -1;
        }
        initialized = 1;
    }
    return gui_poll_event_with_cursor(&cursor, event);
}

void exit_with_code(uint64_t code) {
    (void)syscall4(SYS_EXIT, code, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void exit(void) {
    exit_with_code(0);
}
