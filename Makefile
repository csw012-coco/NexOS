# =========================
# 설정
# =========================
ROOT := $(CURDIR)
BUILD := $(ROOT)/build
BOOT := $(ROOT)/boot
BOOTX_DIR := $(ROOT)/bootx/bootx
BOOTX_BUILD := $(BOOTX_DIR)/build
IMAGE := $(ROOT)/NexOS.img
BIOS_IMAGE := $(ROOT)/NexOS-bios.img
UEFI_IMAGE := $(ROOT)/NexOS-uefi.img
NXFS_IMAGE := $(ROOT)/nxfs.img
RAMDISK_IMAGE := $(BUILD)/ramdisk.img
RAMDISK_SIZE ?= 36M
NXFS_TOOL := $(BUILD)/nxfs_host
NXFS_FS := $(BUILD)/nxfs.fs
BOOT_FS_IMAGE := $(BUILD)/boot.fat
ROOT_FS_IMAGE := $(BUILD)/root.nxfs
BOOT_PART_LBA := 2048
ROOT_PART_LBA := 100352
MODE ?= solo

CC := x86_64-elf-gcc
LD := x86_64-elf-ld
AR := x86_64-elf-ar
AS := nasm
HOSTCC := cc
Q ?= @

CFLAGS64 := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -Wall -Wextra -O2 -I$(ROOT)
USERCFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=large -Wall -Wextra -O2 -I$(ROOT)
LDFLAGS64 := -nostdlib -static -m elf_x86_64
USER_ELF_BINS := $(BUILD)/HELLO.ELF $(BUILD)/KEYDEMO.ELF $(BUILD)/YIELDDEMO.ELF $(BUILD)/BADPTR.ELF $(BUILD)/PFDEMO.ELF $(BUILD)/GPFDEMO.ELF $(BUILD)/UDDEMO.ELF $(BUILD)/DEDEMO.ELF $(BUILD)/SLEEPDEMO.ELF $(BUILD)/CATDEMO.ELF $(BUILD)/LSDEMO.ELF $(BUILD)/WDEMO.ELF $(BUILD)/GUIDEMO.ELF $(BUILD)/FORTH.ELF $(BUILD)/USH.ELF $(BUILD)/NEXBOX.ELF
INIT_SCRIPT := $(ROOT)/user/init/INIT.SH
OS_CONFIG := $(ROOT)/config/NOS.CFG
FASM_TEST_SOURCE := $(ROOT)/user/examples/fasm/test.asm
CMD_SUITE_NAMES := NEXBOX HELP ACTIONS ACTION MAPPER ECHO CLEAR PWD TTY ENV FONT WHICH TYPE LS CAT LESS HEXDUMP GREP DATE HWCLOCK SLEEP WATCH ON EVENTS CLIPBOARD WC HEAD TAIL FIND AS PICK SELECT SORT-BY COUNT-BY TO VIEW ED VI VIM TOUCH MV CP MKDIR RMDIR RM ASM STAT DU TREE FILE BLK PARTS FDISK DF MOUNTS PROGS FATLS FATFIND FATREAD CPIO MOUNT UMOUNT HOTPLUG RUN RUNELF RUNBG PS SESSION SERVICE JOBS WAIT ALARM TIMEOUT KILL FG BG SWITCH_ROOT REBOOT DMESG LSPCI AC97 HDA RTL8139 RTL8139TX RTL8139RX ARP ROUTE NETSTAT PING DNS DHCP IFCONFIG HTTP WGET NC AUDIO TONE WAV MPLAY DOCTOR NEXCTL SYSINFO MEMINFO MINFO UNAME CPUINFO CONFIG DBG
QEMU_AUDIODEV ?= pa,id=snd0
QEMU_SERIAL ?= -serial stdio
QEMU_NET ?= -nic user,model=rtl8139
QEMU_NET_TAP_IFNAME ?= tap0
QEMU_NET_TAP_USER ?= $(shell id -un)
QEMU_NET_TAP_BRIDGE ?= br0
QEMU_NET_TAP_SUDO ?= sudo
QEMU_NET_TAP ?= -netdev tap,id=n0,ifname=$(QEMU_NET_TAP_IFNAME),script=no,downscript=no -device rtl8139,netdev=n0
QEMU_NXFS_SATA ?= -drive if=none,id=nxfsdisk,format=raw,file=$(NXFS_IMAGE) -device ich9-ahci,id=ahci -device ide-hd,drive=nxfsdisk,bus=ahci.0
QEMU_USB_MSC ?= -drive if=none,id=usbdisk,format=raw,file=$(NXFS_IMAGE) -device usb-ehci,id=ehci -device usb-storage,drive=usbdisk,bus=ehci.0
QEMU_USB_HID ?= -device usb-kbd,bus=ehci.0 -device usb-mouse,bus=ehci.0
QEMU_XHCI_MSC ?= -drive if=none,id=xhcidisk,format=raw,file=$(NXFS_IMAGE) -device qemu-xhci,id=xhci -device usb-storage,drive=xhcidisk,bus=xhci.0
QEMU_XHCI_HID ?= -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0
OVMF_CODE ?= /usr/share/OVMF/x64/OVMF_CODE.4m.fd
OVMF_VARS_TEMPLATE ?= /usr/share/OVMF/x64/OVMF_VARS.4m.fd
OVMF_VARS_IMAGE := $(BUILD)/OVMF_VARS.fd
BOOTX_STAGE1 := $(BOOTX_BUILD)/stage1.bin
BOOTX_STAGE2 := $(BOOTX_BUILD)/stage2.bin
BOOTX_STAGE3 := $(BOOTX_BUILD)/stage3.sys
BOOTX_UEFI := $(BOOTX_BUILD)/BOOTX64.EFI

