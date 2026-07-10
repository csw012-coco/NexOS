#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "abi/syscall_abi.h"

extern uint32_t __nlibc32_syscall4(uint32_t number,
                                   uint32_t arg0,
                                   uint32_t arg1,
                                   uint32_t arg2,
                                   uint32_t arg3);

ssize_t write(int fd, const void *buffer, size_t size) {
    ssize_t result;

    do {
        result = (ssize_t)__nlibc32_syscall4(
            SYS_WRITE,
            (uint32_t)fd,
            (uint32_t)(uintptr_t)buffer,
            (uint32_t)size,
            0u);
        if (result == -2) {
            yield();
        }
    } while (result == -2);
    return result;
}

ssize_t write_stdout(const void *buffer, size_t size) {
    return write(STDOUT_FILENO, buffer, size);
}

ssize_t write_stderr(const void *buffer, size_t size) {
    return write(STDERR_FILENO, buffer, size);
}

uint32_t write_fd(uint32_t fd, const char *data, uint32_t size) {
    return (uint32_t)write((int)fd, data, size);
}

uint32_t write_str(const char *text) {
    return text != 0 ? (uint32_t)write_stdout(text, strlen(text)) : 0u;
}

uint32_t write_err(const char *data, uint32_t size) {
    return (uint32_t)write_stderr(data, size);
}

uint32_t write_err_str(const char *text) {
    return text != 0 ? write_err(text, (uint32_t)strlen(text)) : 0u;
}

int open(const char *path, int flags) {
    return (int)__nlibc32_syscall4(SYS_OPEN,
                                   (uint32_t)(uintptr_t)path,
                                   (uint32_t)flags,
                                   0u,
                                   0u);
}

ssize_t nex_read(int fd, void *buffer, size_t size, uint32_t flags) {
    ssize_t result;

    do {
        result = (ssize_t)__nlibc32_syscall4(
            SYS_READ,
            (uint32_t)fd,
            (uint32_t)(uintptr_t)buffer,
            (uint32_t)size,
            flags);
        if (result == -2 && (flags & SYS_READ_NONBLOCK) == 0u) {
            yield();
        }
    } while (result == -2 && (flags & SYS_READ_NONBLOCK) == 0u);
    if (result == -2) {
        return 0;
    }
    return result;
}

ssize_t read(int fd, void *buffer, size_t size) {
    return nex_read(fd, buffer, size, SYS_READ_BLOCKING);
}

long lseek(int fd, long offset, int whence) {
    return (long)(int32_t)__nlibc32_syscall4(SYS_SEEK,
                                             (uint32_t)fd,
                                             (uint32_t)offset,
                                             (uint32_t)whence,
                                             0u);
}

uint32_t read_char_nonblock(char *ch) {
    return (uint32_t)nex_read(STDIN_FILENO,
                              ch,
                              1u,
                              SYS_READ_NONBLOCK | SYS_READ_CHAR);
}

uint32_t read_line(uint32_t fd, char *buffer, uint32_t size) {
    uint32_t length = 0;
    uint32_t saw_input = 0;
    char ch;

    if (buffer == 0 || size == 0u) {
        return 0u;
    }
    while (length + 1u < size) {
        uint32_t got = (uint32_t)nex_read((int)fd,
                                          &ch,
                                          1u,
                                          SYS_READ_BLOCKING | SYS_READ_CHAR);

        if (got == 0u) {
            break;
        }
        saw_input = 1u;
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }
        buffer[length++] = ch;
    }
    buffer[length] = '\0';
    if (saw_input == 0u) {
        return 0u;
    }
    return length != 0u ? length : 1u;
}

int close(int fd) {
    return (int)__nlibc32_syscall4(SYS_CLOSE,
                                   (uint32_t)fd,
                                   0u,
                                   0u,
                                   0u);
}

int dup2(int old_fd, int new_fd) {
    return (int)__nlibc32_syscall4(SYS_DUP2,
                                   (uint32_t)old_fd,
                                   (uint32_t)new_fd,
                                   0u,
                                   0u);
}

