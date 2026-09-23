# Common check and governance targets.

check-deps: check-host-tools-x86_64
	@echo "check-deps mode=$(MODE)"
	@test -f $(ROOT)/docs/project_status.md || (echo "missing docs/project_status.md"; exit 1)
	@test -f $(ROOT)/docs/technical_debt.md || (echo "missing docs/technical_debt.md"; exit 1)
	@test -d $(ROOT)/docs/adr || (echo "missing docs/adr/"; exit 1)
	@test -f $(ROOT)/docs/adr/0001-operating-mode.md || (echo "missing docs/adr/0001-operating-mode.md"; exit 1)
	@test -f $(ROOT)/abi/syscall_abi.h || (echo "missing abi/syscall_abi.h"; exit 1)
	@grep -q "^Operating Mode:" $(ROOT)/docs/project_status.md || (echo "project_status.md missing Operating Mode"; exit 1)
	@grep -q "^Applicable Modes:" $(ROOT)/docs/technical_debt.md || (echo "technical_debt.md missing Applicable Modes fields"; exit 1)
	@grep -q '#include "abi/syscall_abi.h"' $(ROOT)/kernel/public/sys/syscall.h || (echo "kernel syscall header must include abi/syscall_abi.h"; exit 1)
	@grep -q '#include "abi/syscall_abi.h"' $(ROOT)/user/public/sysapi.h || (echo "user syscall header must include abi/syscall_abi.h"; exit 1)
	@! rg -n '^(enum syscall_|struct syscall_(request|dirent|process_info|block_info|partition_info|mount_info|boot_info|memmap_info|pmm_info|kmsg_info|pci_info|ac97_info|hda_info|rtl8139_info|rtl8139_rx_info|rtl8139_tx_info|audio_info|audio_play_info|rtc_info|machine_info|block_read_info|block_write_info|program_info|fat_entry_info|root_entry_info|gfx_info|gfx_command))|NOS_ELF_FILE_BUFFER_SIZE|SYS_MAX' $(ROOT)/kernel/public/sys/syscall.h $(ROOT)/user/public/sysapi.h || (echo "syscall ABI belongs in abi/syscall_abi.h"; exit 1)
	@! rg -n '#include "kernel/public/sys/syscall.h"' $(ROOT)/user || (echo "user code must include user/public/sysapi.h, not kernel syscall headers"; exit 1)
	@! rg -n '#include "hal/hal.h"|hal_[A-Za-z0-9_]+[[:space:]]*\(' $(ROOT)/kernel/sys $(ROOT)/kernel/internal/sys || (echo "syscall layer must not call HAL directly"; exit 1)
	@echo "SOSP dependency/governance checks passed"

check: arch-check

check-x86_64: check-deps check-kernel check-image
	@printf '%s\n' '[check] build and image smoke checks passed'

check-all: check-x86_64 check-i386
	@printf '%s\n' '[check] x86_64 and i386 checks passed'

check-stabilization-base: check-x86_64 check-i386 check-kernel
	@printf '%s\n' '[stabilization] base x86_64/i386 build gates passed'

check-stabilization-smoke: check-x86_64-nexbox-full check-x86_64-stress check-i386-smoke check-i386-nexbox32-full
	@printf '%s\n' '[stabilization] x86_64/i386 QEMU smoke gates passed'

check-i386-mm-stress: check-i386-strict-mm check-i386-nexbox32-full
	@printf '%s\n' '[stabilization] i386 strict MM and process lifetime gates passed'

check-pmm-stress: $(BUILD)/pmm_refcount_test
	$(call log_cmd,TEST,$<)
	$(Q)$<

$(BUILD)/pmm_refcount_test: $(ROOT)/tests/pmm_refcount_test.c $(ROOT)/kernel/mem/pmm.c $(ROOT)/kernel/public/mem/pmm.h
	$(call log_cmd,HOSTCC,$@)
	$(Q)mkdir -p $(@D)
	$(Q)$(HOSTCC) -std=c11 -Wall -Wextra -I$(ROOT) -I$(ROOT)/include -I$(JANUS_DIR)/include \
		$(ROOT)/tests/pmm_refcount_test.c $(ROOT)/kernel/mem/pmm.c -o $@

check-pmm-vmm-stress: check-pmm-stress check-x86_64-mm-stress
	@printf '%s\n' '[stabilization] PMM host stress and x86_64 VMM stress passed'

check-stabilization-mm: check-pmm-vmm-stress check-i386-mm-stress
	@printf '%s\n' '[stabilization] x86_64/i386 MM stability gates passed'

check-syscall-invalid: check-x86_64-nexbox-full check-i386-strict-mm
	@printf '%s\n' '[stabilization] syscall invalid-input gates passed'

check-vfs-block-stress: check-x86_64-stress check-i386-nexbox32-full
	@printf '%s\n' '[stabilization] VFS/block query and runtime gates passed'

check-stabilization-driver: check-i386-driver-active check-x86_64-nexbox-full
	@printf '%s\n' '[stabilization] driver/backend smoke gates passed'

BOOT_LOOP_COUNT ?= 10
check-boot-loop:
	@set -e; \
	for i in $$(seq 1 $(BOOT_LOOP_COUNT)); do \
		printf '%s\n' "[stabilization] boot loop $$i/$(BOOT_LOOP_COUNT): x86_64"; \
		$(MAKE) check-x86_64-nexbox-full; \
		printf '%s\n' "[stabilization] boot loop $$i/$(BOOT_LOOP_COUNT): i386"; \
		$(MAKE) check-i386-smoke; \
	done
	@printf '%s\n' '[stabilization] boot loop gate passed'

check-stabilization: check-stabilization-base check-stabilization-smoke check-stabilization-mm check-syscall-invalid check-vfs-block-stress check-stabilization-driver
	@printf '%s\n' '[stabilization] all current stabilization gates passed'

check-kernel: $(BUILD)/kernel64.elf
	@printf '%s\n' '[check] verifying kernel program headers'
	@readelf -l $(BUILD)/kernel64.elf | grep -q ' RWE ' && { printf '%s\n' '[check] error: kernel64.elf still has an RWX LOAD segment'; exit 1; } || true
	@readelf -l $(BUILD)/kernel64.elf | grep -q 'LOAD'