KERNEL_C_SRCS := \
	kernel/core/kernel.c \
	kernel/core/kernel_boot.c \
	kernel/core/kernel_init.c \
	kernel/core/kernel_config.c \
	kernel/core/clipboard.c \
	kernel/core/device_poll.c \
	kernel/core/graphics_service.c \
	kernel/core/machine_info.c \
	kernel/core/system_query.c \
	kernel/core/system_power.c \
	kernel/core/kernel_panic.c \
	kernel/core/console.c \
	kernel/core/tty.c \
	kernel/core/kprint.c \
	kernel/fs/file.c \
	kernel/fs/file_backend.c \
	kernel/fs/file_device_backend.c \
	kernel/fs/file_pipe_backend.c \
	kernel/mem/pmm.c \
	kernel/mem/vmm.c \
	kernel/sys/syscall.c \
	kernel/sys/syscall_mem.c \
	kernel/sys/syscall_proc.c \
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
	kernel/proc/process_core.c \
	kernel/proc/process_exec.c \
	kernel/proc/process_program_registry.c \
	kernel/sched/scheduler_core.c \
	kernel/sched/sched_policy.c \
	kernel/proc/job_control.c \
	kernel/proc/process_reap.c \
	kernel/proc/process_session.c \
	kernel/mem/address_space_core.c \
	kernel/proc/process_elf.c \
	arch/x86/gdt64.c \
	arch/x86/paging.c \
	arch/x86/idt64.c \
	block/block_event.c \
	block/blockdev.c \
	drivers/audio/audio.c \
	drivers/bus/pci.c \
	drivers/audio/ac97.c \
	drivers/audio/hda.c \
	drivers/net/net_event.c \
	drivers/net/rtl8139.c \
	drivers/rtc/cmos.c \
	drivers/serial/uart.c \
	drivers/storage/ahci.c \
	drivers/storage/ata.c \
	drivers/storage/ramdisk.c \
	drivers/usb/usb_hid_keymap.c \
	drivers/usb/ehci_core.c \
	drivers/usb/ehci_hid.c \
	drivers/usb/ehci_msc.c \
	drivers/usb/ehci_msc_block.c \
	drivers/usb/ehci_hub.c \
	drivers/usb/ehci.c \
	drivers/usb/xhci_core.c \
	drivers/usb/xhci_hid.c \
	drivers/usb/xhci_msc.c \
	drivers/usb/xhci_msc_block.c \
	drivers/usb/xhci_hub.c \
	drivers/usb/xhci_controller.c \
	drivers/usb/xhci.c \
	fs/fat32_core.c \
	fs/fat32_name.c \
	fs/fat32_dir.c \
	fs/fat32_file.c \
	fs/fat32.c \
	fs/nxfs.c \
	fs/nxfs_io.c \
	fs/vfs.c \
	fs/vfs_path.c \
	fs/vfs_mount.c \
	fs/vfs_devfs.c \
	fs/vfs_procfs.c \
	fs/vfs_procfs_format.c \
	fs/vfs_eventfs.c \
	fs/vfs_eventfs_format.c \
	fs/vfs_proc_actions.c \
	fs/vfs_io.c \
	drivers/video/surface.c \
	drivers/video/framebuffer.c \
	drivers/video/vga.c \
	drivers/input/keyboard.c \
	drivers/input/mouse.c \
	lib/string.c \
	lib/parse.c \
	arch/x86/io.c \
	hal/x86/platform.c \
	hal/x86/cpu.c \
	hal/x86/interrupts.c \
	hal/x86/paging.c

KERNEL_ASM_SRCS := \
	arch/x86/irq_stub64.asm \
	arch/x86/gdt64_flush.asm \
	arch/x86/usermode.asm

