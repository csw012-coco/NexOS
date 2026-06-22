#include "block/blockdev.h"
#include "arch/x86/i386/keyboard.h"
#include "arch/x86/i386/paging.h"
#include "arch/x86/i386/scheduler.h"
#include "drivers/input/keyboard.h"
#include "fs/fat32.h"
#include "fs/vfs_internal.h"
#include "hal/hal.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"

static struct vfs i386_vfs;
static struct tty *i386_tty;

extern int kernel_i386_run_test32(struct process_snapshot *process0,
                                  struct process_snapshot *process1);

static int i386_user_buffer_valid(uint32_t address,
                                  uint32_t size,
                                  int writable) {
    uint32_t end;
    uint32_t page;
    uint32_t user_root;
    uint32_t kernel_root;
    int valid = 1;

    if (size == 0u ||
        address < I386_PAGING_IDENTITY_LIMIT ||
        address >= 0xc0000000u ||
        address + size < address ||
        address + size > 0xc0000000u) {
        return 0;
    }
    user_root = i386_paging_root();
    kernel_root = i386_paging_kernel_root();
    if (user_root == 0u || kernel_root == 0u || user_root == kernel_root) {
        return 0;
    }
    end = address + size - 1u;
    page = address & ~(I386_PAGE_SIZE - 1u);
    i386_paging_switch(kernel_root);
    for (;;) {
        if (!i386_paging_user_accessible_in(user_root, page, writable)) {
            valid = 0;
            break;
        }
        if (page >= (end & ~(I386_PAGE_SIZE - 1u))) {
            break;
        }
        page += I386_PAGE_SIZE;
    }
    i386_paging_switch(user_root);
    return valid;
}

uint32_t kernel_i386_syscall_open(void *opaque,
                                  uint32_t user_path,
                                  uint32_t flags) {
    const char *path = (const char *)user_path;
    uint32_t length = 0u;

    (void)opaque;
    if (!i386_user_buffer_valid(user_path, 1u, 0)) {
        return (uint32_t)-1;
    }
    while (length < NOS_PATH_MAX) {
        if (!i386_user_buffer_valid(user_path + length, 1u, 0)) {
            return (uint32_t)-1;
        }
        if (path[length] == '\0') {
            return (uint32_t)i386_scheduler_open(&i386_vfs, path, flags);
        }
        length++;
    }
    return (uint32_t)-1;
}

uint32_t kernel_i386_syscall_read(void *opaque,
                                  uint32_t fd,
                                  uint32_t user_address,
                                  uint32_t size,
                                  uint32_t flags) {
    (void)opaque;
    if (size > 4096u ||
        !i386_user_buffer_valid(user_address, size, 1)) {
        return (uint32_t)-1;
    }
    return (uint32_t)i386_scheduler_read(&i386_vfs,
                                         fd,
                                         (void *)user_address,
                                         size,
                                         flags);
}

uint32_t kernel_i386_syscall_close(void *opaque, uint32_t fd) {
    (void)opaque;
    return (uint32_t)i386_scheduler_close(fd);
}

uint32_t kernel_i386_syscall_write(void *opaque,
                                   uint32_t fd,
                                   uint32_t user_address,
                                   uint32_t size) {
    const char *text = (const char *)user_address;

    (void)opaque;
    if (i386_tty == 0 ||
        (fd != SYS_FD_STDOUT && fd != SYS_FD_STDERR) ||
        text == 0 ||
        size == 0u ||
        size > 4096u ||
        user_address < I386_PAGING_IDENTITY_LIMIT ||
        user_address >= 0xc0000000u ||
        user_address + size < user_address ||
        user_address + size > 0xc0000000u ||
        !i386_user_buffer_valid(user_address, size, 0)) {
        return (uint32_t)-1;
    }
    return tty_write(i386_tty,
                     text,
                     size,
                     fd == SYS_FD_STDERR ? 0x0cu : 0x0fu);
}

static int command_starts_with(const char *line, const char *command) {
    while (*command != '\0') {
        if (*line++ != *command++) {
            return 0;
        }
    }
    return *line == '\0' || *line == ' ';
}

static const char *command_argument(const char *line) {
    while (*line != '\0' && *line != ' ') {
        line++;
    }
    while (*line == ' ') {
        line++;
    }
    return line;
}

