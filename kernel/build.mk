# =========================
# Kernel sources
# =========================
KERNEL_C_SRCS := \
	kernel/core/kernel.c \
	kernel/core/boot_log.c \
	kernel/core/boot_state.c \
	kernel/core/kernel_boot.c \
	kernel/core/kernel_init.c \
	kernel/core/kernel_config.c \
	kernel/core/clipboard.c \
	kernel/core/device_poll.c \
	kernel/core/graphics_service.c \
	kernel/core/machine_info.c \
	kernel/core/system_query.c \
	kernel/core/profile.c \
	kernel/core/system_power.c \
	kernel/core/kernel_panic.c \
	kernel/core/runtime.c \
	kernel/core/console.c \
	kernel/core/tty.c \
	kernel/core/tty_ansi.c \
	kernel/core/tty_hangul.c \
	kernel/core/tty_input.c \
	kernel/core/tty_line_edit.c \
	kernel/core/tty_queue.c \
	kernel/core/tty_utf8.c \
	kernel/core/tty_virtual.c \
	kernel/core/kprint.c \
	kernel/fs/file.c \
	kernel/fs/file_backend.c \
	kernel/fs/file_device_backend.c \
	kernel/fs/file_pipe_backend.c \
	kernel/mem/pmm.c \
	kernel/mem/vmm.c \
	kernel/sys/syscall.c \
	kernel/sys/syscall_common_request_core.c \
	kernel/sys/syscall_native_request_core.c \
	kernel/sys/syscall_mem.c \
	kernel/sys/syscall_proc.c \
	kernel/sys/syscall_ipc.c \
	kernel/sys/syscall_mmap.c \
	kernel/sys/syscall_fs.c \
	kernel/sys/syscall_fs_path.c \
	kernel/sys/syscall_fs_fd.c \
	kernel/sys/syscall_query.c \
	kernel/sys/syscall_rtl8139.c \
	kernel/sys/syscall_query_fat.c \
	kernel/sys/syscall_query_mount.c \
	kernel/sys/syscall_query_kmsg.c \
	kernel/sys/syscall_query_pci.c \
	kernel/sys/syscall_query_ac97.c \
	kernel/sys/syscall_query_rtl8139.c \
	kernel/sys/syscall_query_audio.c \
	kernel/sys/syscall_query_machine.c \
	kernel/sys/syscall_power.c \
	kernel/sys/syscall_event.c \
	kernel/sys/syscall_gfx.c \
	kernel/sys/syscall_clipboard.c \
	kernel/fs/fs_service_root_query.c \
	kernel/fs/fs_service_mount_query.c \
	kernel/fs/fs_service_path.c \
	kernel/fs/fs_service_fd.c \
	kernel/fs/path_resolve.c \
	kernel/proc/process_context.c \
	kernel/proc/process_command.c \
	kernel/proc/process_model.c \
	kernel/proc/process_core.c \
	kernel/proc/process_exec.c \
	kernel/proc/process_program_registry.c \
	kernel/sched/scheduler_core.c \
	kernel/sched/sched_policy.c \
	kernel/sched/runqueue.c \
	kernel/proc/job_control.c \
	kernel/proc/process_reap.c \
	kernel/proc/process_session.c \
	kernel/mem/address_space_core.c \
	kernel/driver/driver.c \
	kernel/proc/process_elf.c \
	arch/x86/x86_64/gdt.c \
	arch/x86/x86_64/paging.c \
	arch/x86/x86_64/idt.c \
	$(DRIVER_C_SRCS) \
	$(FS_C_SRCS) \
	lib/string.c \
	lib/string_benchmark.c \
	lib/parse.c \
	arch/x86/common/io.c \
	hal/x86/platform.c \
	hal/x86/cpu.c \
	hal/x86/interrupts.c \
	hal/x86/paging.c

KERNEL_ASM_SRCS := \
	arch/x86/x86_64/irq_stub.asm \
	arch/x86/x86_64/gdt_flush.asm \
	arch/x86/x86_64/user.asm

KERNEL_C_OBJS := $(addprefix $(BUILD)/,$(KERNEL_C_SRCS:.c=.o))
KERNEL_ASM_OBJS := $(addprefix $(BUILD)/,$(KERNEL_ASM_SRCS:.asm=.o))
OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)
