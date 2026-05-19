#pragma once

#include "kernel/public/sys/syscall.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"
#include "lib/parse.h"
#include "lib/string.h"

enum {
    SYSCALL_EXIT_TO_KERNEL = 0xfffffffffffffff0ull,
    SYSCALL_COPY_CHUNK = 256u,
    SYSCALL_PAGE_SIZE = NOS_PAGE_SIZE,
    SYSCALL_USER_STRING_MAX = 255u,
    SYSCALL_PATH_MAX = NOS_PATH_MAX
};

struct syscall_user_buffer {
    uint64_t user_addr;
    uint32_t size;
};

extern struct tty *g_syscall_tty;
extern volatile uint32_t *g_syscall_ticks;
extern struct vfs *g_syscall_vfs;
extern const struct bootx_boot_info *g_syscall_boot_info;
extern const struct bootx_memmap_entry *g_syscall_memmap;
extern uint32_t g_syscall_memmap_count;

extern uint8_t g_syscall_copy_buffer[SYSCALL_COPY_CHUNK];
extern char g_syscall_path_buffer[SYSCALL_PATH_MAX + 1];
extern char g_syscall_path_buffer2[SYSCALL_PATH_MAX + 1];
extern char g_syscall_name_buffer[NOS_TTY_LINE_MAX + 1];

uint64_t syscall_kill_bad_user_pointer(void);
int syscall_user_readable(uint64_t user_addr, uint32_t size);
int syscall_user_writable(uint64_t user_addr, uint32_t size);
int syscall_user_page_arg_valid(uint64_t user_addr);
int syscall_copy_from_user(void *dest, uint64_t user_addr, uint32_t size);
int syscall_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len);
int syscall_copy_to_user(uint64_t user_addr, const void *src, uint32_t size);
uint64_t syscall_handle_page_free(uint64_t user_page_addr);
uint64_t syscall_handle_exec(uint64_t user_name_addr, uint64_t user_envp_addr);
uint64_t syscall_handle_exec_replace(uint64_t user_name_addr, uint64_t user_envp_addr);
uint64_t syscall_handle_spawn(uint64_t user_name_addr,
                              uint32_t syscall_mode,
                              uint32_t flags,
                              uint64_t user_envp_addr);
uint64_t syscall_handle_getpid(void);
uint64_t syscall_handle_proc_query(uint32_t kind, uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_wait(uint32_t pid, uint64_t user_info_addr);
uint64_t syscall_handle_kill(uint32_t pid);
uint64_t syscall_handle_fg(uint32_t pid);
uint64_t syscall_handle_bg(uint32_t pid);
uint64_t syscall_handle_mkdir(uint64_t user_path_addr);
uint64_t syscall_handle_rmdir(uint64_t user_path_addr);
uint64_t syscall_handle_remove(uint64_t user_path_addr);
uint64_t syscall_handle_mount(uint64_t user_source_addr, uint64_t user_target_addr, uint32_t syscall_kind);
uint64_t syscall_handle_umount(uint64_t user_target_addr);
uint64_t syscall_handle_switch_root(uint64_t user_target_addr);
uint64_t syscall_handle_write(const struct syscall_user_buffer *buffer);
uint64_t syscall_handle_fd_write(uint32_t fd, const struct syscall_user_buffer *buffer);
uint64_t syscall_handle_open(uint64_t user_name_addr, uint32_t flags);
uint64_t syscall_handle_opendir(uint64_t user_path_addr);
uint64_t syscall_handle_fd_read(uint32_t fd, const struct syscall_user_buffer *buffer, uint32_t flags);
uint64_t syscall_handle_close(uint32_t fd);
uint64_t syscall_handle_dup2(uint32_t src_fd, uint32_t dst_fd);
uint64_t syscall_handle_pipe(uint64_t user_pair_addr);
uint64_t syscall_handle_readdir(uint32_t fd, uint64_t user_entry_addr);
uint64_t syscall_handle_boot_info_query(uint64_t user_info_addr);
uint64_t syscall_handle_memmap_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_pmm_query(uint64_t user_info_addr);
uint64_t syscall_handle_block_read(uint32_t disk_index, uint64_t lba, uint64_t user_info_addr);
uint64_t syscall_handle_block_write(uint32_t disk_index, uint64_t lba, uint64_t user_info_addr);
uint64_t syscall_handle_program_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_root_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_root_find(uint64_t user_name_addr, uint64_t user_info_addr);
uint64_t syscall_handle_fat_root_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_fat_root_find(uint64_t user_name_addr, uint64_t user_info_addr);
uint64_t syscall_handle_block_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_part_query(uint32_t disk_index, uint32_t slot, uint64_t user_info_addr);
uint64_t syscall_handle_mount_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_kmsg_query(uint32_t offset, uint64_t user_info_addr);
uint64_t syscall_handle_pci_query(uint64_t user_info_addr);
uint64_t syscall_handle_ac97_query(uint64_t user_info_addr);
uint64_t syscall_handle_hda_query(uint64_t user_info_addr);
uint64_t syscall_handle_rtl8139_query(uint64_t user_info_addr);
uint64_t syscall_handle_rtl8139_tx_test(void);
uint64_t syscall_handle_rtl8139_tx_send(uint64_t user_info_addr);
uint64_t syscall_handle_rtl8139_rx_dump(uint64_t user_info_addr);
uint64_t syscall_handle_audio_query(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_audio_tone(uint32_t index, uint32_t hz, uint32_t duration_ms);
uint64_t syscall_handle_audio_play(uint32_t index, uint64_t user_info_addr);
uint64_t syscall_handle_reboot(void);
uint64_t syscall_handle_capability_event(uint64_t user_info_addr);
uint64_t syscall_handle_gfx(uint32_t op, uint64_t user_info_addr);
uint64_t syscall_handle_gui_event(uint32_t op, uint64_t user_info_addr);
uint64_t syscall_handle_machine_info_query(uint64_t user_info_addr);
uint64_t syscall_handle_rtc_query(uint64_t user_info_addr);
uint64_t syscall_handle_tty_query(uint32_t fd, uint64_t user_info_addr);
uint64_t syscall_handle_query(uint32_t kind, uint64_t arg0, uint64_t arg1, uint64_t user_info_addr);
uint64_t syscall_handle_chdir(uint64_t user_path_addr);
uint64_t syscall_handle_getcwd(uint64_t user_path_addr, uint32_t size);