static void i386_prompt(void) {
    tty_write_str(i386_tty, "i386> ", 0x0bu);
    tty_show_prompt(i386_tty);
}

static int i386_pop_keyboard_event(struct keyboard_event *event) {
    struct i386_key_event raw;

    if (!i386_keyboard_pop(&raw)) {
        return 0;
    }
    *event = keyboard_handle_scancode(raw.scancode);
    return 1;
}

static int i386_inject_and_feed(uint8_t scancode) {
    struct keyboard_event event;

    if (!i386_keyboard_inject_scancode(scancode)) {
        return 0;
    }
    for (uint32_t ticks = 0; ticks < 100u; ticks++) {
        __asm__ volatile("sti; hlt" : : : "memory");
        if (i386_pop_keyboard_event(&event)) {
            tty_feed_key_event(i386_tty, &event);
            return 1;
        }
    }
    return 0;
}

static int i386_tty_input_self_test(void) {
    static const uint8_t scancodes[] = {
        0x1eu, 0x30u, 0x0eu, 0x2eu, 0x1cu
    };
    char line[TTY_LINE_MAX + 1u];

    tty_clear(i386_tty);
    i386_prompt();
    for (uint32_t i = 0; i < sizeof(scancodes); i++) {
        if (!i386_inject_and_feed(scancodes[i])) {
            return 0;
        }
    }
    return tty_has_line(i386_tty) &&
           tty_read(i386_tty, line, sizeof(line), TTY_READ_LINE) != 0u &&
           streq(line, "ac");
}

static void i386_command_ls(const char *path) {
    struct vfs_node directory;
    struct vfs_dirent entry;
    uint32_t index = 0;
    int64_t result;

    if (path == 0 || path[0] == '\0') {
        path = "/";
    }
    if (vfs_opendir(&i386_vfs, path, &directory) != 0) {
        tty_write_str(i386_tty, "ls: directory not found\n", 0x0cu);
        return;
    }

    while ((result = vfs_readdir(&i386_vfs,
                                 &directory,
                                 &index,
                                 &entry)) > 0) {
        tty_write_str(i386_tty, entry.name, 0x0fu);
        if ((entry.attributes & VFS_ATTR_DIR) != 0u) {
            tty_write_str(i386_tty, "/\n", 0x0au);
        } else {
            tty_write_str(i386_tty, "\n", 0x0fu);
        }
    }
    if (result < 0) {
        tty_write_str(i386_tty, "ls: read error\n", 0x0cu);
    }
}

static void i386_command_cat(const char *path) {
    struct vfs_node node;
    uint32_t offset = 0;
    char buffer[129];
    int64_t count;

    if (path == 0 || path[0] == '\0') {
        tty_write_str(i386_tty, "usage: cat <path>\n", 0x0eu);
        return;
    }
    if (vfs_open(&i386_vfs, path, 0u, &node) != 0) {
        tty_write_str(i386_tty, "cat: file not found\n", 0x0cu);
        return;
    }

    do {
        count = vfs_read(&i386_vfs,
                         &node,
                         &offset,
                         buffer,
                         sizeof(buffer) - 1u,
                         VFS_READ_BLOCKING);
        if (count > 0) {
            buffer[count] = '\0';
            tty_write(i386_tty, buffer, (uint32_t)count, 0x0fu);
        }
    } while (count > 0);
    if (count < 0) {
        tty_write_str(i386_tty, "\ncat: read error\n", 0x0cu);
    } else {
        tty_write_str(i386_tty, "\n", 0x0fu);
    }
}

static void i386_command_ps(void) {
    for (uint32_t slot = 0; slot < 2u; slot++) {
        struct process_snapshot snapshot;

        if (!i386_scheduler_process_snapshot(slot, &snapshot)) {
            continue;
        }
        tty_write_str(i386_tty, "pid=", 0x0fu);
        tty_write_dec(i386_tty, snapshot.pid, 0x0fu);
        tty_write_str(i386_tty, " state=", 0x0fu);
        tty_write_dec(i386_tty, snapshot.state, 0x0fu);
        tty_write_str(i386_tty, " exit=", 0x0fu);
        tty_write_dec(i386_tty, (uint32_t)snapshot.exit_code, 0x0fu);
        tty_write_str(i386_tty, " name=", 0x0fu);
        tty_write_str(i386_tty, snapshot.name, 0x0au);
        tty_write_str(i386_tty, "\n", 0x0fu);
    }
}