USER_NLIBC_C_SRCS := \
	user/libc/sys/syscall.c \
	user/libc/std/string.c \
	user/libc/std/io.c \
	user/libc/std/printf.c \
	user/libc/std/stdio_scan.c \
	user/libc/std/env.c \
	user/libc/std/malloc.c \
	user/libc/std/stdlib.c

USER_NLIBC_ASM_SRCS := \
	user/libc/sys/arch/x86/syscall.S

USER_CRT_C_SRCS := \
	user/libc/crt/libc_start.c

USER_CRT_ASM_SRCS := \
	user/libc/crt/crt0.S

USER_ELF_C_SRCS := \
	user/apps/elf/hello.c \
	user/apps/elf/keydemo.c \
	user/apps/elf/yielddemo.c \
	user/apps/elf/badptr.c \
	user/apps/elf/pfdemo.c \
	user/apps/elf/gpfdemo.c \
	user/apps/elf/uddemo.c \
	user/apps/elf/dedemo.c \
	user/apps/elf/sleepdemo.c \
	user/apps/elf/cat.c \
	user/apps/elf/ls.c \
	user/apps/elf/nexbox/applets/fs/cmd_ls_shared.c \
	user/apps/elf/wdemo.c \
	user/apps/elf/guidemo.c \
	user/apps/elf/forth.c \
	user/apps/elf/ush.c \
	user/apps/elf/ush_editor.c \
	user/apps/elf/ush_vars.c \
	user/apps/elf/ush_exec.c \
	user/apps/elf/ush_parse.c \
	user/apps/elf/nexbox/core/cmdsuite.c \
	user/apps/elf/nexbox/core/cmdsuite_dispatch.c \
	user/apps/elf/nexbox/core/cmdsuite_action.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_basic.c \
	user/apps/elf/nexbox/applets/text/cmdsuite_text.c \
	user/apps/elf/nexbox/applets/text/cmdsuite_text_events.c \
	user/apps/elf/nexbox/applets/text/cmdsuite_text_table.c \
	user/apps/elf/nexbox/applets/audio/cmdsuite_audio.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_arp.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_dns.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_dhcp.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_tcp.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_http.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_rtl8139.c \
	user/apps/elf/nexbox/applets/editor/cmdsuite_editor.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_fdisk.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_block.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_cpio.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_session.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_nexctl.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_sysinfo.c \
	user/apps/elf/nexbox/applets/proc/cmdsuite_proc.c \
	user/apps/elf/nexbox/applets/debug/cmdsuite_debug.c \
	user/apps/elf/nexbox/applets/debug/cmdsuite_debug_doctor.c \
	user/apps/elf/nexbox/applets/asm/cmdsuite_asm.c

KERNEL_C_OBJS := $(addprefix $(BUILD)/,$(KERNEL_C_SRCS:.c=.o))
KERNEL_ASM_OBJS := $(addprefix $(BUILD)/,$(KERNEL_ASM_SRCS:.asm=.o))
USER_NLIBC_C_OBJS := $(addprefix $(BUILD)/,$(USER_NLIBC_C_SRCS:.c=.o))
USER_NLIBC_ASM_OBJS := $(addprefix $(BUILD)/,$(USER_NLIBC_ASM_SRCS:.S=.o))
USER_CRT_C_OBJS := $(addprefix $(BUILD)/,$(USER_CRT_C_SRCS:.c=.o))
USER_CRT_ASM_OBJS := $(addprefix $(BUILD)/,$(USER_CRT_ASM_SRCS:.S=.o))
USER_ELF_C_OBJS := $(addprefix $(BUILD)/,$(USER_ELF_C_SRCS:.c=.o))

OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)
USER_NLIBC_OBJS := $(USER_NLIBC_C_OBJS) $(USER_NLIBC_ASM_OBJS)
USER_NLIBC := $(BUILD)/libnlibc.a
USER_CRT0 := $(BUILD)/user/libc/crt/crt0.o
USER_CRT_START := $(BUILD)/user/libc/crt/libc_start.o
DEPFILES := $(KERNEL_C_OBJS:.o=.d) $(USER_NLIBC_C_OBJS:.o=.d) $(USER_NLIBC_ASM_OBJS:.o=.d) $(USER_CRT_C_OBJS:.o=.d) $(USER_CRT_ASM_OBJS:.o=.d) $(USER_ELF_C_OBJS:.o=.d)

define log_cmd
	$(Q)printf '%-7s %s\n' "$(1)" "$(2)"
endef

define do_cc_kernel
	$(call log_cmd,CC,$@)
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS64) -MMD -MP -MF $(basename $@).d -c $< -o $@
endef

define do_cc_user
	$(call log_cmd,CC,$@)
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(USERCFLAGS) -MMD -MP -MF $(basename $@).d -c $< -o $@
endef

