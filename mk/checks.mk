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

check-kernel: $(BUILD)/kernel64.elf
	@printf '%s\n' '[check] verifying kernel program headers'
	@readelf -l $(BUILD)/kernel64.elf | grep -q ' RWE ' && { printf '%s\n' '[check] error: kernel64.elf still has an RWX LOAD segment'; exit 1; } || true
	@readelf -l $(BUILD)/kernel64.elf | grep -q 'LOAD'
