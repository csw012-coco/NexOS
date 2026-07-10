#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct syscall_process_info;
struct syscall_block_info;
struct syscall_partition_info;
struct syscall_mount_info;
struct syscall_fd_info;
struct syscall_tty_info;
struct syscall_machine_info;
struct syscall_memmap_info;
struct syscall_pmm_info;
struct syscall_vm_info;
struct syscall_framebuffer_info;
struct syscall_boot_info;
struct syscall_rtc_info;
struct syscall_program_info;
struct syscall_root_entry_info;
struct syscall_fat_entry_info;

enum {
    STDIN_FILENO = 0,
    STDOUT_FILENO = 1,
    STDERR_FILENO = 2
};

ssize_t write(int fd, const void *buffer, size_t size);
ssize_t write_stdout(const void *buffer, size_t size);
ssize_t write_stderr(const void *buffer, size_t size);
uint32_t write_fd(uint32_t fd, const char *data, uint32_t size);
uint32_t write_str(const char *text);
uint32_t write_err(const char *data, uint32_t size);
uint32_t write_err_str(const char *text);
ssize_t read(int fd, void *buffer, size_t size);
ssize_t nex_read(int fd, void *buffer, size_t size, uint32_t flags);
uint32_t read_char_nonblock(char *ch);
long lseek(int fd, long offset, int whence);
int close(int fd);
int dup2(int old_fd, int new_fd);
int pipe(int pair[2]);
int mkdir(const char *path);
int rmdir(const char *path);
int remove(const char *path);
int mount(const char *source, const char *target, uint32_t kind);
int umount(const char *target);
int switch_root(const char *target);
int chdir(const char *path);
int getcwd(char *buffer, size_t size);
pid_t getpid(void);
pid_t fork(void);
pid_t spawn(const char *command, uint32_t mode, uint32_t flags);
pid_t spawn_ex(const char *command, uint32_t mode, uint32_t flags);
int exec(const char *command);
int exec_replace(const char *command);
int sys_query(uint32_t kind, uint32_t arg0, uint32_t arg1, void *buffer);
int block_query(uint32_t index, struct syscall_block_info *info);
int part_query(uint32_t disk_index,
               uint32_t slot,
               struct syscall_partition_info *info);
int mount_query(uint32_t index, struct syscall_mount_info *info);
int block_flush(uint32_t disk_index);
int fd_query(uint32_t fd, struct syscall_fd_info *info);
int tty_query(uint32_t fd, struct syscall_tty_info *info);
int machine_info_query(struct syscall_machine_info *info);
int memmap_query(uint32_t index, struct syscall_memmap_info *info);
int pmm_query(struct syscall_pmm_info *info);
int vm_query(struct syscall_vm_info *info);
int framebuffer_query(struct syscall_framebuffer_info *info);
int boot_info_query(struct syscall_boot_info *info);
int rtc_query(struct syscall_rtc_info *info);
int program_query(uint32_t index, struct syscall_program_info *info);
int root_query(uint32_t index, struct syscall_root_entry_info *info);
int root_find(const char *name, struct syscall_root_entry_info *info);
int fat_root_query(uint32_t index, struct syscall_fat_entry_info *info);
int fat_root_find(const char *name, struct syscall_fat_entry_info *info);
int proc_query(uint32_t kind, uint32_t index, struct syscall_process_info *info);
int wait(uint32_t pid, struct syscall_process_info *info);
int waitpid(pid_t pid);
int kill(pid_t pid);
int fg(uint32_t pid);
int bg(uint32_t pid);
int reboot(void);
uint32_t ticks(void);
void yield(void);
void sleep(uint32_t tick_count);
uint64_t page_alloc(void);
int page_free(uint64_t user_page_addr);
__attribute__((noreturn)) void _exit(int status);