define do_hostcc
	$(call log_cmd,HOSTCC,$@)
	$(Q)$(HOSTCC) -O2 -Wall -Wextra -I$(ROOT) $(shell pkg-config --cflags fuse3) $< -o $@ $(shell pkg-config --libs fuse3)
endef

define do_as
	$(call log_cmd,AS,$@)
	$(Q)mkdir -p $(@D)
	$(Q)$(AS) -f elf64 $< -o $@
endef

define do_ar
	$(call log_cmd,AR,$@)
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $(USER_NLIBC_OBJS)
endef

define do_ld_kernel
	$(call log_cmd,LD,$@)
	$(Q)$(LD) $(LDFLAGS64) -T $(ROOT)/linker.ld -o $@ $(OBJS)
endef

define do_ld_user
	$(call log_cmd,LD,$@)
	$(Q)$(LD) $(LDFLAGS64) -T $(ROOT)/user/apps/elf/user.ld -o $@ $(USER_LD_OBJS)
endef

define define_user_elf
$(BUILD)/$(1): USER_LD_OBJS := $(USER_CRT0) $(USER_CRT_START) $(2) $(USER_NLIBC)
$(BUILD)/$(1): $(2) $(USER_CRT0) $(USER_CRT_START) $(USER_NLIBC) $(ROOT)/user/apps/elf/user.ld | $(BUILD)
	$$(do_ld_user)
endef

all: images $(NXFS_IMAGE)

images: $(IMAGE) $(BIOS_IMAGE) $(UEFI_IMAGE)

.PHONY: all images run dev run-uefi dev-uefi run-ac97 dev-ac97 run-tap dev-tap run-hda-tap dev-hda-tap run-ac97-tap dev-ac97-tap tap-up tap-down clean distclean check check-kernel check-image check-deps oneoff-user
.SILENT:

check-deps:
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

oneoff-user: $(USER_CRT0) $(USER_CRT_START) $(USER_NLIBC) $(ROOT)/user/apps/elf/user.ld | $(BUILD)
	@if [ -z "$(SRC)" ]; then echo "usage: make oneoff-user SRC=./aa.c OUT=AA.ELF"; exit 1; fi
	@if [ -z "$(OUT)" ]; then echo "usage: make oneoff-user SRC=./aa.c OUT=AA.ELF"; exit 1; fi
	$(call log_cmd,CC,$(BUILD)/oneoff_user.o)
	$(Q)$(CC) $(USERCFLAGS) -c $(SRC) -o $(BUILD)/oneoff_user.o
	$(call log_cmd,LD,$(BUILD)/$(OUT))
	$(Q)$(LD) $(LDFLAGS64) -T $(ROOT)/user/apps/elf/user.ld -o $(BUILD)/$(OUT) $(USER_CRT0) $(USER_CRT_START) $(BUILD)/oneoff_user.o $(USER_NLIBC)

$(RAMDISK_IMAGE): $(USER_ELF_BINS) $(ROOT)/bootx.cfg $(ROOT)/font.hex $(INIT_SCRIPT) $(OS_CONFIG) $(FASM_TEST_SOURCE) $(ROOT)/config/ACTION.CAPS | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s $(RAMDISK_SIZE) $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary fat32 1MiB 100%
	$(Q)mkfs.fat -F 32 --offset 2048 $@
	$(Q)mmd -i $@@@1048576 ::/HOME
	$(Q)mmd -i $@@@1048576 ::/CMD
	$(Q)mcopy -i $@@@1048576 $(ROOT)/bootx.cfg ::/HOME/BOOTX.TXT
	$(Q)mcopy -i $@@@1048576 $(BUILD)/HELLO.ELF ::/HOME/HELLO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/KEYDEMO.ELF ::/HOME/KEYDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/YIELDDEMO.ELF ::/HOME/YIELDDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/BADPTR.ELF ::/HOME/BADPTR.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/PFDEMO.ELF ::/HOME/PFDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/GPFDEMO.ELF ::/HOME/GPFDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/UDDEMO.ELF ::/HOME/UDDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/DEDEMO.ELF ::/HOME/DEDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/SLEEPDEMO.ELF ::/HOME/SLEEPDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/CATDEMO.ELF ::/HOME/CATDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/LSDEMO.ELF ::/HOME/LSDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/WDEMO.ELF ::/HOME/WDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/GUIDEMO.ELF ::/HOME/GUIDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/FORTH.ELF ::/HOME/FORTH.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/USH.ELF ::/HOME/USH.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/NEXBOX.ELF ::/HOME/NEXBOX.ELF
	$(Q)mcopy -i $@@@1048576 $(ROOT)/config/ACTION.CAPS ::/HOME/ACTION.CAPS
	$(Q)mcopy -i $@@@1048576 $(FASM_TEST_SOURCE) ::/HOME/TEST.ASM
	$(Q)mcopy -i $@@@1048576 $(INIT_SCRIPT) ::/INIT.SH
	$(Q)mcopy -i $@@@1048576 $(OS_CONFIG) ::/NOS.CFG
	$(Q)mcopy -i $@@@1048576 $(BUILD)/HELLO.ELF ::/CMD/HELLO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/KEYDEMO.ELF ::/CMD/KEYDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/YIELDDEMO.ELF ::/CMD/YIELDDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/BADPTR.ELF ::/CMD/BADPTR
	$(Q)mcopy -i $@@@1048576 $(BUILD)/PFDEMO.ELF ::/CMD/PFDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/GPFDEMO.ELF ::/CMD/GPFDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/UDDEMO.ELF ::/CMD/UDDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/DEDEMO.ELF ::/CMD/DEDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/SLEEPDEMO.ELF ::/CMD/SLEEPDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/CATDEMO.ELF ::/CMD/CAT
	$(Q)mcopy -i $@@@1048576 $(BUILD)/LSDEMO.ELF ::/CMD/LS
	$(Q)mcopy -i $@@@1048576 $(BUILD)/WDEMO.ELF ::/CMD/WDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/GUIDEMO.ELF ::/CMD/GUIDEMO
	$(Q)mcopy -i $@@@1048576 $(BUILD)/FORTH.ELF ::/CMD/FORTH
	$(Q)mcopy -i $@@@1048576 $(BUILD)/USH.ELF ::/CMD/USH
	$(Q)for alias in $(CMD_SUITE_NAMES); do mcopy -o -i $@@@1048576 $(BUILD)/NEXBOX.ELF ::/CMD/$$alias; done

