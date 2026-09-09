#pragma once

#include "kernel/public/sys/syscall_request.h"

struct syscall_capability_event;
struct audio_pcm_stream;
struct bootx_boot_info;
struct bootx_memmap_entry;
struct vfs;

struct syscall_common_gfx_blit_plan {
    uint64_t first_addr;
    uint64_t span;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t visible_width;
    uint32_t visible_height;
    int32_t dst_x;
    int32_t dst_y;
    uint32_t pitch;
};

struct syscall_common_clipboard_transfer_ops {
    int (*copy_from_user)(void *dest, uint64_t user_addr, uint32_t size);
    int (*copy_to_user)(uint64_t user_addr, const void *src, uint32_t size);
    uint64_t (*bad_pointer)(void);
    uint64_t bad_pointer_value;
};

struct syscall_common_user_input_ops {
    int (*copy_from_user)(void *dest, uint64_t user_addr, uint32_t size);
    uint64_t (*bad_pointer)(void);
    uint64_t bad_pointer_value;
};

struct syscall_common_user_copy_ops {
    int (*copy_from_user)(void *dest, uint64_t user_addr, uint32_t size);
    int (*copy_to_user)(uint64_t user_addr, const void *src, uint32_t size);
    int (*copy_user_cstr)(char *dest, uint64_t user_addr, uint32_t size);
    uint64_t (*bad_pointer)(void);
    uint64_t bad_pointer_value;
};

struct syscall_common_misc_ops {
    uint64_t (*clear)(void *ctx);
    uint64_t (*ticks)(void *ctx);
    uint64_t (*reboot)(void *ctx);
    uint64_t (*poweroff)(void *ctx);
    void *ctx;
};

struct syscall_common_proc_ops {
    uint64_t (*exit)(void *ctx, int32_t code);
    uint64_t (*yield)(void *ctx);
    uint64_t (*exec_replace)(void *ctx,
                             uint64_t user_command,
                             uint64_t user_envp);
    uint64_t (*exec)(void *ctx, uint64_t user_command, uint64_t user_envp);
    uint64_t (*fork)(void *ctx);
    uint64_t (*wait)(void *ctx, uint32_t pid, uint64_t user_info_addr);
    uint64_t (*sleep)(void *ctx, uint32_t ticks);
    uint64_t (*getpid)(void *ctx);
    uint64_t (*proc_query)(void *ctx,
                           uint32_t kind,
                           uint32_t index,
                           uint64_t user_info_addr);
    uint64_t (*kill)(void *ctx, uint32_t pid);
    uint64_t (*fg)(void *ctx, uint32_t pid);
    uint64_t (*bg)(void *ctx, uint32_t pid);
    uint64_t (*tty_claim)(void *ctx);
    uint64_t (*spawn)(void *ctx,
                      uint64_t user_command,
                      uint32_t mode,
                      uint32_t flags,
                      uint64_t user_envp);
    void *ctx;
};

struct syscall_common_mount_ops {
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    int has_boot_info;
    uint64_t (*switch_root)(void *ctx, struct vfs *vfs, const char *target);
    void *ctx;
};

struct tty;

struct syscall_common_file_io_ops {
    char *io_buffer;
    uint32_t io_buffer_size;
    struct tty *tty;
    void (*drain_tty_input)(void *ctx, struct tty *tty);
    void (*wait_for_interrupt)(void *ctx);
    void *ctx;
};

struct syscall_common_query_ops {
    uint32_t (*fd_kind)(void *ctx, uint32_t fd);
    int (*tty_query)(void *ctx, uint32_t fd, struct syscall_tty_info *info);
    int32_t (*fd_query)(void *ctx, uint32_t fd, struct syscall_fd_info *info);
    int (*fill_mount_info)(void *ctx,
                           struct syscall_mount_info *info,
                           uint32_t index,
                           uint32_t flags);
    void (*fill_machine_info)(void *ctx, struct syscall_machine_info *info);
    void *ctx;
};

struct syscall_common_vm_page_ops {
    uint64_t (*page_alloc)(void *ctx);
    uint64_t (*page_alloc_prot)(void *ctx, uint32_t writable);
    uint64_t (*page_alloc_at)(void *ctx, uint64_t user_page, uint32_t writable);
    uint64_t (*page_free)(void *ctx, uint64_t user_page);
    uint64_t (*page_protect)(void *ctx, uint64_t user_page, uint32_t writable);
    uint64_t (*release_page)(void *ctx, uint64_t user_page);
    uint64_t user_mmap_base;
    uint64_t user_mmap_end;
    uint32_t page_size;
    uint32_t max_pages;
    void *ctx;
};

struct process;

void syscall_common_request_core_query_state_init(
    struct vfs *vfs,
    const struct bootx_boot_info *boot_info,
    const struct bootx_memmap_entry *memmap,
    uint32_t memmap_count);
void syscall_common_request_core_fill_fb_info(
    const struct bootx_boot_info *boot_info,
    struct syscall_framebuffer_info *info);
