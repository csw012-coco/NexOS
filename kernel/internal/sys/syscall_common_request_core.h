#pragma once

#include "kernel/public/sys/syscall_request.h"

struct syscall_capability_event;
struct audio_pcm_stream;
struct bootx_boot_info;
struct bootx_memmap_entry;

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
    uint64_t (*bad_pointer)(void);
    uint64_t bad_pointer_value;
};

struct syscall_common_misc_ops {
    uint64_t (*clear)(void *ctx);
    uint64_t (*ticks)(void *ctx);
    uint64_t (*reboot)(void *ctx);
    void *ctx;
};

void syscall_common_request_core_query_state_init(
    const struct bootx_boot_info *boot_info,
    const struct bootx_memmap_entry *memmap,
    uint32_t memmap_count);
void syscall_common_request_core_fill_fb_info(
    const struct bootx_boot_info *boot_info,
    struct syscall_framebuffer_info *info);
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