$(BUILD):
	$(call log_cmd,MKDIR,$@)
	$(Q)mkdir -p $(BUILD)

$(NXFS_TOOL): $(ROOT)/tools/nxfs_host.c $(ROOT)/fs/nxfs.c $(ROOT)/fs/nxfs_io.c $(ROOT)/fs/nxfs_internal.h $(ROOT)/fs/nxfs.h $(ROOT)/kernel/public/fs/nxfs_types.h $(ROOT)/lib/string.c | $(BUILD)
	$(call log_cmd,HOSTCC,$@)
	$(Q)$(HOSTCC) -O2 -Wall -Wextra -fno-builtin -I$(ROOT) $(ROOT)/tools/nxfs_host.c $(ROOT)/fs/nxfs.c $(ROOT)/fs/nxfs_io.c $(ROOT)/lib/string.c -o $@

$(NXFS_FS): $(NXFS_TOOL)
	$(call log_cmd,GEN,$@)
	$(Q)rm -f $@
	$(Q)$< mkfs $@

$(BOOT_FS_IMAGE): $(BOOTX_STAGE3) $(BOOTX_UEFI) $(BUILD)/kernel64.elf $(RAMDISK_IMAGE) $(ROOT)/bootx.cfg $(ROOT)/font.hex | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s 48M $@
	$(Q)mkfs.fat -F 32 $@
	$(Q)mmd -i $@ ::/BOOT
	$(Q)mmd -i $@ ::/EFI
	$(Q)mmd -i $@ ::/EFI/BOOT
	$(Q)mcopy -i $@ $(BOOTX_STAGE3) ::/BOOT/STAGE3.SYS
	$(Q)mcopy -i $@ $(BOOTX_UEFI) ::/EFI/BOOT/BOOTX64.EFI
	$(Q)mcopy -i $@ $(BUILD)/kernel64.elf ::/BOOT/NEX.ELF
	$(Q)mcopy -i $@ $(RAMDISK_IMAGE) ::/BOOT/RAMDISK.IMG
	$(Q)mcopy -i $@ $(ROOT)/font.hex ::/BOOT/FONT.HEX
	$(Q)mcopy -i $@ $(ROOT)/bootx.cfg ::/BOOT/BOOTX.CFG