void syscall_common_request_core_set_root_token(const char *token);
int syscall_common_request_core_backend(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_common_request_core_clipboard(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
uint32_t syscall_common_request_core_clipboard_size(void);
const char *syscall_common_request_core_clipboard_text(void);
uint32_t syscall_common_request_core_clipboard_copy_size(uint32_t requested);
uint32_t syscall_common_request_core_clipboard_set_text(const char *text,
                                                       uint32_t bytes);
uint32_t syscall_common_request_core_clipboard_prepare_get(
    struct syscall_clipboard_transfer *transfer);
uint32_t syscall_common_request_core_clipboard_prepare_set(
    const struct syscall_clipboard_transfer *transfer);
uint32_t syscall_common_request_core_clipboard_commit_set(
    struct syscall_clipboard_transfer *transfer,
    const char *text,
    uint32_t bytes);
uint32_t syscall_common_request_core_clipboard_prepare_size(
    struct syscall_clipboard_transfer *transfer);
uint64_t syscall_common_request_core_clipboard_transfer(
    uint32_t op,
    uint64_t user_info_addr,
    const struct syscall_common_clipboard_transfer_ops *ops,
    char *scratch,
    uint32_t scratch_size);
int syscall_common_request_core_clipboard_transfer_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_clipboard_transfer_ops *ops,
    char *scratch,
    uint32_t scratch_size);
int syscall_common_request_core_audio_play_valid(
    const struct syscall_audio_play_info *info,
    uint32_t max_bytes);
uint64_t syscall_common_request_core_audio_play_dispatch(
    uint32_t index,
    const struct syscall_audio_play_info *info,
    const uint8_t *buffer);
uint64_t syscall_common_request_core_audio_play_transfer(
    uint32_t index,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr);
int syscall_common_request_core_audio_play_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr);
int syscall_common_request_core_audio_stream_valid(
    const struct syscall_audio_stream_info *info);
void syscall_common_request_core_audio_stream_init(
    struct audio_pcm_stream *stream,
    const struct syscall_audio_stream_info *info,
    void *ctx,
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx));
uint64_t syscall_common_request_core_audio_stream_dispatch(
    uint32_t index,
    struct audio_pcm_stream *stream);
uint64_t syscall_common_request_core_audio_stream_transfer(
    uint32_t index,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    void *ctx,
    void (*prepare)(void *ctx, const struct syscall_audio_stream_info *info),
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx));
int syscall_common_request_core_audio_stream_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    void *ctx,
    void (*prepare)(void *ctx, const struct syscall_audio_stream_info *info),
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx));
int syscall_common_request_core_rtl8139_tx_valid(
    const struct syscall_rtl8139_tx_info *info,
    uint32_t max_bytes);
uint64_t syscall_common_request_core_rtl8139_tx_dispatch(
    const uint8_t *frame,
    uint32_t bytes);
uint64_t syscall_common_request_core_rtl8139_rx_dispatch(
    struct syscall_rtl8139_rx_info *info);
uint64_t syscall_common_request_core_rtl8139_tx_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr);
uint64_t syscall_common_request_core_rtl8139_rx_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_rtl8139_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr);
int syscall_common_request_core_block_read_dispatch(
    uint32_t disk_index,
    uint64_t lba,
    struct syscall_block_read_info *info);
int syscall_common_request_core_block_write_dispatch(
    uint32_t disk_index,
    uint64_t lba,
    struct syscall_block_write_info *info);
uint64_t syscall_common_request_core_block_flush_dispatch(uint32_t disk_index);
uint64_t syscall_common_request_core_block_read_transfer(
    uint32_t disk_index,
    uint64_t lba,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_block_write_transfer(
    uint32_t disk_index,
    uint64_t lba,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_block_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_capability_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_identity_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_capability_event(
    struct syscall_capability_event *event);
uint64_t syscall_common_request_core_capability_event_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_input_ops *ops);
int syscall_common_request_core_capability_event_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_input_ops *ops);
int syscall_common_request_core_misc_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_misc_ops *ops);
int syscall_common_request_core_proc_lifecycle_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_proc_ops *ops);
int syscall_common_request_core_gui_event_cursor_init(
    struct syscall_gui_event_cursor *cursor);
uint64_t syscall_common_request_core_gui_event_poll(
    struct syscall_gui_event_poll *poll,
    uint32_t current_pid);
uint64_t syscall_common_request_core_gui_event_grab(uint32_t current_pid,
                                                    int foreground_allowed);
uint64_t syscall_common_request_core_gui_event_release(uint32_t current_pid);
int syscall_common_request_core_gui_event_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    uint32_t current_pid,
    int foreground_allowed);
int syscall_common_request_core_gui_event_transfer_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint32_t current_pid,
    int foreground_allowed);
int syscall_common_request_core_query_info(uint32_t kind,
                                           uint64_t arg0,
                                           uint64_t arg1,
                                           void *info,
                                           uint32_t *info_size);