int pipe(int pair[2]) {
    return (int)__nlibc32_syscall4(SYS_PIPE,
                                   (uint32_t)(uintptr_t)pair,
                                   0u,
                                   0u,
                                   0u);
}

int mkdir(const char *path) {
    return (int)__nlibc32_syscall4(SYS_MKDIR,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int rmdir(const char *path) {
    return (int)__nlibc32_syscall4(SYS_RMDIR,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int remove(const char *path) {
    return (int)__nlibc32_syscall4(SYS_REMOVE,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int mount(const char *source, const char *target, uint32_t kind) {
    return (int)__nlibc32_syscall4(SYS_MOUNT,
                                   (uint32_t)(uintptr_t)source,
                                   (uint32_t)(uintptr_t)target,
                                   kind,
                                   0u);
}

int umount(const char *target) {
    return (int)__nlibc32_syscall4(SYS_UMOUNT,
                                   (uint32_t)(uintptr_t)target,
                                   0u,
                                   0u,
                                   0u);
}

int switch_root(const char *target) {
    return (int)__nlibc32_syscall4(SYS_SWITCH_ROOT,
                                   (uint32_t)(uintptr_t)target,
                                   0u,
                                   0u,
                                   0u);
}

int chdir(const char *path) {
    return (int)__nlibc32_syscall4(SYS_CHDIR,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int getcwd(char *buffer, size_t size) {
    return (int)__nlibc32_syscall4(SYS_GETCWD,
                                   (uint32_t)(uintptr_t)buffer,
                                   (uint32_t)size,
                                   0u,
                                   0u);
}

int opendir(const char *path) {
    return (int)__nlibc32_syscall4(SYS_OPENDIR,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int readdir(uint32_t fd, struct syscall_dirent *entry) {
    return (int)__nlibc32_syscall4(SYS_READDIR,
                                   fd,
                                   (uint32_t)(uintptr_t)entry,
                                   0u,
                                   0u);
}

pid_t getpid(void) {
    return (pid_t)__nlibc32_syscall4(SYS_GETPID, 0u, 0u, 0u, 0u);
}

pid_t fork(void) {
    return (pid_t)__nlibc32_syscall4(SYS_FORK, 0u, 0u, 0u, 0u);
}

pid_t spawn_ex(const char *command, uint32_t mode, uint32_t flags) {
    return (pid_t)__nlibc32_syscall4(SYS_SPAWN,
                                     (uint32_t)(uintptr_t)command,
                                     mode,
                                     flags,
                                     0u);
}

pid_t spawn(const char *command, uint32_t mode, uint32_t flags) {
    return spawn_ex(command, mode, flags);
}

int exec(const char *command) {
    return (int)__nlibc32_syscall4(SYS_EXEC,
                                   (uint32_t)(uintptr_t)command,
                                   0u,
                                   0u,
                                   0u);
}

int exec_replace(const char *command) {
    return (int)__nlibc32_syscall4(SYS_EXEC_REPLACE,
                                   (uint32_t)(uintptr_t)command,
                                   0u,
                                   0u,
                                   0u);
}

void *mmap(void *addr,
           size_t length,
           int prot,
           int flags,
           int shm_handle,
           uint64_t offset) {
    struct syscall_mmap_request request;
    uint32_t result;

    request.addr = (uint64_t)(uintptr_t)addr;
    request.length = (uint64_t)length;
    request.prot = (uint32_t)prot;
    request.flags = (uint32_t)flags;
    request.shm_handle = (uint32_t)shm_handle;
    request.reserved = 0u;
    request.offset = offset;
    result = __nlibc32_syscall4(SYS_MMAP,
                                (uint32_t)(uintptr_t)&request,
                                0u,
                                0u,
                                0u);
    return result == 0u ? MAP_FAILED : (void *)(uintptr_t)result;
}

int munmap(void *addr, size_t length) {
    return __nlibc32_syscall4(SYS_MUNMAP,
                              (uint32_t)(uintptr_t)addr,
                              (uint32_t)length,
                              0u,
                              0u) != 0u ? 0 : -1;
}

int mprotect(void *addr, size_t length, int prot) {
    return __nlibc32_syscall4(SYS_MPROTECT,
                              (uint32_t)(uintptr_t)addr,
                              (uint32_t)length,
                              (uint32_t)prot,
                              0u) != 0u ? 0 : -1;
}

int shm_open(const char *name, size_t size, int flags) {
    return (int)__nlibc32_syscall4(SYS_SHM_OPEN,
                                   (uint32_t)(uintptr_t)name,
                                   (uint32_t)size,
                                   (uint32_t)flags,
                                   0u);
}

int shm_unlink(const char *name) {
    return __nlibc32_syscall4(SYS_SHM_UNLINK,
                              (uint32_t)(uintptr_t)name,
                              0u,
                              0u,
                              0u) != 0u ? 0 : -1;
}

mqd_t mq_open(const char *name, int flags) {
    return (mqd_t)__nlibc32_syscall4(SYS_MQ_OPEN,
                                     (uint32_t)(uintptr_t)name,
                                     (uint32_t)flags,
                                     0u,
                                     0u);
}

int mq_unlink(const char *name) {
    return __nlibc32_syscall4(SYS_MQ_UNLINK,
                              (uint32_t)(uintptr_t)name,
                              0u,
                              0u,
                              0u) != 0u ? 0 : -1;
}

int mq_send(mqd_t queue, const void *data, size_t size, int flags) {
    struct syscall_mq_buffer buffer;
    int32_t result;

    if (data == 0 || size == 0u || size > SYS_MQ_MESSAGE_MAX) {
        return -1;
    }
    buffer.data_addr = (uint64_t)(uintptr_t)data;
    buffer.size = (uint32_t)size;
    buffer.flags = (uint32_t)flags;
    for (;;) {
        result = (int32_t)__nlibc32_syscall4(SYS_MQ_SEND,
                                             (uint32_t)queue,
                                             (uint32_t)(uintptr_t)&buffer,
                                             0u,
                                             0u);
        if (result != 0 || (flags & IPC_NONBLOCK) != 0) {
            return result > 0 ? 0 : -1;
        }
        yield();
    }
}

int mq_receive(mqd_t queue, void *data, size_t capacity, int flags) {
    struct syscall_mq_buffer buffer;
    int32_t result;

    if (data == 0 || capacity == 0u) {
        return -1;
    }
    buffer.data_addr = (uint64_t)(uintptr_t)data;
    buffer.size = (uint32_t)capacity;
    buffer.flags = (uint32_t)flags;
    for (;;) {
        result = (int32_t)__nlibc32_syscall4(SYS_MQ_RECEIVE,
                                             (uint32_t)queue,
                                             (uint32_t)(uintptr_t)&buffer,
                                             0u,
                                             0u);
        if (result != 0 || (flags & IPC_NONBLOCK) != 0) {
            return result > 0 ? (int)buffer.size : -1;
        }
        yield();
    }
}

sem_t sem_open(const char *name, unsigned int initial_value, int flags) {
    return (sem_t)__nlibc32_syscall4(SYS_SEM_OPEN,
                                     (uint32_t)(uintptr_t)name,
                                     (uint32_t)initial_value,
                                     (uint32_t)flags,
                                     0u);
}

int sem_unlink(const char *name) {
    return __nlibc32_syscall4(SYS_SEM_UNLINK,
                              (uint32_t)(uintptr_t)name,
                              0u,
                              0u,
                              0u) != 0u ? 0 : -1;
}

int sem_trywait(sem_t sem) {
    return (int32_t)__nlibc32_syscall4(SYS_SEM_TRYWAIT,
                                       (uint32_t)sem,
                                       0u,
                                       0u,
                                       0u) > 0 ? 0 : -1;
}

int sem_wait(sem_t sem) {
    int32_t result;

    for (;;) {
        result = (int32_t)__nlibc32_syscall4(SYS_SEM_TRYWAIT,
                                             (uint32_t)sem,
                                             0u,
                                             0u,
                                             0u);
        if (result < 0) {
            return -1;
        }
        if (result > 0) {
            return 0;
        }
        yield();
    }
}

int sem_post(sem_t sem) {
    return __nlibc32_syscall4(SYS_SEM_POST,
                              (uint32_t)sem,
                              0u,
                              0u,
                              0u) != 0u ? 0 : -1;
}

int sys_query(uint32_t kind, uint32_t arg0, uint32_t arg1, void *buffer) {
    return (int)__nlibc32_syscall4(SYS_QUERY,
                                   kind,
                                   arg0,
                                   arg1,
                                   (uint32_t)(uintptr_t)buffer);
}

int block_query(uint32_t index, struct syscall_block_info *info) {
    return sys_query(SYS_QUERY_BLOCK, index, 0u, info);
}

int block_read(uint32_t disk_index, uint64_t lba, struct syscall_block_read_info *info) {
    return (int)__nlibc32_syscall4(SYS_BLOCK_READ,
                                   disk_index,
                                   (uint32_t)lba,
                                   (uint32_t)(uintptr_t)info,
                                   0u);
}

int block_write(uint32_t disk_index, uint64_t lba, struct syscall_block_write_info *info) {
    return (int)__nlibc32_syscall4(SYS_BLOCK_WRITE,
                                   disk_index,
                                   (uint32_t)lba,
                                   (uint32_t)(uintptr_t)info,
                                   0u);
}

int block_flush(uint32_t disk_index) {
    return (int)__nlibc32_syscall4(SYS_BLOCK_FLUSH,
                                   disk_index,
                                   0u,
                                   0u,
                                   0u);
}

int part_query(uint32_t disk_index,
               uint32_t slot,
               struct syscall_partition_info *info) {
    return sys_query(SYS_QUERY_PART, disk_index, slot, info);
}

int mount_query(uint32_t index, struct syscall_mount_info *info) {
    return sys_query(SYS_QUERY_MOUNT, index, 0u, info);
}

int fd_query(uint32_t fd, struct syscall_fd_info *info) {
    return sys_query(SYS_QUERY_FD, fd, 0u, info);
}

int tty_query(uint32_t fd, struct syscall_tty_info *info) {
    return sys_query(SYS_QUERY_TTY, fd, 0u, info);
}

int machine_info_query(struct syscall_machine_info *info) {
    return sys_query(SYS_QUERY_MACHINE_INFO, 0u, 0u, info);
}

int memmap_query(uint32_t index, struct syscall_memmap_info *info) {
    return sys_query(SYS_QUERY_MEMMAP, index, 0u, info);
}

int pmm_query(struct syscall_pmm_info *info) {
    return sys_query(SYS_QUERY_PMM, 0u, 0u, info);
}

int vm_query(struct syscall_vm_info *info) {
    return sys_query(SYS_QUERY_VM, 0u, 0u, info);
}

int framebuffer_query(struct syscall_framebuffer_info *info) {
    return sys_query(SYS_QUERY_FB, 0u, 0u, info);
}

int boot_info_query(struct syscall_boot_info *info) {
    return sys_query(SYS_QUERY_BOOT_INFO, 0u, 0u, info);
}

int program_query(uint32_t index, struct syscall_program_info *info) {
    return sys_query(SYS_QUERY_PROGRAM, index, 0u, info);
}

int root_query(uint32_t index, struct syscall_root_entry_info *info) {
    return sys_query(SYS_QUERY_ROOT, index, 0u, info);
}

int root_find(const char *name, struct syscall_root_entry_info *info) {
    return sys_query(SYS_QUERY_ROOT_FIND, (uint32_t)(uintptr_t)name, 0u, info);
}

int rtc_query(struct syscall_rtc_info *info) {
    return sys_query(SYS_QUERY_RTC, 0u, 0u, info);
}

int kmsg_query(uint32_t offset, struct syscall_kmsg_info *info) {
    return sys_query(SYS_QUERY_KMSG, offset, 0u, info);
}

int pci_query(struct syscall_pci_info *info) {
    return sys_query(SYS_QUERY_PCI, 0u, 0u, info);
}

int pci_query_at(uint32_t index, struct syscall_pci_info *info) {
    return sys_query(SYS_QUERY_PCI, index, 0u, info);
}

int profile_query(uint32_t index, uint32_t flags, struct syscall_profile_info *info) {
    return sys_query(SYS_QUERY_PROFILE, index, flags, info);
}

int ac97_query(struct syscall_ac97_info *info) {
    return sys_query(SYS_QUERY_AC97, 0u, 0u, info);
}

int hda_query(struct syscall_hda_info *info) {
    return sys_query(SYS_QUERY_HDA, 0u, 0u, info);
}

int audio_query(uint32_t index, struct syscall_audio_info *info) {
    return sys_query(SYS_QUERY_AUDIO, index, 0u, info);
}

int audio_tone(uint32_t index, uint32_t hz, uint32_t duration_ms) {
    return (int)__nlibc32_syscall4(SYS_AUDIO_TONE,
                                   index,
                                   hz,
                                   duration_ms,
                                   0u);
}

int audio_play(uint32_t index, const struct syscall_audio_play_info *info) {
    return (int)__nlibc32_syscall4(SYS_AUDIO_PLAY,
                                   index,
                                   (uint32_t)(uintptr_t)info,
                                   0u,
                                   0u);
}

int audio_play_fd(uint32_t index, const struct syscall_audio_stream_info *info) {
    return (int)__nlibc32_syscall4(SYS_AUDIO_PLAY_FD,
                                   index,
                                   (uint32_t)(uintptr_t)info,
                                   0u,
                                   0u);
}

int rtl8139_query(struct syscall_rtl8139_info *info) {
    return sys_query(SYS_QUERY_RTL8139, 0u, 0u, info);
}

int rtl8139_tx_test(void) {
    return (int)__nlibc32_syscall4(SYS_RTL8139_TX_TEST, 0u, 0u, 0u, 0u);
}

int rtl8139_rx_dump(struct syscall_rtl8139_rx_info *info) {
    return (int)__nlibc32_syscall4(SYS_RTL8139_RX_DUMP,
                                   (uint32_t)(uintptr_t)info,
                                   0u,
                                   0u,
                                   0u);
}

int rtl8139_tx_send(const void *data, uint32_t bytes) {
    struct syscall_rtl8139_tx_info info;

    info.bytes = bytes;
    info.reserved = 0u;
    info.data_addr = (uint64_t)(uintptr_t)data;
    return (int)__nlibc32_syscall4(SYS_RTL8139_TX_SEND,
                                   (uint32_t)(uintptr_t)&info,
                                   0u,
                                   0u,
                                   0u);
}

int proc_query(uint32_t kind, uint32_t index, struct syscall_process_info *info) {
    return (int)__nlibc32_syscall4(SYS_PROC_QUERY,
                                   kind,
                                   index,
                                   (uint32_t)(uintptr_t)info,
                                   0u);
}

int waitpid(pid_t pid) {
    return (int)__nlibc32_syscall4(SYS_WAIT,
                                   (uint32_t)pid,
                                   0u,
                                   0u,
                                   0u);
}

int wait(uint32_t pid, struct syscall_process_info *info) {
    int status = (int)__nlibc32_syscall4(SYS_WAIT, pid, 0u, 0u, 0u);

    if (status != 0 && info != 0) {
        memset(info, 0, sizeof(*info));
        info->pid = pid;
        info->exit_code = status;
    }
    return status != 0 ? 1 : 0;
}

int kill(pid_t pid) {
    return (int)__nlibc32_syscall4(SYS_KILL,
                                   (uint32_t)pid,
                                   0u,
                                   0u,
                                   0u);
}

int fg(uint32_t pid) {
    return (int)__nlibc32_syscall4(SYS_FG, pid, 0u, 0u, 0u);
}

int bg(uint32_t pid) {
    return (int)__nlibc32_syscall4(SYS_BG, pid, 0u, 0u, 0u);
}

int reboot(void) {
    return (int)__nlibc32_syscall4(SYS_REBOOT, 0u, 0u, 0u, 0u);
}

int capability_event(const struct syscall_capability_event *event) {
    return (int)__nlibc32_syscall4(SYS_CAPABILITY_EVENT,
                                   (uint32_t)(uintptr_t)event,
                                   0u,
                                   0u,
                                   0u);
}

void clear(void) {
    (void)__nlibc32_syscall4(SYS_CLEAR, 0u, 0u, 0u, 0u);
}

int clipboard_get(char *buffer, uint32_t size) {
    struct syscall_clipboard_transfer transfer;
    int rc;

    transfer.data_addr = (uint64_t)(uintptr_t)buffer;
    transfer.bytes = size;
    transfer.size = 0u;
    rc = (int)__nlibc32_syscall4(SYS_CLIPBOARD,
                                 SYS_CLIPBOARD_GET,
                                 (uint32_t)(uintptr_t)&transfer,
                                 0u,
                                 0u);
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
    return (int)__nlibc32_syscall4(SYS_CLIPBOARD,
                                   SYS_CLIPBOARD_SET,
                                   (uint32_t)(uintptr_t)&transfer,
                                   0u,
                                   0u);
}

int clipboard_clear(void) {
    return (int)__nlibc32_syscall4(SYS_CLIPBOARD,
                                   SYS_CLIPBOARD_CLEAR,
                                   0u,
                                   0u,
                                   0u);
}

int clipboard_size(void) {
    struct syscall_clipboard_transfer transfer;

    transfer.data_addr = 0u;
    transfer.bytes = 0u;
    transfer.size = 0u;
    return (int)__nlibc32_syscall4(SYS_CLIPBOARD,
                                   SYS_CLIPBOARD_SIZE,
                                   (uint32_t)(uintptr_t)&transfer,
                                   0u,
                                   0u);
}

static struct syscall_gfx_batch_entry *g_gfx_batch_entries;
static uint32_t g_gfx_batch_count;
static uint32_t g_gfx_batch_capacity;

static int gfx_command(uint32_t op, const struct syscall_gfx_command *cmd) {
    if (g_gfx_batch_entries != 0) {
        struct syscall_gfx_batch_entry *entry;

        if (g_gfx_batch_count >= g_gfx_batch_capacity ||
            op == SYS_GFX_INFO || op == SYS_GFX_PRESENT || op == SYS_GFX_BATCH) {
            return -1;
        }
        entry = &g_gfx_batch_entries[g_gfx_batch_count++];
        entry->op = op;
        entry->reserved = 0u;
        entry->command = *cmd;
        return 0;
    }
    return (int)__nlibc32_syscall4(SYS_GFX,
                                   op,
                                   (uint32_t)(uintptr_t)cmd,
                                   0u,
                                   0u);
}

int gfx_info(struct syscall_gfx_info *info) {
    return (int)__nlibc32_syscall4(SYS_GFX,
                                   SYS_GFX_INFO,
                                   (uint32_t)(uintptr_t)info,
                                   0u,
                                   0u);
}

int gfx_batch_begin(struct syscall_gfx_batch_entry *entries, uint32_t capacity) {
    if (entries == 0 || capacity == 0u || g_gfx_batch_entries != 0) {
        return -1;
    }
    g_gfx_batch_entries = entries;
    g_gfx_batch_count = 0u;
    g_gfx_batch_capacity = capacity;
    return 0;
}

int gfx_batch_submit(uint32_t flags) {
    struct syscall_gfx_batch batch;

    if (g_gfx_batch_entries == 0) {
        return -1;
    }
    batch.entries_addr = (uint64_t)(uintptr_t)g_gfx_batch_entries;
    batch.count = g_gfx_batch_count;
    batch.flags = flags;
    g_gfx_batch_entries = 0;
    g_gfx_batch_count = 0u;
    g_gfx_batch_capacity = 0u;
    return (int)__nlibc32_syscall4(SYS_GFX,
                                   SYS_GFX_BATCH,
                                   (uint32_t)(uintptr_t)&batch,
                                   0u,
                                   0u);
}

void gfx_batch_cancel(void) {
    g_gfx_batch_entries = 0;
    g_gfx_batch_count = 0u;
    g_gfx_batch_capacity = 0u;
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

int gfx_blit(const uint32_t *pixels,
             uint32_t pitch,
             int32_t dst_x,
             int32_t dst_y,
             uint32_t width,
             uint32_t height) {
    struct syscall_gfx_blit blit;

    if (pixels == 0 || width == 0u || height == 0u || g_gfx_batch_entries != 0) {
        return -1;
    }
    blit.pixels_addr = (uint64_t)(uintptr_t)pixels;
    blit.dst_x = dst_x;
    blit.dst_y = dst_y;
    blit.width = width;
    blit.height = height;
    blit.pitch = pitch;
    blit.format = SYS_GFX_FORMAT_XRGB8888;
    blit.flags = 0u;
    return (int)__nlibc32_syscall4(SYS_GFX,
                                   SYS_GFX_BLIT,
                                   (uint32_t)(uintptr_t)&blit,
                                   0u,
                                   0u);
}

int gfx_present(void) {
    struct syscall_gfx_command cmd = {0};

    if (g_gfx_batch_entries != 0) {
        return gfx_batch_submit(SYS_GFX_BATCH_PRESENT);
    }
    return gfx_command(SYS_GFX_PRESENT, &cmd);
}

int gui_event_cursor_init(struct syscall_gui_event_cursor *cursor) {
    if (cursor == 0) {
        return -1;
    }
    return (int)__nlibc32_syscall4(SYS_GUI_EVENT,
                                   SYS_GUI_EVENT_CURSOR_INIT,
                                   (uint32_t)(uintptr_t)cursor,
                                   0u,
                                   0u);
}

int gui_poll_event_with_cursor(struct syscall_gui_event_cursor *cursor,
                               struct syscall_gui_event *event) {
    struct syscall_gui_event_poll poll;
    int rc;

    if (cursor == 0 || event == 0) {
        return -1;
    }
    poll.cursor = *cursor;
    memset(&poll.event, 0, sizeof(poll.event));
    poll.event.type = SYS_GUI_EVENT_NONE;
    poll.keyboard_dropped = 0u;
    poll.mouse_dropped = 0u;

    rc = (int)__nlibc32_syscall4(SYS_GUI_EVENT,
                                 SYS_GUI_EVENT_POLL,
                                 (uint32_t)(uintptr_t)&poll,
                                 0u,
                                 0u);
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

int gui_input_grab(void) {
    return (int)__nlibc32_syscall4(SYS_GUI_EVENT,
                                   SYS_GUI_EVENT_GRAB,
                                   0u,
                                   0u,
                                   0u);
}

int gui_input_release(void) {
    return (int)__nlibc32_syscall4(SYS_GUI_EVENT,
                                   SYS_GUI_EVENT_RELEASE,
                                   0u,
                                   0u,
                                   0u);
}

uint32_t ticks(void) {
    return __nlibc32_syscall4(SYS_TICKS, 0u, 0u, 0u, 0u);
}

void yield(void) {
    (void)__nlibc32_syscall4(SYS_YIELD, 0u, 0u, 0u, 0u);
}

void sleep(uint32_t tick_count) {
    (void)__nlibc32_syscall4(SYS_SLEEP, tick_count, 0u, 0u, 0u);
}

uint64_t page_alloc(void) {
    return (uint64_t)__nlibc32_syscall4(SYS_PAGE_ALLOC,
                                        0u,
                                        0u,
                                        0u,
                                        0u);
}

int page_free(uint64_t user_page_addr) {
    return (int)__nlibc32_syscall4(SYS_PAGE_FREE,
                                   (uint32_t)user_page_addr,
                                   0u,
                                   0u,
                                   0u);
}

__attribute__((noreturn)) void exit_with_code(uint64_t code) {
    (void)__nlibc32_syscall4(SYS_EXIT, (uint32_t)code, 0u, 0u, 0u);
    for (;;) {
        __asm__ volatile("ud2");
    }
}

void _exit(int status) {
    exit_with_code((uint64_t)(int64_t)status);
}

void exit(int status) {
    exit_with_code((uint64_t)(int64_t)status);
}
