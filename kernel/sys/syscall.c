#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/core/tty.h"

struct tty *g_syscall_tty;
volatile uint32_t *g_syscall_ticks;
struct vfs *g_syscall_vfs;
const struct bootx_boot_info *g_syscall_boot_info;
const struct bootx_memmap_entry *g_syscall_memmap;
uint32_t g_syscall_memmap_count;

uint8_t g_syscall_copy_buffer[SYSCALL_COPY_CHUNK];
char g_syscall_path_buffer[SYSCALL_PATH_MAX + 1];
char g_syscall_path_buffer2[SYSCALL_PATH_MAX + 1];
char g_syscall_name_buffer[NOS_TTY_LINE_MAX + 1];

void syscall_init(struct tty *tty,
                  volatile uint32_t *timer_ticks,
                  struct vfs *vfs,
                  const struct bootx_boot_info *boot_info,
                  const struct bootx_memmap_entry *memmap,
                  uint32_t memmap_count) {
    g_syscall_tty = tty;
    g_syscall_ticks = timer_ticks;
    g_syscall_vfs = vfs;
    g_syscall_boot_info = boot_info;
    g_syscall_memmap = memmap;
    g_syscall_memmap_count = memmap_count;
}

uint64_t syscall_dispatch(struct syscall_frame *frame) {
    struct syscall_user_buffer buffer;

    switch (frame->rax) {
        case SYS_EXIT:
            process_exit_current(process_current_session(), (int32_t)frame->rbx);
            return SYSCALL_EXIT_TO_KERNEL;
        case SYS_OPEN:
            return syscall_handle_open(frame->rbx, (uint32_t)frame->rcx);
        case SYS_READ:
            buffer.user_addr = frame->rcx;
            buffer.size = (uint32_t)frame->rdx;
            return syscall_handle_fd_read((uint32_t)frame->rbx, &buffer, (uint32_t)frame->rsi);
        case SYS_WRITE:
            buffer.user_addr = frame->rcx;
            buffer.size = (uint32_t)frame->rdx;
            return syscall_handle_fd_write((uint32_t)frame->rbx, &buffer);
        case SYS_CLOSE:
            return syscall_handle_close((uint32_t)frame->rbx);
        case SYS_DUP2:
            return syscall_handle_dup2((uint32_t)frame->rbx, (uint32_t)frame->rcx);
        case SYS_PIPE:
            return syscall_handle_pipe(frame->rbx);
        case SYS_CLEAR:
            return syscall_handle_clear();
        case SYS_TICKS:
            if (g_syscall_ticks == 0) {
                return 0;
            }
            return *g_syscall_ticks;
        case SYS_YIELD:
            sched_yield_current(process_current_session(), frame);
            return SYSCALL_EXIT_TO_KERNEL;
        case SYS_SLEEP:
            sched_sleep_current(process_current_session(), frame, (uint32_t)frame->rbx);
            return SYSCALL_EXIT_TO_KERNEL;
        case SYS_EXEC:
            return syscall_handle_exec(frame->rbx, frame->rcx);
        case SYS_EXEC_REPLACE:
            if (process_current() == 0) {
                return (uint64_t)-1;
            }
            {
                uint64_t rc = syscall_handle_exec_replace(frame->rbx, frame->rcx);

                if ((int64_t)rc < 0) {
                    return rc;
                }
            }
            return SYSCALL_EXIT_TO_KERNEL;
        case SYS_PROC_QUERY:
            return syscall_handle_proc_query((uint32_t)frame->rbx, (uint32_t)frame->rcx, frame->rdx);
        case SYS_WAIT:
            return syscall_handle_wait((uint32_t)frame->rbx, frame->rcx);
        case SYS_KILL:
            return syscall_handle_kill((uint32_t)frame->rbx);
        case SYS_GETPID:
            return syscall_handle_getpid();
        case SYS_FG:
            return syscall_handle_fg((uint32_t)frame->rbx);
        case SYS_BG:
            return syscall_handle_bg((uint32_t)frame->rbx);
        case SYS_MKDIR:
            return syscall_handle_mkdir(frame->rbx);
        case SYS_RMDIR:
            return syscall_handle_rmdir(frame->rbx);
        case SYS_REMOVE:
            return syscall_handle_remove(frame->rbx);
        case SYS_CHDIR:
            return syscall_handle_chdir(frame->rbx);
        case SYS_GETCWD:
            return syscall_handle_getcwd(frame->rbx, (uint32_t)frame->rcx);
        case SYS_OPENDIR:
            return syscall_handle_opendir(frame->rbx);
        case SYS_READDIR:
            return syscall_handle_readdir((uint32_t)frame->rbx, frame->rcx);
        case SYS_MOUNT:
            return syscall_handle_mount(frame->rbx, frame->rcx, (uint32_t)frame->rdx);
        case SYS_UMOUNT:
            return syscall_handle_umount(frame->rbx);
        case SYS_SPAWN:
            return syscall_handle_spawn(frame->rbx, (uint32_t)frame->rcx, (uint32_t)frame->rdx, frame->rsi);
        case SYS_QUERY:
            return syscall_handle_query((uint32_t)frame->rbx, frame->rcx, frame->rdx, frame->rsi);
        case SYS_PAGE_ALLOC:
            return addrspace_alloc_page();
        case SYS_PAGE_FREE:
            return syscall_handle_page_free(frame->rbx);
        case SYS_SWITCH_ROOT:
            return syscall_handle_switch_root(frame->rbx);
        case SYS_BLOCK_READ:
            return syscall_handle_block_read((uint32_t)frame->rbx, frame->rcx, frame->rdx);
        case SYS_BLOCK_WRITE:
            return syscall_handle_block_write((uint32_t)frame->rbx, frame->rcx, frame->rdx);
        case SYS_AUDIO_TONE:
            return syscall_handle_audio_tone((uint32_t)frame->rbx,
                                             (uint32_t)frame->rcx,
                                             (uint32_t)frame->rdx);
        case SYS_AUDIO_PLAY:
            return syscall_handle_audio_play((uint32_t)frame->rbx, frame->rcx);
        case SYS_AUDIO_PLAY_FD:
            return syscall_handle_audio_play_fd((uint32_t)frame->rbx, frame->rcx);
        case SYS_RTL8139_TX_TEST:
            return syscall_handle_rtl8139_tx_test();
        case SYS_RTL8139_TX_SEND:
            return syscall_handle_rtl8139_tx_send(frame->rbx);
        case SYS_RTL8139_RX_DUMP:
            return syscall_handle_rtl8139_rx_dump(frame->rbx);
        case SYS_REBOOT:
            return syscall_handle_reboot();
        case SYS_CAPABILITY_EVENT:
            return syscall_handle_capability_event(frame->rbx);
        case SYS_GFX:
            return syscall_handle_gfx((uint32_t)frame->rbx, frame->rcx);
        case SYS_GUI_EVENT:
            return syscall_handle_gui_event((uint32_t)frame->rbx, frame->rcx);
        case SYS_CLIPBOARD:
            return syscall_handle_clipboard((uint32_t)frame->rbx, frame->rcx);
        default:
            return 0;
    }
}