uint64_t syscall_common_request_core_query_transfer(
    uint32_t kind,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_query_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_query_request_with_ops(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_query_ops *query_ops);
uint64_t syscall_common_request_core_getcwd_transfer(
    struct process *proc,
    uint64_t user_path_addr,
    uint32_t size,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_opendir_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_path_addr,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_readdir_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint32_t fd,
    uint64_t user_entry_addr,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_pipe_transfer(
    struct process *proc,
    uint64_t user_pair_addr,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_dup2_dispatch(
    struct process *proc,
    uint32_t src_fd,
    uint32_t dst_fd);
int syscall_common_request_core_fs_fd_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_open_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_path_addr,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_read_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint32_t fd,
    uint64_t user_address,
    uint32_t size,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops);
int syscall_common_request_core_tty_read_transfer(
    uint64_t user_address,
    uint32_t size,
    uint32_t flags,
    struct tty *tty,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops,
    uint64_t *result_out);
uint64_t syscall_common_request_core_write_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint32_t fd,
    uint64_t user_address,
    uint32_t size,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops);
uint64_t syscall_common_request_core_close_dispatch(struct process *proc,
                                                    uint32_t fd);
uint64_t syscall_common_request_core_seek_dispatch(struct process *proc,
                                                   uint32_t fd,
                                                   int64_t offset,
                                                   uint32_t whence);
int syscall_common_request_core_io_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops);
uint64_t syscall_common_request_core_page_alloc_dispatch(
    const struct syscall_common_vm_page_ops *ops);
uint64_t syscall_common_request_core_page_free_dispatch(
    const struct syscall_common_vm_page_ops *ops,
    uint64_t user_page);
uint64_t syscall_common_request_core_page_protect_dispatch(
    const struct syscall_common_vm_page_ops *ops,
    uint64_t user_page,
    uint32_t writable);
int syscall_common_request_core_vm_page_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_vm_page_ops *ops);
uint64_t syscall_common_request_core_mmap_transfer(
    uint64_t user_request_addr,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_vm_page_ops *page_ops);
uint64_t syscall_common_request_core_munmap_dispatch(
    uint64_t user_addr,
    uint64_t length,
    const struct syscall_common_vm_page_ops *page_ops);
uint64_t syscall_common_request_core_mprotect_dispatch(
    uint64_t user_addr,
    uint64_t length,
    uint32_t prot,
    const struct syscall_common_vm_page_ops *page_ops);
int syscall_common_request_core_mmap_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_vm_page_ops *page_ops);
uint64_t syscall_common_request_core_shm_open_transfer(
    uint64_t user_name_addr,
    uint64_t size,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_shm_unlink_transfer(
    uint64_t user_name_addr,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_shm_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_ipc_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_path_transfer(
    struct vfs *vfs,
    struct process *proc,
    uint32_t number,
    uint64_t user_path_addr,
    const struct syscall_common_user_copy_ops *ops);
int syscall_common_request_core_fs_path_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_mount_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_source_addr,
    uint64_t user_target_addr,
    uint32_t kind,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_mount_ops *mount_ops);
uint64_t syscall_common_request_core_umount_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_target_addr,
    const struct syscall_common_user_copy_ops *ops);
uint64_t syscall_common_request_core_switch_root_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_target_addr,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_mount_ops *mount_ops);
int syscall_common_request_core_mount_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_mount_ops *mount_ops);
int syscall_common_request_core_gfx_info(uint32_t op,
                                         struct syscall_gfx_info *info);
int syscall_common_request_core_gfx_command(
    uint32_t op,
    const struct syscall_gfx_command *cmd);
int syscall_common_request_core_gfx_batch_valid(
    const struct syscall_gfx_batch *batch);
int syscall_common_request_core_gfx_batch_dispatch(
    const struct syscall_gfx_batch_entry *entries,
    uint32_t count,
    int allow_blit);
int syscall_common_request_core_gfx_blit_plan(
    const struct syscall_gfx_blit *blit,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    struct syscall_common_gfx_blit_plan *plan,
    int *noop);
int syscall_common_request_core_gfx_blit_dispatch(
    const uint32_t *pixels,
    uint32_t pitch,
    uint32_t width,
    uint32_t height,
    int32_t dst_x,
    int32_t dst_y);
uint64_t syscall_common_request_core_gfx_batch_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    struct syscall_gfx_batch_entry *scratch,
    uint32_t scratch_count,
    int allow_blit);
uint64_t syscall_common_request_core_gfx_blit_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint32_t max_dimension,
    uint64_t max_user_addr);
uint64_t syscall_common_request_core_gfx_transfer(
    uint32_t op,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    struct syscall_gfx_batch_entry *batch_scratch,
    uint32_t batch_scratch_count,
    uint8_t *blit_scratch,
    uint32_t blit_scratch_size,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    int allow_blit);
int syscall_common_request_core_gfx_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    struct syscall_gfx_batch_entry *batch_scratch,
    uint32_t batch_scratch_count,
    uint8_t *blit_scratch,
    uint32_t blit_scratch_size,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    int allow_blit);