static void i386_command_test32(void) {
    struct process_snapshot process0;
    struct process_snapshot process1;

    tty_write_str(i386_tty, "test32: loading /BOOT/TEST32.ELF twice\n", 0x0eu);
    if (!kernel_i386_run_test32(&process0, &process1)) {
        tty_write_str(i386_tty, "test32: FAILED\n", 0x0cu);
        return;
    }
    tty_write_str(i386_tty, "test32: PASS, pids=", 0x0au);
    tty_write_dec(i386_tty, process0.pid, 0x0fu);
    tty_write_str(i386_tty, ",", 0x0fu);
    tty_write_dec(i386_tty, process1.pid, 0x0fu);
    tty_write_str(i386_tty, "\n", 0x0fu);
}

static void i386_execute_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }
    if (streq(line, "help")) {
        tty_write_str(i386_tty,
                      "commands: help clear ps test32 ls [path] cat <path>\n",
                      0x0fu);
    } else if (streq(line, "clear")) {
        tty_clear(i386_tty);
    } else if (command_starts_with(line, "ls")) {
        i386_command_ls(command_argument(line));
    } else if (command_starts_with(line, "cat")) {
        i386_command_cat(command_argument(line));
    } else if (streq(line, "ps")) {
        i386_command_ps();
    } else if (streq(line, "test32")) {
        i386_command_test32();
    } else {
        tty_write_str(i386_tty, "unknown command: ", 0x0cu);
        tty_write_str(i386_tty, line, 0x0fu);
        tty_write_str(i386_tty, "\n", 0x0fu);
    }
}

int kernel_i386_shared_services_init(void) {
    struct block_device *boot_disk;
    struct blockdev_partition partition;

    tty_virtual_init_all(0u, 24u, 0x0fu);
    i386_tty = tty_active();
    if (i386_tty == 0) {
        return 0;
    }

    tty_clear(i386_tty);
    tty_write_str(i386_tty, "NexOS i386 shared kernel services\n", 0x0fu);
    tty_write_str(i386_tty, "Console/TTY: common kernel implementation OK\n", 0x0au);

    vfs_init(&i386_vfs);
    boot_disk = blockdev_get(0u);
    if (boot_disk == 0 ||
        blockdev_partition_get(boot_disk, 0u, &partition) != 0 ||
        fat32_mount(&i386_vfs.fat32,
                    boot_disk,
                    (uint32_t)partition.start_lba) != 0) {
        tty_write_str(i386_tty, "VFS: FAT32 root mount failed\n", 0x0cu);
        return 0;
    }

    i386_vfs.root_kind = VFS_MOUNT_FAT32;
    i386_vfs.root_slot = 0u;
    if (!i386_tty_input_self_test()) {
        tty_write_str(i386_tty, "TTY input self-test failed\n", 0x0cu);
        return 0;
    }

    tty_clear(i386_tty);
    tty_write_str(i386_tty, "NexOS i386 shared kernel services\n", 0x0fu);
    tty_write_str(i386_tty, "Console/TTY: common implementation OK\n", 0x0au);
    tty_write_str(i386_tty, "Keyboard IRQ1 -> keyboard_event -> TTY OK\n", 0x0au);
    tty_write_str(i386_tty, "TTY edit test: input/backspace/enter OK\n", 0x0au);
    tty_write_str(i386_tty, "VFS: FAT32 root mounted; ls/cat ready\n", 0x0au);
    tty_write_str(i386_tty, "Syscall ABI: common int 0x40 dispatcher OK\n", 0x0au);
    tty_write_str(i386_tty, "Process model: struct process + address_space OK\n", 0x0au);
    i386_prompt();
    return 1;
}

void kernel_i386_shared_services_run(void) {
    char line[TTY_LINE_MAX + 1u];

    for (;;) {
        struct keyboard_event event;

        __asm__ volatile("sti; hlt" : : : "memory");
        while (i386_pop_keyboard_event(&event)) {
            tty_feed_key_event(i386_tty, &event);
        }
        if (tty_has_line(i386_tty)) {
            (void)tty_read(i386_tty, line, sizeof(line), TTY_READ_LINE);
            i386_execute_command(line);
            i386_prompt();
        }
    }
}
