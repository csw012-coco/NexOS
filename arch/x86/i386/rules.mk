kernel-i386: $(I386_KERNEL)

$(I386_BUILD):
	$(call log_cmd,MKDIR,$@)
	$(Q)mkdir -p $@

$(I386_BUILD)/entry.o: $(ROOT)/arch/x86/i386/entry.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_BUILD)/gdt_flush.o: $(ROOT)/arch/x86/i386/gdt_flush.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_BUILD)/isr.o: $(ROOT)/arch/x86/i386/isr.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_BUILD)/platform_boot.o: $(ROOT)/arch/x86/i386/platform_boot.c $(ROOT)/arch/x86/i386/gdt.h \
		$(ROOT)/arch/x86/i386/idt.h $(ROOT)/arch/x86/i386/keyboard.h \
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/arch/x86/common/pic.h \
		$(ROOT)/arch/x86/i386/pmm.h $(ROOT)/arch/x86/i386/scheduler.h \
		$(ROOT)/kernel/public/core/early_boot.h \
		$(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 \
		-I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/gdt.o: $(ROOT)/arch/x86/i386/gdt.c $(ROOT)/arch/x86/i386/gdt.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/idt.o: $(ROOT)/arch/x86/i386/idt.c $(ROOT)/arch/x86/i386/idt.h \
		$(ROOT)/arch/x86/i386/gdt.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/keyboard.o: $(ROOT)/arch/x86/i386/keyboard.c \
		$(ROOT)/arch/x86/i386/keyboard.h $(ROOT)/arch/x86/common/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/pic.o: $(ROOT)/arch/x86/common/pic.c $(ROOT)/arch/x86/common/pic.h \
		$(ROOT)/arch/x86/common/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/arch_ops.o: $(ROOT)/arch/x86/common/arch_ops.c \
		$(ROOT)/kernel/public/arch/arch_ops.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/paging.o: $(ROOT)/arch/x86/i386/paging.c \
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/arch/x86/i386/pmm.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/pmm.o: $(ROOT)/arch/x86/i386/pmm.c \
		$(ROOT)/arch/x86/i386/pmm.h $(ROOT)/arch/x86/i386/paging.h \
		$(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 \
		-I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/vmm_i386.o: $(ROOT)/arch/x86/i386/vmm.c \
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/kernel/public/mem/vmm.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler.o: $(ROOT)/arch/x86/i386/scheduler.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h $(ROOT)/arch/x86/i386/idt.h \
		$(ROOT)/arch/x86/i386/gdt.h $(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/kernel/internal/proc/process_types_internal.h \
		$(ROOT)/kernel/internal/proc/process_internal_base.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_fpu.o: $(ROOT)/arch/x86/i386/scheduler_fpu.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_backend.o: $(ROOT)/arch/x86/i386/scheduler_backend.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/arch/x86/i386/context.h \
		$(ROOT)/arch/x86/i386/paging.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_file_ops.o: $(ROOT)/arch/x86/i386/scheduler_file_ops.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/kernel/public/proc/process_file_ops.h \
		$(ROOT)/kernel/internal/fs/file_internal.h \
		$(ROOT)/kernel/internal/fs/path_resolve_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_mm_ops.o: $(ROOT)/arch/x86/i386/scheduler_mm_ops.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/pmm.h \
		$(ROOT)/kernel/public/proc/process_mm_ops.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_process_ops.o: $(ROOT)/arch/x86/i386/scheduler_process_ops.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/pmm.h \
		$(ROOT)/kernel/public/proc/process_scheduler_ops.h \
		$(ROOT)/kernel/internal/proc/process_lifecycle_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_run_ops.o: $(ROOT)/arch/x86/i386/scheduler_run_ops.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/kernel/internal/proc/process_lifecycle_internal.h \
		$(ROOT)/kernel/internal/fs/file_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_fault_ops.o: $(ROOT)/arch/x86/i386/scheduler_fault_ops.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/pmm.h \
		$(ROOT)/arch/x86/i386/user.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/scheduler_stats_i386.o: $(ROOT)/arch/x86/i386/scheduler_stats_i386.c \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/kernel/internal/proc/process_lifecycle_internal.h \
		$(ROOT)/kernel/public/proc/sched_policy.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/process32.o: $(ROOT)/arch/x86/i386/process32.c \
		$(ROOT)/arch/x86/i386/process32.h \
		$(ROOT)/kernel/public/proc/process_user_backend.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/process32_user_backend.o: $(ROOT)/arch/x86/i386/process32_user_backend.c \
		$(ROOT)/arch/x86/i386/process32.h \
		$(ROOT)/arch/x86/i386/process32_internal.h \
		$(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/kernel/public/proc/process_scheduler_ops.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/process32_exec.o: $(ROOT)/arch/x86/i386/process32_exec.c \
		$(ROOT)/arch/x86/i386/process32_internal.h \
		$(ROOT)/kernel/internal/proc/process_program_registry_internal.h \
		$(ROOT)/kernel/public/proc/process.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/process32_elf_backend.o: $(ROOT)/arch/x86/i386/process32_elf_backend.c \
		$(ROOT)/arch/x86/i386/process32_internal.h \
		$(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/user.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/process32_address_space.o: $(ROOT)/arch/x86/i386/process32_address_space.c \
		$(ROOT)/kernel/internal/proc/process_internal_base.h \
		$(ROOT)/kernel/internal/mem/address_space_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_current_i386.o: $(ROOT)/arch/x86/i386/process_current_i386.c \
		$(ROOT)/arch/x86/i386/process32.h \
		$(ROOT)/arch/x86/i386/scheduler_internal.h \
		$(ROOT)/kernel/internal/proc/process_lifecycle_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/context.o: $(ROOT)/arch/x86/i386/context.c \
		$(ROOT)/arch/x86/i386/context.h $(ROOT)/arch/x86/i386/idt.h \
		$(ROOT)/kernel/public/proc/context.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_hooks.o: $(ROOT)/arch/x86/i386/process_hooks.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/user.o: $(ROOT)/arch/x86/i386/user.c \
		$(ROOT)/arch/x86/i386/user.h $(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/pmm.h $(ROOT)/fs/early_vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/i386_ops.o: $(ROOT)/arch/x86/i386/ops.c \
		$(ROOT)/arch/x86/i386/idt.h $(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/kernel/public/arch/arch_ops.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/early_boot.o: $(ROOT)/kernel/core/early_boot.c \
		$(ROOT)/kernel/public/core/early_boot.h \
		$(ROOT)/kernel/public/core/early_console.h $(ROOT)/hal/early.h \
		$(ROOT)/lib/string.h $(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/hal_early.o: $(ROOT)/hal/early.c $(ROOT)/hal/early.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/early_console.o: $(ROOT)/kernel/core/early_console.c \
		$(ROOT)/kernel/public/core/early_console.h $(ROOT)/hal/early.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/early_kprint.o: $(ROOT)/kernel/core/early_kprint.c \
		$(ROOT)/kernel/public/core/early_kprint.h \
		$(ROOT)/kernel/public/core/early_console.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/kprint.o: $(ROOT)/kernel/core/kprint.c \
		$(ROOT)/kernel/public/core/kprint.h $(ROOT)/drivers/serial/uart.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/boot_log.o: $(ROOT)/kernel/core/boot_log.c \
		$(ROOT)/kernel/internal/core/boot_log_internal.h \
		$(ROOT)/kernel/public/core/kprint.h $(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/boot_state.o: $(ROOT)/kernel/core/boot_state.c \
		$(ROOT)/kernel/internal/core/boot_state_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/early_runtime_hooks.o: $(ROOT)/kernel/core/early_runtime_hooks.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/kernel_init_flow.o: $(ROOT)/kernel/core/kernel_init_flow.c \
		$(ROOT)/kernel/public/core/kernel_init_flow.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/div64.o: $(ROOT)/user/libc32/std/div64.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT)/user/libc32/include -c $< -o $@

$(I386_BUILD)/clipboard.o: $(ROOT)/kernel/core/clipboard.c \
		$(ROOT)/kernel/internal/core/clipboard_internal.h \
		$(ROOT)/kernel/public/core/console.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/console.o: $(ROOT)/kernel/core/console.c \
		$(ROOT)/kernel/internal/core/console_internal.h $(ROOT)/hal/hal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty.o: $(ROOT)/kernel/core/tty.c \
		$(ROOT)/kernel/internal/core/tty_internal.h \
		$(ROOT)/kernel/internal/core/clipboard_internal.h $(ROOT)/hal/hal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_ansi.o: $(ROOT)/kernel/core/tty_ansi.c \
		$(ROOT)/kernel/internal/core/tty_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_hangul.o: $(ROOT)/kernel/core/tty_hangul.c \
		$(ROOT)/kernel/internal/core/tty_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_input.o: $(ROOT)/kernel/core/tty_input.c \
		$(ROOT)/kernel/internal/core/tty_internal.h \
		$(ROOT)/kernel/internal/core/clipboard_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_line_edit.o: $(ROOT)/kernel/core/tty_line_edit.c \
		$(ROOT)/kernel/internal/core/tty_internal.h \
		$(ROOT)/kernel/internal/core/clipboard_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_queue.o: $(ROOT)/kernel/core/tty_queue.c \
		$(ROOT)/kernel/internal/core/tty_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_utf8.o: $(ROOT)/kernel/core/tty_utf8.c \
		$(ROOT)/kernel/internal/core/tty_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/tty_virtual.o: $(ROOT)/kernel/core/tty_virtual.c \
		$(ROOT)/kernel/internal/core/tty_internal.h $(ROOT)/hal/hal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/graphics_service.o: $(ROOT)/kernel/core/graphics_service.c \
		$(ROOT)/kernel/internal/core/graphics_service_internal.h \
		$(ROOT)/hal/hal.h $(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/profile.o: $(ROOT)/kernel/core/profile.c \
		$(ROOT)/kernel/public/core/profile.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/system_query.o: $(ROOT)/kernel/core/system_query.c \
		$(ROOT)/kernel/internal/core/system_query_internal.h \
		$(ROOT)/drivers/audio/audio.h $(ROOT)/drivers/net/rtl8139.h \
		$(ROOT)/block/blockdev.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/i386_shared_services.o: $(ROOT)/kernel/core/i386_shared_services.c \
		$(ROOT)/kernel/internal/core/tty_internal.h $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/fat32.h $(ROOT)/block/blockdev.h \
		$(ROOT)/drivers/input/keyboard.h $(ROOT)/arch/x86/i386/keyboard.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/i386_smoke_services.o: $(ROOT)/kernel/core/i386_smoke_services.c \
		$(ROOT)/kernel/internal/core/i386_shared_services_internal.h \
		$(ROOT)/kernel/public/proc/boot_user_init.h \
		$(ROOT)/block/blockdev.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/i386_tty_selftest.o: $(ROOT)/kernel/core/i386_tty_selftest.c \
		$(ROOT)/kernel/internal/core/i386_shared_services_internal.h \
		$(ROOT)/kernel/public/core/tty.h \
		$(ROOT)/arch/x86/i386/keyboard.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_compat32.o: $(ROOT)/kernel/sys/syscall_compat32.c \
		$(ROOT)/kernel/public/sys/syscall_compat32.h \
		$(ROOT)/kernel/public/arch/arch_ops.h $(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_common_request_core.o: $(ROOT)/kernel/sys/syscall_common_request_core.c \
		$(ROOT)/kernel/internal/sys/syscall_common_request_core.h \
		$(ROOT)/kernel/public/sys/syscall_request.h \
		$(ROOT)/kernel/internal/core/system_query_internal.h \
		$(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_i386_request_adapter.o: $(ROOT)/kernel/sys/syscall_i386_request_adapter.c \
		$(ROOT)/kernel/public/sys/syscall_compat32.h \
		$(ROOT)/kernel/public/sys/syscall_i386.h \
		$(ROOT)/kernel/public/sys/syscall_request.h \
		$(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_i386_vm_adapter.o: $(ROOT)/kernel/sys/syscall_i386_vm_adapter.c \
		$(ROOT)/kernel/internal/sys/syscall_compat32_internal.h \
		$(ROOT)/kernel/public/sys/syscall_i386.h \
		$(ROOT)/kernel/public/sys/syscall_request.h \
		$(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_i386_job_adapter.o: $(ROOT)/kernel/sys/syscall_i386_job_adapter.c \
		$(ROOT)/kernel/internal/sys/syscall_compat32_internal.h \
		$(ROOT)/kernel/public/sys/syscall_i386.h \
		$(ROOT)/kernel/public/sys/syscall_request.h \
		$(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_i386_mem_ipc_adapter.o: $(ROOT)/kernel/sys/syscall_i386_mem_ipc_adapter.c \
		$(ROOT)/kernel/internal/sys/syscall_compat32_internal.h \
		$(ROOT)/kernel/public/sys/syscall_i386.h \
		$(ROOT)/kernel/public/sys/syscall_request.h \
		$(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_i386_device_ui_adapter.o: $(ROOT)/kernel/sys/syscall_i386_device_ui_adapter.c \
		$(ROOT)/kernel/internal/sys/syscall_compat32_internal.h \
		$(ROOT)/kernel/internal/sys/syscall_common_request_core.h \
		$(ROOT)/kernel/public/sys/syscall_i386.h \
		$(ROOT)/kernel/public/sys/syscall_request.h \
		$(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_compat32_query.o: $(ROOT)/kernel/sys/syscall_compat32_query.c \
		$(ROOT)/kernel/public/sys/syscall_compat32.h \
		$(ROOT)/kernel/public/arch/arch_ops.h $(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_compat32_fs.o: $(ROOT)/kernel/sys/syscall_compat32_fs.c \
		$(ROOT)/kernel/public/sys/syscall_compat32.h \
		$(ROOT)/kernel/public/arch/arch_ops.h $(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_compat32_proc.o: $(ROOT)/kernel/sys/syscall_compat32_proc.c \
		$(ROOT)/kernel/public/sys/syscall_compat32.h \
		$(ROOT)/kernel/public/arch/arch_ops.h \
		$(ROOT)/kernel/public/proc/process.h $(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/boot_user_init.o: $(ROOT)/kernel/proc/boot_user_init.c \
		$(ROOT)/kernel/public/proc/boot_user_init.h \
		$(ROOT)/kernel/public/proc/process.h \
		$(ROOT)/kernel/public/core/tty.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_model.o: $(ROOT)/kernel/proc/process_model.c \
		$(ROOT)/kernel/internal/proc/process_lifecycle_internal.h \
		$(ROOT)/kernel/internal/proc/process_types_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_command.o: $(ROOT)/kernel/proc/process_command.c \
		$(ROOT)/kernel/public/proc/process_command.h \
		$(ROOT)/kernel/public/proc/process.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_program_registry.o: $(ROOT)/kernel/proc/process_program_registry.c \
		$(ROOT)/kernel/internal/proc/process_program_registry_internal.h \
		$(ROOT)/kernel/public/proc/process.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_user_backend.o: $(ROOT)/kernel/proc/process_user_backend.c \
		$(ROOT)/kernel/public/proc/process_user_backend.h \
		$(ROOT)/kernel/public/proc/context.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_mm_ops.o: $(ROOT)/kernel/proc/process_mm_ops.c \
		$(ROOT)/kernel/public/proc/process_mm_ops.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_file_ops.o: $(ROOT)/kernel/proc/process_file_ops.c \
		$(ROOT)/kernel/public/proc/process_file_ops.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/address_space_core.o: $(ROOT)/kernel/mem/address_space_core.c \
		$(ROOT)/kernel/internal/mem/address_space_internal.h \
		$(ROOT)/kernel/public/mem/vmm.h $(ROOT)/kernel/public/mem/pmm.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/pmm_core.o: $(ROOT)/kernel/mem/pmm.c \
		$(ROOT)/kernel/public/mem/pmm.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/vmm_transfer.o: $(ROOT)/kernel/mem/vmm_transfer.c \
		$(ROOT)/kernel/internal/mem/vmm_transfer.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -c $< -o $@

$(I386_BUILD)/process_scheduler_ops.o: $(ROOT)/kernel/sched/process_scheduler_ops.c \
		$(ROOT)/kernel/public/proc/process_scheduler_ops.h \
		$(ROOT)/kernel/public/proc/context.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/sched_policy.o: $(ROOT)/kernel/sched/sched_policy.c \
		$(ROOT)/kernel/public/proc/sched_policy.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/process_context.o: $(ROOT)/kernel/proc/process_context.c \
		$(ROOT)/kernel/public/proc/context.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/runqueue.o: $(ROOT)/kernel/sched/runqueue.c \
		$(ROOT)/kernel/public/proc/runqueue.h \
		$(ROOT)/kernel/internal/proc/process_types_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/driver.o: $(ROOT)/kernel/driver/driver.c \
		$(ROOT)/kernel/public/driver/driver.h \
		$(ROOT)/kernel/internal/driver/driver_loader_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/driver_i386_services.o: $(ROOT)/kernel/driver/driver_i386_services.c \
		$(ROOT)/kernel/public/driver/driver.h \
		$(ROOT)/kernel/public/driver/driver_module.h \
		$(ROOT)/kernel/internal/driver/driver_i386_legacy_internal.h \
		$(ROOT)/kernel/internal/driver/driver_loader_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/driver_i386_legacy.o: $(ROOT)/kernel/driver/driver_i386_legacy.c \
		$(ROOT)/kernel/public/driver/driver.h \
		$(ROOT)/kernel/internal/driver/driver_loader_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/path_resolve.o: $(ROOT)/kernel/fs/path_resolve.c \
		$(ROOT)/kernel/internal/fs/path_resolve_internal.h \
		$(ROOT)/kernel/public/proc/process.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/file.o: $(ROOT)/kernel/fs/file.c \
		$(ROOT)/kernel/internal/fs/file_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/file_backend.o: $(ROOT)/kernel/fs/file_backend.c \
		$(ROOT)/kernel/internal/fs/file_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/file_device_backend.o: \
		$(ROOT)/kernel/fs/file_device_backend.c \
		$(ROOT)/kernel/internal/fs/file_device_backend.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/file_pipe_backend.o: $(ROOT)/kernel/fs/file_pipe_backend.c \
		$(ROOT)/kernel/internal/fs/file_pipe_backend.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/audio.o: $(ROOT)/drivers/audio/audio.c \
		$(ROOT)/drivers/audio/audio.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/pc_speaker.o: $(ROOT)/drivers/audio/pc_speaker.c \
		$(ROOT)/drivers/audio/pc_speaker.h $(ROOT)/hal/hal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/framebuffer.o: $(ROOT)/drivers/video/framebuffer.c \
		$(ROOT)/drivers/video/framebuffer.h $(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/input_keyboard.o: $(ROOT)/drivers/input/keyboard.c \
		$(ROOT)/drivers/input/keyboard.h \
		$(ROOT)/kernel/public/input/keyboard_types.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/mouse.o: $(ROOT)/drivers/input/mouse.c \
		$(ROOT)/drivers/input/mouse.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/usb_%.o: $(ROOT)/drivers/usb/usb_%.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/ehci%.o: $(ROOT)/drivers/usb/ehci%.c \
		$(ROOT)/drivers/usb/ehci_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/ehci.o: $(ROOT)/drivers/usb/ehci.c \
		$(ROOT)/drivers/usb/ehci_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/xhci%.o: $(ROOT)/drivers/usb/xhci%.c \
		$(ROOT)/drivers/usb/xhci_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/xhci.o: $(ROOT)/drivers/usb/xhci.c \
		$(ROOT)/drivers/usb/xhci_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/pci.o: $(ROOT)/drivers/bus/pci.c $(ROOT)/drivers/bus/pci.h \
		$(ROOT)/arch/x86/common/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/cmos.o: $(ROOT)/drivers/rtc/cmos.c $(ROOT)/drivers/rtc/cmos.h \
		$(ROOT)/arch/x86/common/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/ramdisk.o: $(ROOT)/drivers/storage/ramdisk.c \
		$(ROOT)/drivers/storage/ramdisk.h $(ROOT)/block/blockdev.h \
		$(BOOTX_DIR)/include/bootx.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/block_event.o: $(ROOT)/block/block_event.c $(ROOT)/block/block_event.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/blockdev.o: $(ROOT)/block/blockdev.c $(ROOT)/block/blockdev.h \
		$(ROOT)/block/block_event.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/net_event.o: $(ROOT)/drivers/net/net_event.c \
		$(ROOT)/drivers/net/net_event.h \
		$(ROOT)/kernel/public/proc/scheduler.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/rtl8139.o: $(ROOT)/drivers/net/rtl8139.c \
		$(ROOT)/drivers/net/rtl8139.h $(ROOT)/drivers/net/net_event.h \
		$(ROOT)/drivers/bus/pci.h $(ROOT)/hal/hal.h \
		$(ROOT)/kernel/public/mem/pmm.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/uart.o: $(ROOT)/drivers/serial/uart.c $(ROOT)/drivers/serial/uart.h \
		$(ROOT)/arch/x86/common/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/ata.o: $(ROOT)/drivers/storage/ata.c $(ROOT)/drivers/storage/ata.h \
		$(ROOT)/drivers/bus/pci.h $(ROOT)/block/blockdev.h $(ROOT)/hal/hal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/ahci.o: $(ROOT)/drivers/storage/ahci.c $(ROOT)/drivers/storage/ahci.h \
		$(ROOT)/drivers/bus/pci.h $(ROOT)/block/blockdev.h $(ROOT)/hal/hal.h \
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/arch/x86/i386/pmm.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/device_backend_stubs.o: $(ROOT)/drivers/i386/device_backend_stubs.c \
		$(ROOT)/drivers/audio/ac97.h $(ROOT)/drivers/audio/hda.h \
		$(ROOT)/drivers/net/rtl8139.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/fat32_%.o: $(ROOT)/fs/fat32_%.c $(ROOT)/fs/fat32_internal.h \
		$(ROOT)/fs/fat32.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/fat32.o: $(ROOT)/fs/fat32.c $(ROOT)/fs/fat32_internal.h \
		$(ROOT)/fs/fat32.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/nxfs.o: $(ROOT)/fs/nxfs.c $(ROOT)/fs/nxfs_internal.h \
		$(ROOT)/fs/nxfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/nxfs_io.o: $(ROOT)/fs/nxfs_io.c $(ROOT)/fs/nxfs_internal.h \
		$(ROOT)/fs/nxfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/early_vfs.o: $(ROOT)/fs/early_vfs.c $(ROOT)/fs/early_vfs.h \
		$(ROOT)/fs/fat32.h $(ROOT)/block/blockdev.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/vfs.o: $(ROOT)/fs/vfs.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_mount.o: $(ROOT)/fs/vfs_mount.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_path.o: $(ROOT)/fs/vfs_path.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_io.o: $(ROOT)/fs/vfs_io.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_devfs.o: $(ROOT)/fs/vfs_devfs.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h $(ROOT)/block/blockdev.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_procfs.o: $(ROOT)/fs/vfs_procfs.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_procfs_format.o: $(ROOT)/fs/vfs_procfs_format.c \
		$(ROOT)/fs/vfs_internal.h $(ROOT)/fs/vfs_text.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_eventfs.o: $(ROOT)/fs/vfs_eventfs.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_eventfs_format.o: $(ROOT)/fs/vfs_eventfs_format.c \
		$(ROOT)/fs/vfs_internal.h $(ROOT)/fs/vfs_text.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_proc_actions.o: $(ROOT)/fs/vfs_proc_actions.c \
		$(ROOT)/fs/vfs_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/hal_i386.o: $(ROOT)/hal/i386/platform.c $(ROOT)/hal/hal.h \
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/arch/x86/common/pic.h \
		$(ROOT)/arch/x86/i386/keyboard.h $(ROOT)/arch/x86/i386/gdt.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/io.o: $(ROOT)/arch/x86/common/io.c $(ROOT)/arch/x86/common/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/string.o: $(ROOT)/lib/string.c $(ROOT)/lib/string.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_KERNEL): $(I386_OBJS) $(ROOT)/arch/x86/i386/linker.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/arch/x86/i386/linker.ld \
		-o $@ $(I386_OBJS)

$(I386_BUILD)/user_smoke.o: $(ROOT)/user/i386/smoke.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_USER): $(I386_BUILD)/user_smoke.o $(ROOT)/user/i386/linker.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/linker.ld \
		-o $@ $(I386_BUILD)/user_smoke.o

$(I386_BUILD)/user_scheduler.o: $(ROOT)/user/i386/scheduler.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_SCHED_USER): $(I386_BUILD)/user_scheduler.o $(ROOT)/user/i386/scheduler.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/scheduler.ld \
		-o $@ $(I386_BUILD)/user_scheduler.o

$(I386_CRT0): $(ROOT)/user/libc32/crt/crt0.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_BUILD)/libc32_syscall_asm.o: $(ROOT)/user/libc32/sys/syscall.asm | $(I386_BUILD)
	$(call log_cmd,AS,$@)
	$(Q)$(AS) -f elf32 $< -o $@

$(I386_BUILD)/libc32_syscall.o: $(ROOT)/user/libc32/sys/syscall.c \
		$(ROOT)/user/libc32/include/unistd.h $(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_string.o: $(ROOT)/user/libc/std/string.c \
		$(ROOT)/user/libc32/include/string.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_io.o: $(ROOT)/user/libc32/std/io.c \
		$(ROOT)/user/libc32/include/stdio.h $(ROOT)/user/libc32/include/unistd.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_malloc.o: $(ROOT)/user/libc32/std/malloc.c \
		$(ROOT)/user/libc32/include/stdlib.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_stdlib.o: $(ROOT)/user/libc32/std/stdlib.c \
		$(ROOT)/user/libc32/include/stdlib.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_env.o: $(ROOT)/user/libc32/std/env.c \
		$(ROOT)/user/libc32/include/stdlib.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_math.o: $(ROOT)/user/libc32/std/math.c \
		$(ROOT)/user/libc32/include/math.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_printf.o: $(ROOT)/user/libc32/std/printf.c \
		$(ROOT)/user/libc32/include/stdio.h $(ROOT)/user/libc32/include/stdarg.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/libc32_div64.o: $(ROOT)/user/libc32/std/div64.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_NLIBC): $(I386_BUILD)/libc32_syscall_asm.o \
		$(I386_BUILD)/libc32_syscall.o $(I386_BUILD)/libc32_string.o \
		$(I386_BUILD)/libc32_io.o $(I386_BUILD)/libc32_malloc.o \
		$(I386_BUILD)/libc32_stdlib.o $(I386_BUILD)/libc32_env.o \
		$(I386_BUILD)/libc32_math.o $(I386_BUILD)/libc32_printf.o \
		$(I386_BUILD)/libc32_div64.o | $(I386_BUILD)
	$(call log_cmd,AR32,$@)
	$(Q)rm -f $@
	$(Q)$(I386_AR) rcs $@ $^

$(I386_BUILD)/user_test32.o: $(ROOT)/user/i386/test32.c \
		$(ROOT)/user/i386/test32_fs.h \
		$(ROOT)/user/i386/test32_ipc.h \
		$(ROOT)/user/i386/test32_libc.h \
		$(ROOT)/user/i386/test32_mm.h \
		$(ROOT)/user/i386/test32_proc.h \
		$(ROOT)/user/i386/test32_pseudo.h \
		$(ROOT)/user/i386/test32_query.h \
		$(ROOT)/user/i386/test32_sys.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_fs.o: $(ROOT)/user/i386/test32_fs.c \
		$(ROOT)/user/i386/test32_fs.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_ipc.o: $(ROOT)/user/i386/test32_ipc.c \
		$(ROOT)/user/i386/test32_ipc.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_libc.o: $(ROOT)/user/i386/test32_libc.c \
		$(ROOT)/user/i386/test32_libc.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_mm.o: $(ROOT)/user/i386/test32_mm.c \
		$(ROOT)/user/i386/test32_mm.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_proc.o: $(ROOT)/user/i386/test32_proc.c \
		$(ROOT)/user/i386/test32_proc.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_pseudo.o: $(ROOT)/user/i386/test32_pseudo.c \
		$(ROOT)/user/i386/test32_pseudo.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_query.o: $(ROOT)/user/i386/test32_query.c \
		$(ROOT)/user/i386/test32_query.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/user_test32_sys.o: $(ROOT)/user/i386/test32_sys.c \
		$(ROOT)/user/i386/test32_sys.h \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_TEST_USER): $(I386_CRT0) $(I386_BUILD)/user_test32.o \
		$(I386_BUILD)/user_test32_fs.o \
		$(I386_BUILD)/user_test32_ipc.o \
		$(I386_BUILD)/user_test32_libc.o \
		$(I386_BUILD)/user_test32_mm.o \
		$(I386_BUILD)/user_test32_proc.o \
		$(I386_BUILD)/user_test32_pseudo.o \
		$(I386_BUILD)/user_test32_query.o \
		$(I386_BUILD)/user_test32_sys.o \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_BUILD)/user_test32.o \
		$(I386_BUILD)/user_test32_fs.o \
		$(I386_BUILD)/user_test32_ipc.o \
		$(I386_BUILD)/user_test32_libc.o \
		$(I386_BUILD)/user_test32_mm.o \
		$(I386_BUILD)/user_test32_proc.o \
		$(I386_BUILD)/user_test32_pseudo.o \
		$(I386_BUILD)/user_test32_query.o \
		$(I386_BUILD)/user_test32_sys.o $(I386_NLIBC)

$(I386_BUILD)/user_app32.o: $(ROOT)/user/i386/app32.c \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_APP_USER): $(I386_CRT0) $(I386_BUILD)/user_app32.o \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_BUILD)/user_app32.o $(I386_NLIBC)

$(I386_BUILD)/user_nexbox_lite.o: $(ROOT)/user/i386/nexbox_lite.c \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_main.o: $(ROOT)/user/apps/elf/ush.c \
		$(ROOT)/user/apps/elf/ush_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_editor.o: $(ROOT)/user/apps/elf/ush_editor.c \
		$(ROOT)/user/apps/elf/ush_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_vars.o: $(ROOT)/user/apps/elf/ush_vars.c \
		$(ROOT)/user/apps/elf/ush_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_exec.o: $(ROOT)/user/apps/elf/ush_exec.c \
		$(ROOT)/user/apps/elf/ush_shared.h \
		$(ROOT)/user/apps/elf/ush_exec_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_exec_stdio.o: $(ROOT)/user/apps/elf/ush_exec_stdio.c \
		$(ROOT)/user/apps/elf/ush_shared.h \
		$(ROOT)/user/apps/elf/ush_exec_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_exec_dispatch.o: $(ROOT)/user/apps/elf/ush_exec_dispatch.c \
		$(ROOT)/user/apps/elf/ush_shared.h \
		$(ROOT)/user/apps/elf/ush_exec_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_exec_external.o: $(ROOT)/user/apps/elf/ush_exec_external.c \
		$(ROOT)/user/apps/elf/ush_shared.h \
		$(ROOT)/user/apps/elf/ush_exec_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_exec_pipeline.o: $(ROOT)/user/apps/elf/ush_exec_pipeline.c \
		$(ROOT)/user/apps/elf/ush_shared.h \
		$(ROOT)/user/apps/elf/ush_exec_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_exec_redir.o: $(ROOT)/user/apps/elf/ush_exec_redir.c \
		$(ROOT)/user/apps/elf/ush_shared.h \
		$(ROOT)/user/apps/elf/ush_exec_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/ush32_parse.o: $(ROOT)/user/apps/elf/ush_parse.c \
		$(ROOT)/user/apps/elf/ush_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/cmdsuite_i386.o: $(ROOT)/user/apps/elf/nexbox/core/cmdsuite.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/cmdsuite_storage_block_i386.o: $(ROOT)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_block.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/cmdsuite_storage_i386.o: $(ROOT)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/cmdsuite_sysinfo_i386.o: $(ROOT)/user/apps/elf/nexbox/applets/system/cmdsuite_sysinfo.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/cmdsuite_debug_subset_i386.o: $(ROOT)/user/i386/cmdsuite_debug_subset.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/cmdsuite_proc_subset_i386.o: $(ROOT)/user/i386/cmdsuite_proc_subset.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/nexbox32_subset_dispatch.o: $(ROOT)/user/i386/nexbox32_subset_dispatch.c \
		$(ROOT)/user/apps/elf/nexbox/core/cmdsuite_shared.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_BUILD)/full/%.o: $(ROOT)/%.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)mkdir -p $(@D)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

I386_NEXBOX_SUBSET_OBJS := \
	$(I386_BUILD)/cmdsuite_i386.o \
	$(I386_BUILD)/cmdsuite_storage_block_i386.o \
	$(I386_BUILD)/cmdsuite_storage_i386.o \
	$(I386_BUILD)/cmdsuite_sysinfo_i386.o \
	$(I386_BUILD)/cmdsuite_debug_subset_i386.o \
	$(I386_BUILD)/cmdsuite_proc_subset_i386.o \
	$(I386_BUILD)/nexbox32_subset_dispatch.o

I386_USH_OBJS := \
	$(I386_BUILD)/ush32_main.o \
	$(I386_BUILD)/ush32_editor.o \
	$(I386_BUILD)/ush32_vars.o \
	$(I386_BUILD)/ush32_exec.o \
	$(I386_BUILD)/ush32_exec_dispatch.o \
	$(I386_BUILD)/ush32_exec_external.o \
	$(I386_BUILD)/ush32_exec_pipeline.o \
	$(I386_BUILD)/ush32_exec_redir.o \
	$(I386_BUILD)/ush32_exec_stdio.o \
	$(I386_BUILD)/ush32_parse.o

$(I386_NEXBOX_SUBSET_USER): $(I386_CRT0) $(I386_NEXBOX_SUBSET_OBJS) \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_NEXBOX_SUBSET_OBJS) $(I386_NLIBC)

.SECONDEXPANSION:

$(I386_NEXBOX_FULL_USER): $(I386_CRT0) $$(I386_NEXBOX_FULL_OBJS) \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_NEXBOX_FULL_OBJS) $(I386_NLIBC)

$(I386_NEXBOX_USER): $(I386_NEXBOX_FULL_USER)
	$(call log_cmd,COPY32,$@)
	$(Q)cp $(I386_NEXBOX_FULL_USER) $@

.PHONY: nexbox32-full nexbox32-full-symbols
nexbox32-full: $(I386_NEXBOX_FULL_USER)

nexbox32-full-symbols:
	$(call log_cmd,DIAG32,$(I386_NEXBOX_FULL_LOG))
	$(Q)set +e; $(MAKE) --no-print-directory $(I386_NEXBOX_FULL_USER) >$(I386_NEXBOX_FULL_LOG) 2>&1; status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "NEXBOX32 full link passed: $(I386_NEXBOX_FULL_USER)"; \
	else \
		echo "NEXBOX32 full link failed with status $$status"; \
		echo "log: $(I386_NEXBOX_FULL_LOG)"; \
		echo "undefined symbols:"; \
		sed -n "s/.*undefined reference to \`\\([^']*\\)'.*/\\1/p" $(I386_NEXBOX_FULL_LOG) | sort -u; \
		echo "first diagnostics:"; \
		sed -n '1,80p' $(I386_NEXBOX_FULL_LOG); \
	fi

$(I386_USH_USER): $(I386_CRT0) $(I386_USH_OBJS) \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_USH_OBJS) $(I386_NLIBC)

.PHONY: app32
app32: $(I386_CRT0) $(I386_NLIBC) | $(I386_BUILD)
	@if [ -z "$(SRC)" ]; then echo "usage: make app32 SRC=path/to/app.c OUT=APP32.ELF"; exit 1; fi
	@if [ -z "$(OUT)" ]; then echo "usage: make app32 SRC=path/to/app.c OUT=APP32.ELF"; exit 1; fi
	$(call log_cmd,CC32,$(I386_BUILD)/app32_oneoff.o)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $(SRC) -o $(I386_BUILD)/app32_oneoff.o
	$(call log_cmd,LD32,$(I386_BUILD)/$(OUT))
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $(I386_BUILD)/$(OUT) $(I386_CRT0) \
		$(I386_BUILD)/app32_oneoff.o $(I386_NLIBC)

check-i386-elf: $(I386_KERNEL) $(I386_USER) $(I386_SCHED_USER) $(I386_TEST_USER) $(I386_APP_USER) $(I386_NEXBOX_USER) $(I386_NEXBOX_SUBSET_USER) $(I386_USH_USER)
	$(call log_cmd,CHECK,$<)
	$(Q)readelf -h $< | grep -q 'Class:.*ELF32'
	$(Q)readelf -h $< | grep -q 'Machine:.*Intel 80386'
	$(Q)readelf -h $< | grep -q 'Entry point address:.*0x100000'
	$(Q)readelf -h $(I386_USER) | grep -q 'Class:.*ELF32'
	$(Q)readelf -h $(I386_USER) | grep -q 'Machine:.*Intel 80386'
	$(Q)readelf -h $(I386_USER) | grep -q 'Entry point address:.*0x40000000'
	$(Q)readelf -h $(I386_SCHED_USER) | grep -q 'Class:.*ELF32'
	$(Q)readelf -h $(I386_SCHED_USER) | grep -q 'Machine:.*Intel 80386'
	$(Q)readelf -h $(I386_SCHED_USER) | grep -q 'Entry point address:.*0x40010000'
	$(Q)readelf -h $(I386_TEST_USER) | grep -q 'Class:.*ELF32'
	$(Q)readelf -h $(I386_TEST_USER) | grep -q 'Machine:.*Intel 80386'
	$(Q)readelf -h $(I386_TEST_USER) | grep -q 'Entry point address:.*0x40020000'
	$(Q)readelf -h $(I386_APP_USER) | grep -q 'Class:.*ELF32'
	$(Q)readelf -h $(I386_APP_USER) | grep -q 'Machine:.*Intel 80386'
	$(Q)readelf -h $(I386_NEXBOX_USER) | grep -q 'Class:.*ELF32'
	$(Q)readelf -h $(I386_NEXBOX_USER) | grep -q 'Machine:.*Intel 80386'
	$(Q)readelf -h $(I386_USH_USER) | grep -q 'Machine:.*Intel 80386'
	$(Q)echo "i386 boot/x ELF32 kernel checks passed"

check-i386-boot: check-host-tools-image check-host-tools-qemu-i386 check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE)
	$(call log_cmd,QEMU32,$(I386_IMAGE))
	$(Q)rm -f $(I386_BOOT_LOG)
	$(Q)set +e; \
			timeout $(I386_BOOT_TIMEOUT)s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				$(QEMU_NET) \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_IMAGE) \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE); \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 QEMU boot failed with status $$status"; \
			test -f $(I386_BOOT_LOG) && tail -n 80 $(I386_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'i386: architecture bootstrap passed' $(I386_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BOOT_LOG)
	$(Q)grep -q 'rtl8139: controller' $(I386_BOOT_LOG)
	$(Q)grep -q 'block.*RAMDISK IMG' $(I386_BOOT_LOG)
	$(Q)grep -q 'ramdisk: FAT32 /ram mounted' $(I386_BOOT_LOG)
	$(Q)grep -q 'kernel: init starting /system/init' $(I386_BOOT_LOG)
	$(Q)echo "i386 QEMU boot smoke passed ($(I386_BOOT_LOG))"

check-i386: check-host-tools-i386 check-i386-elf

check-i386-smoke: check-i386-boot

include mk/smoke-i386.mk