$(ROOT_FS_IMAGE): $(NXFS_TOOL) $(USER_ELF_BINS) $(ROOT)/bootx.cfg $(ROOT)/font.hex $(INIT_SCRIPT) $(OS_CONFIG) $(FASM_TEST_SOURCE) $(ROOT)/config/ACTION.CAPS | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)$(NXFS_TOOL) mkfs $@
	$(Q)$(NXFS_TOOL) mkdir $@ /cmd
	$(Q)$(NXFS_TOOL) mkdir $@ /home
	$(Q)$(NXFS_TOOL) mkdir $@ /system
	$(Q)$(NXFS_TOOL) mkdir $@ /system/font
	$(Q)$(NXFS_TOOL) mkdir $@ /system/config
	$(Q)$(NXFS_TOOL) mkdir $@ /system/session
	$(Q)$(NXFS_TOOL) mkdir $@ /system/session/images
	$(Q)$(NXFS_TOOL) mkdir $@ /system/service
	$(Q)$(NXFS_TOOL) write $@ $(INIT_SCRIPT) /init.sh
	$(Q)$(NXFS_TOOL) write $@ $(OS_CONFIG) /nos.cfg
	$(Q)$(NXFS_TOOL) write $@ $(OS_CONFIG) /system/config/nos.cfg
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/font.hex /system/font/font.hex
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/bootx.cfg /home/bootx.txt
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/config/ACTION.CAPS /home/action.caps
	$(Q)$(NXFS_TOOL) write $@ $(FASM_TEST_SOURCE) /home/test.asm
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/HELLO.ELF /home/hello.elf
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/GUIDEMO.ELF /home/guidemo.elf
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/FORTH.ELF /home/forth.elf
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/USH.ELF /home/ush.elf
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/NEXBOX.ELF /home/nexbox.elf
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/USH.ELF /cmd/ush
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/NEXBOX.ELF /cmd/nexbox
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/HELLO.ELF /cmd/hello
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/GUIDEMO.ELF /cmd/guidemo
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/FORTH.ELF /cmd/forth
	$(Q)for alias in $(CMD_SUITE_NAMES); do lower=$$(printf '%s' "$$alias" | tr 'A-Z' 'a-z'); $(NXFS_TOOL) write $@ $(BUILD)/NEXBOX.ELF /cmd/$$lower; done

$(NXFS_IMAGE): $(NXFS_FS)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s 80M $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary 1MiB 100%
	$(Q)dd if=$(NXFS_FS) of=$@ conv=notrunc bs=512 seek=2048

$(BUILD)/%.o: $(ROOT)/%.c | $(BUILD)
	$(if $(filter user/libc/% user/apps/elf/%,$*),$(do_cc_user),$(do_cc_kernel))

$(BUILD)/%.o: $(ROOT)/%.S | $(BUILD)
	$(do_cc_user)

$(BUILD)/%.o: $(ROOT)/%.asm | $(BUILD)
	$(do_as)

$(USER_NLIBC): $(USER_NLIBC_OBJS) | $(BUILD)
	$(do_ar)

$(BUILD)/kernel64.elf: $(OBJS) $(ROOT)/linker.ld
	$(do_ld_kernel)

HELLO_ELF_OBJS := $(BUILD)/user/apps/elf/hello.o
KEYDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/keydemo.o
YIELDDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/yielddemo.o
BADPTR_ELF_OBJS := $(BUILD)/user/apps/elf/badptr.o
PFDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/pfdemo.o
GPFDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/gpfdemo.o
UDDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/uddemo.o
DEDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/dedemo.o
SLEEPDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/sleepdemo.o
CATDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/cat.o
LSDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/ls.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmd_ls_shared.o
WDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/wdemo.o
GUIDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/guidemo.o
FORTH_ELF_OBJS := $(BUILD)/user/apps/elf/forth.o
USH_ELF_OBJS := $(BUILD)/user/apps/elf/ush.o $(BUILD)/user/apps/elf/ush_editor.o $(BUILD)/user/apps/elf/ush_vars.o $(BUILD)/user/apps/elf/ush_exec.o $(BUILD)/user/apps/elf/ush_parse.o
NEXBOX_ELF_OBJS := $(BUILD)/user/apps/elf/nexbox/core/cmdsuite.o $(BUILD)/user/apps/elf/nexbox/core/cmdsuite_dispatch.o $(BUILD)/user/apps/elf/nexbox/core/cmdsuite_action.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_basic.o $(BUILD)/user/apps/elf/nexbox/applets/text/cmdsuite_text.o $(BUILD)/user/apps/elf/nexbox/applets/text/cmdsuite_text_events.o $(BUILD)/user/apps/elf/nexbox/applets/text/cmdsuite_text_table.o $(BUILD)/user/apps/elf/nexbox/applets/audio/cmdsuite_audio.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_arp.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_dns.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_dhcp.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_tcp.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_http.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_rtl8139.o $(BUILD)/user/apps/elf/nexbox/applets/editor/cmdsuite_editor.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_fdisk.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_block.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_cpio.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_session.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_nexctl.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_sysinfo.o $(BUILD)/user/apps/elf/nexbox/applets/proc/cmdsuite_proc.o $(BUILD)/user/apps/elf/nexbox/applets/debug/cmdsuite_debug.o $(BUILD)/user/apps/elf/nexbox/applets/debug/cmdsuite_debug_doctor.o $(BUILD)/user/apps/elf/nexbox/applets/asm/cmdsuite_asm.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmd_ls_shared.o

