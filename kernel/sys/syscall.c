#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"
#include "hal/hal.h"

struct tty *g_syscall_tty;
volatile uint32_t *g_syscall_ticks;
struct vfs *g_syscall_vfs;
const struct bootx_boot_info *g_syscall_boot_info;
const struct bootx_memmap_entry *g_syscall_memmap;
uint32_t g_syscall_memmap_count;

uint8_t g_syscall_copy_buffer[SYSCALL_COPY_CHUNK] __attribute__((aligned(sizeof(uint32_t))));
char g_syscall_path_buffer[SYSCALL_PATH_MAX + 1];
char g_syscall_path_buffer2[SYSCALL_PATH_MAX + 1];
char g_syscall_name_buffer[NOS_TTY_LINE_MAX + 1];
struct syscall_trace g_last_syscall_trace;

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
    const struct process *trace_proc = process_current();

#define SYSCALL_RETURN(value) do { \
        uint64_t syscall_result__ = (uint64_t)(value); \
        g_last_syscall_trace.result = syscall_result__; \
        g_last_syscall_trace.returned = syscall_result__ != SYSCALL_EXIT_TO_KERNEL; \
        hal_display_service_pending(); \
        return syscall_result__; \
    } while (0)

    g_last_syscall_trace.valid = 1u;
    g_last_syscall_trace.number = frame->rax;
    g_last_syscall_trace.arg0 = frame->rbx;
    g_last_syscall_trace.arg1 = frame->rcx;
    g_last_syscall_trace.arg2 = frame->rdx;
    g_last_syscall_trace.arg3 = frame->rsi;
    g_last_syscall_trace.rip = frame->rip;
    g_last_syscall_trace.rsp = frame->rsp;
    g_last_syscall_trace.result = 0;
    g_last_syscall_trace.returned = 0u;
    g_last_syscall_trace.pid = trace_proc != 0 ? trace_proc->pid : 0u;

    switch (frame->rax) {
        case SYS_EXIT:
            process_exit_current(process_current_session(), (int32_t)frame->rbx);
            SYSCALL_RETURN(SYSCALL_EXIT_TO_KERNEL);
        case SYS_OPEN:
            SYSCALL_RETURN(syscall_handle_open(frame->rbx, (uint32_t)frame->rcx));
        case SYS_READ:
            buffer.user_addr = frame->rcx;
            buffer.size = (uint32_t)frame->rdx;
            SYSCALL_RETURN(syscall_handle_fd_read((uint32_t)frame->rbx, &buffer, (uint32_t)frame->rsi));
        case SYS_WRITE:
            buffer.user_addr = frame->rcx;
            buffer.size = (uint32_t)frame->rdx;
            SYSCALL_RETURN(syscall_handle_fd_write((uint32_t)frame->rbx, &buffer, frame));
        case SYS_CLOSE:
            SYSCALL_RETURN(syscall_handle_close((uint32_t)frame->rbx));
        case SYS_DUP2:
            SYSCALL_RETURN(syscall_handle_dup2((uint32_t)frame->rbx, (uint32_t)frame->rcx));
        case SYS_PIPE:
            SYSCALL_RETURN(syscall_handle_pipe(frame->rbx));
        case SYS_CLEAR:
            SYSCALL_RETURN(syscall_handle_clear());
        case SYS_TICKS:
            if (g_syscall_ticks == 0) {
                SYSCALL_RETURN(0);
            }
            SYSCALL_RETURN(*g_syscall_ticks);
        case SYS_SEEK:
            SYSCALL_RETURN(syscall_handle_seek((uint32_t)frame->rbx,
                                               (int64_t)frame->rcx,
                                               (uint32_t)frame->rdx));
        case SYS_YIELD:
            sched_yield_current(process_current_session(), frame);
            SYSCALL_RETURN(SYSCALL_EXIT_TO_KERNEL);
        case SYS_SLEEP:
            sched_sleep_current(process_current_session(), frame, (uint32_t)frame->rbx);
            SYSCALL_RETURN(SYSCALL_EXIT_TO_KERNEL);
        case SYS_EXEC:
            SYSCALL_RETURN(syscall_handle_exec(frame->rbx, frame->rcx));
        case SYS_EXEC_REPLACE:
            if (process_current() == 0) {
                SYSCALL_RETURN((uint64_t)-1);
            }
            {
                uint64_t rc = syscall_handle_exec_replace(frame->rbx, frame->rcx);

                if ((int64_t)rc < 0) {
                    SYSCALL_RETURN(rc);
                }
            }
            SYSCALL_RETURN(SYSCALL_EXIT_TO_KERNEL);
        case SYS_PROC_QUERY:
            SYSCALL_RETURN(syscall_handle_proc_query((uint32_t)frame->rbx, (uint32_t)frame->rcx, frame->rdx));
        case SYS_WAIT:
            SYSCALL_RETURN(syscall_handle_wait((uint32_t)frame->rbx, frame->rcx));
        case SYS_KILL:
            SYSCALL_RETURN(syscall_handle_kill((uint32_t)frame->rbx));
        case SYS_GETPID:
            SYSCALL_RETURN(syscall_handle_getpid());
        case SYS_FG:
            SYSCALL_RETURN(syscall_handle_fg((uint32_t)frame->rbx));
        case SYS_BG:
            SYSCALL_RETURN(syscall_handle_bg((uint32_t)frame->rbx));
        case SYS_MKDIR:
            SYSCALL_RETURN(syscall_handle_mkdir(frame->rbx));
        case SYS_RMDIR:
            SYSCALL_RETURN(syscall_handle_rmdir(frame->rbx));
        case SYS_REMOVE:
            SYSCALL_RETURN(syscall_handle_remove(frame->rbx));
        case SYS_CHDIR:
            SYSCALL_RETURN(syscall_handle_chdir(frame->rbx));
        case SYS_GETCWD:
            SYSCALL_RETURN(syscall_handle_getcwd(frame->rbx, (uint32_t)frame->rcx));
        case SYS_OPENDIR:
            SYSCALL_RETURN(syscall_handle_opendir(frame->rbx));
        case SYS_READDIR:
            SYSCALL_RETURN(syscall_handle_readdir((uint32_t)frame->rbx, frame->rcx));
        case SYS_MOUNT:
            SYSCALL_RETURN(syscall_handle_mount(frame->rbx, frame->rcx, (uint32_t)frame->rdx));
        case SYS_UMOUNT:
            SYSCALL_RETURN(syscall_handle_umount(frame->rbx));
        case SYS_SPAWN:
            SYSCALL_RETURN(syscall_handle_spawn(frame->rbx, (uint32_t)frame->rcx, (uint32_t)frame->rdx, frame->rsi));
        case SYS_QUERY:
            SYSCALL_RETURN(syscall_handle_query((uint32_t)frame->rbx, frame->rcx, frame->rdx, frame->rsi));
        case SYS_MKFIFO:
            SYSCALL_RETURN(syscall_handle_mkfifo(frame->rbx));
        case SYS_FORK:
            SYSCALL_RETURN(syscall_handle_fork(frame));
        case SYS_MMAP:
            SYSCALL_RETURN(syscall_handle_mmap(frame->rbx));
        case SYS_MUNMAP:
            SYSCALL_RETURN(syscall_handle_munmap(frame->rbx, frame->rcx));
        case SYS_SHM_OPEN:
            SYSCALL_RETURN(syscall_handle_shm_open(frame->rbx, frame->rcx, (uint32_t)frame->rdx));
        case SYS_SHM_UNLINK:
            SYSCALL_RETURN(syscall_handle_shm_unlink(frame->rbx));
        case SYS_MQ_OPEN:
            SYSCALL_RETURN(syscall_handle_mq_open(frame->rbx, (uint32_t)frame->rcx));
        case SYS_MQ_UNLINK:
            SYSCALL_RETURN(syscall_handle_mq_unlink(frame->rbx));
        case SYS_MQ_SEND:
            SYSCALL_RETURN(syscall_handle_mq_send((uint32_t)frame->rbx, frame->rcx));
        case SYS_MQ_RECEIVE:
            SYSCALL_RETURN(syscall_handle_mq_receive((uint32_t)frame->rbx, frame->rcx));
        case SYS_SEM_OPEN:
            SYSCALL_RETURN(syscall_handle_sem_open(frame->rbx,
                                                   (uint32_t)frame->rcx,
                                                   (uint32_t)frame->rdx));
        case SYS_SEM_UNLINK:
            SYSCALL_RETURN(syscall_handle_sem_unlink(frame->rbx));
        case SYS_SEM_TRYWAIT:
            SYSCALL_RETURN(syscall_handle_sem_trywait((uint32_t)frame->rbx));
        case SYS_SEM_POST:
            SYSCALL_RETURN(syscall_handle_sem_post((uint32_t)frame->rbx));
        case SYS_PAGE_ALLOC:
            SYSCALL_RETURN(addrspace_alloc_page());
        case SYS_PAGE_FREE:
            SYSCALL_RETURN(syscall_handle_page_free(frame->rbx));
        case SYS_SWITCH_ROOT:
            SYSCALL_RETURN(syscall_handle_switch_root(frame->rbx));
        case SYS_BLOCK_READ:
            SYSCALL_RETURN(syscall_handle_block_read((uint32_t)frame->rbx, frame->rcx, frame->rdx));
        case SYS_BLOCK_WRITE:
            SYSCALL_RETURN(syscall_handle_block_write((uint32_t)frame->rbx, frame->rcx, frame->rdx));
        case SYS_AUDIO_TONE:
            SYSCALL_RETURN(syscall_handle_audio_tone((uint32_t)frame->rbx,
                                                     (uint32_t)frame->rcx,
                                                     (uint32_t)frame->rdx));
        case SYS_AUDIO_PLAY:
            SYSCALL_RETURN(syscall_handle_audio_play((uint32_t)frame->rbx, frame->rcx));
        case SYS_AUDIO_PLAY_FD:
            SYSCALL_RETURN(syscall_handle_audio_play_fd((uint32_t)frame->rbx, frame->rcx));
        case SYS_RTL8139_TX_TEST:
            SYSCALL_RETURN(syscall_handle_rtl8139_tx_test());
        case SYS_RTL8139_TX_SEND:
            SYSCALL_RETURN(syscall_handle_rtl8139_tx_send(frame->rbx));
        case SYS_RTL8139_RX_DUMP:
            SYSCALL_RETURN(syscall_handle_rtl8139_rx_dump(frame->rbx));
        case SYS_REBOOT:
            SYSCALL_RETURN(syscall_handle_reboot());
        case SYS_CAPABILITY_EVENT:
            SYSCALL_RETURN(syscall_handle_capability_event(frame->rbx));
        case SYS_GFX:
            SYSCALL_RETURN(syscall_handle_gfx((uint32_t)frame->rbx, frame->rcx));
        case SYS_GUI_EVENT:
            SYSCALL_RETURN(syscall_handle_gui_event((uint32_t)frame->rbx, frame->rcx));
        case SYS_CLIPBOARD:
            SYSCALL_RETURN(syscall_handle_clipboard((uint32_t)frame->rbx, frame->rcx));
        default:
            SYSCALL_RETURN(0);
    }

#undef SYSCALL_RETURN
}