$(eval $(call define_user_elf,HELLO.ELF,$(HELLO_ELF_OBJS)))
$(eval $(call define_user_elf,KEYDEMO.ELF,$(KEYDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,YIELDDEMO.ELF,$(YIELDDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,BADPTR.ELF,$(BADPTR_ELF_OBJS)))
$(eval $(call define_user_elf,PFDEMO.ELF,$(PFDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,GPFDEMO.ELF,$(GPFDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,UDDEMO.ELF,$(UDDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,DEDEMO.ELF,$(DEDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,SLEEPDEMO.ELF,$(SLEEPDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,CATDEMO.ELF,$(CATDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,LSDEMO.ELF,$(LSDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,WDEMO.ELF,$(WDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,GUIDEMO.ELF,$(GUIDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,FORTH.ELF,$(FORTH_ELF_OBJS)))
$(eval $(call define_user_elf,USH.ELF,$(USH_ELF_OBJS)))
$(eval $(call define_user_elf,NEXBOX.ELF,$(NEXBOX_ELF_OBJS)))

-include $(DEPFILES)

$(IMAGE): $(BOOTX_STAGE1) $(BOOTX_STAGE2) $(BOOT_FS_IMAGE) $(ROOT_FS_IMAGE) | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s 128M $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary fat32 1MiB 49MiB
	$(Q)parted -s $@ mkpart primary 49MiB 100%
	$(Q)parted -s $@ set 1 boot on
	$(Q)dd if=$(BOOTX_STAGE1) of=$@ conv=notrunc bs=446 count=1
	$(Q)dd if=$(BOOTX_STAGE1) of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	$(Q)dd if=$(BOOTX_STAGE2) of=$@ conv=notrunc bs=512 seek=1
	$(Q)dd if=$(BOOT_FS_IMAGE) of=$@ conv=notrunc bs=512 seek=$(BOOT_PART_LBA)
	$(Q)dd if=$(ROOT_FS_IMAGE) of=$@ conv=notrunc bs=512 seek=$(ROOT_PART_LBA)

$(BIOS_IMAGE): $(IMAGE) | $(BUILD)
	$(call log_cmd,CP,$@)
	$(Q)cp $(IMAGE) $@

$(UEFI_IMAGE): $(BOOT_FS_IMAGE) $(ROOT_FS_IMAGE) | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s 128M $@
	$(Q)parted -s $@ mklabel gpt
	$(Q)parted -s $@ mkpart ESP fat32 1MiB 49MiB
	$(Q)parted -s $@ set 1 esp on
	$(Q)parted -s $@ mkpart NexOS 49MiB 100%
	$(Q)dd if=$(BOOT_FS_IMAGE) of=$@ conv=notrunc bs=512 seek=$(BOOT_PART_LBA)
	$(Q)dd if=$(ROOT_FS_IMAGE) of=$@ conv=notrunc bs=512 seek=$(ROOT_PART_LBA)

$(OVMF_VARS_IMAGE): $(OVMF_VARS_TEMPLATE) | $(BUILD)
	$(call log_cmd,CP,$@)
	$(Q)cp $< $@

run: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_NXFS_SATA) \
		-device AC97,audiodev=snd0 \
		-audiodev $(QEMU_AUDIODEV)

run-uefi: $(UEFI_IMAGE) $(NXFS_IMAGE) $(OVMF_VARS_IMAGE)
	qemu-system-x86_64 \
	-machine q35 \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(OVMF_VARS_IMAGE) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(UEFI_IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-uefi: $(UEFI_IMAGE) $(NXFS_IMAGE) $(OVMF_VARS_IMAGE)
	qemu-system-x86_64 \
	-machine q35 \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(OVMF_VARS_IMAGE) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(UEFI_IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-hda: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-hda: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_NXFS_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-ac97: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-usb: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_USB_MSC) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-usb: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_USB_MSC) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-usb-hid: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_USB_MSC) \
	$(QEMU_USB_HID) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-xhci: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_XHCI_MSC) \
	$(QEMU_XHCI_HID) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-ac97: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-tap: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-tap: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-hda-tap: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-hda-tap: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_NXFS_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-ac97-tap: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64 \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-ac97-tap: $(IMAGE) $(NXFS_IMAGE)
	qemu-system-x86_64  \
	-no-reboot \
	-no-shutdown \
	-d int,cpu_reset \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

tap-up:
	@$(QEMU_NET_TAP_SUDO) ip link show $(QEMU_NET_TAP_BRIDGE) >/dev/null 2>&1 || { printf '%s\n' "[tap-up] error: bridge $(QEMU_NET_TAP_BRIDGE) not found"; exit 1; }
	@$(QEMU_NET_TAP_SUDO) ip link show $(QEMU_NET_TAP_IFNAME) >/dev/null 2>&1 || $(QEMU_NET_TAP_SUDO) ip tuntap add dev $(QEMU_NET_TAP_IFNAME) mode tap user $(QEMU_NET_TAP_USER)
	@$(QEMU_NET_TAP_SUDO) ip link set $(QEMU_NET_TAP_IFNAME) master $(QEMU_NET_TAP_BRIDGE)
	@$(QEMU_NET_TAP_SUDO) ip link set $(QEMU_NET_TAP_IFNAME) up
	@printf '%s\n' "[tap-up] attached $(QEMU_NET_TAP_IFNAME) to $(QEMU_NET_TAP_BRIDGE) for user $(QEMU_NET_TAP_USER)"

tap-down:
	@$(QEMU_NET_TAP_SUDO) ip link show $(QEMU_NET_TAP_IFNAME) >/dev/null 2>&1 || { printf '%s\n' "[tap-down] $(QEMU_NET_TAP_IFNAME) does not exist"; exit 0; }
	@$(QEMU_NET_TAP_SUDO) ip link set $(QEMU_NET_TAP_IFNAME) down || true
	@$(QEMU_NET_TAP_SUDO) ip link delete $(QEMU_NET_TAP_IFNAME) || true
	@printf '%s\n' "[tap-down] removed $(QEMU_NET_TAP_IFNAME)"

check: check-deps check-kernel check-image
	@printf '%s\n' '[check] build and image smoke checks passed'

check-kernel: $(BUILD)/kernel64.elf
	@printf '%s\n' '[check] verifying kernel program headers'
	@readelf -l $(BUILD)/kernel64.elf | grep -q ' RWE ' && { printf '%s\n' '[check] error: kernel64.elf still has an RWX LOAD segment'; exit 1; } || true
	@readelf -l $(BUILD)/kernel64.elf | grep -q 'LOAD'

check-image: $(IMAGE) $(BIOS_IMAGE) $(UEFI_IMAGE) $(NXFS_IMAGE) $(ROOT_FS_IMAGE)
	@printf '%s\n' '[check] verifying BIOS boot image contents'
	@mdir -i $(IMAGE)@@1048576 ::/BOOT | grep -Eq 'NEX +ELF'
	@mdir -i $(IMAGE)@@1048576 ::/BOOT | grep -Eq 'STAGE3 +SYS'
	@mdir -i $(IMAGE)@@1048576 ::/BOOT | grep -Eq 'RAMDISK +IMG'
	@mdir -i $(IMAGE)@@1048576 ::/BOOT | grep -Eq 'FONT +HEX'
	@mdir -i $(IMAGE)@@1048576 ::/BOOT | grep -Eq 'BOOTX +CFG'
	@mdir -i $(IMAGE)@@1048576 ::/EFI/BOOT | grep -Eq 'BOOTX64 +EFI'
	@printf '%s\n' '[check] verifying named BIOS image'
	@mdir -i $(BIOS_IMAGE)@@1048576 ::/BOOT | grep -Eq 'NEX +ELF'
	@printf '%s\n' '[check] verifying UEFI boot image contents'
	@parted -s $(UEFI_IMAGE) print | grep -q 'Partition Table: gpt'
	@parted -s $(UEFI_IMAGE) print | grep -q 'esp'
	@mdir -i $(UEFI_IMAGE)@@1048576 ::/EFI/BOOT | grep -Eq 'BOOTX64 +EFI'
	@mdir -i $(UEFI_IMAGE)@@1048576 ::/BOOT | grep -Eq 'NEX +ELF'
	@printf '%s\n' '[check] verifying root NXFS contents'
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /init.sh
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/ush
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/nexbox
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/hello
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/ls
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/echo
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/vi
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/nexctl
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /cmd/sysinfo
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/ush.elf
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/hello.elf
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/config/nos.cfg
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/font/font.hex
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/session/images
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/service
	@printf '%s\n' '[check] verifying ramdisk contents'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/HOME | grep -Eq 'USH +ELF'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/HOME | grep -Eq 'HELLO +ELF'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/CMD | grep -Ei 'MKDIR'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/CMD | grep -Ei 'SERVICE'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/CMD | grep -Ei 'NEXCTL'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/CMD | grep -Ei 'SYSINFO'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/ | grep -Eq 'INIT +SH'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/ | grep -Eq 'NOS +CFG'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/ | grep -Eq 'USH +ELF' && { printf '%s\n' '[check] error: unexpected root /USH.ELF copy in ramdisk'; exit 1; } || true

clean:
	rm -rf $(BUILD) $(IMAGE) $(BIOS_IMAGE) $(UEFI_IMAGE)

distclean: clean
	rm -rf $(NXFS_IMAGE)
.PHONY: bootx-loader

bootx-loader:
	$(Q)$(MAKE) -C $(BOOTX_DIR)

$(BOOTX_STAGE1) $(BOOTX_STAGE2) $(BOOTX_STAGE3) $(BOOTX_UEFI): bootx-loader
