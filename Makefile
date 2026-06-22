# =========================
# 설정
# =========================
ROOT := $(CURDIR)
BUILD := $(ROOT)/build
ASSETS := $(ROOT)/assets
DOOM_ASSETS := $(ASSETS)/doom
DOOM1_WAD := $(DOOM_ASSETS)/DOOM1.WAD
DOOM2_WAD := $(DOOM_ASSETS)/DOOM2.WAD
TEST_C_SOURCE := $(ASSETS)/c/test.c
TEST_WAV := $(ASSETS)/audio/test.wav
TEST2_WAV := $(ASSETS)/audio/test2.wav
FONT_HEX := $(ASSETS)/fonts/font.hex
IMAGE_DIR := $(BUILD)/images
CMD_SUITE_WRAPPER_DIR := $(BUILD)/cmd-wrappers
BOOT := $(ROOT)/boot
BOOTX_DIR := $(ROOT)/bootloader/bootx
BOOTX_BUILD := $(BOOTX_DIR)/build
BOOTX_CONFIG := $(ROOT)/config/bootx.cfg
IMAGE := $(IMAGE_DIR)/NexOS.img
BIOS_IMAGE := $(IMAGE_DIR)/NexOS-bios.img
UEFI_IMAGE := $(IMAGE_DIR)/NexOS-uefi.img
OS_IMAGE_SIZE ?= 256M
NXFS_IMAGE := $(IMAGE_DIR)/nxfs.img
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

I386_CC ?= cc
I386_LD ?= ld
I386_AR ?= ar
I386_BUILD := $(BUILD)/i386
I386_KERNEL := $(I386_BUILD)/kernel-i386.elf
I386_USER := $(I386_BUILD)/USER32.ELF
I386_SCHED_USER := $(I386_BUILD)/SCHED32.ELF
I386_TEST_USER := $(I386_BUILD)/TEST32.ELF
I386_APP_USER := $(I386_BUILD)/APP32.ELF
I386_NEXBOX_USER := $(I386_BUILD)/NEXBOX32.ELF
I386_NLIBC := $(I386_BUILD)/libnlibc32.a
I386_CRT0 := $(I386_BUILD)/libc32_crt0.o
I386_IMAGE := $(IMAGE_DIR)/NexOS-i386.img
I386_BOOTX_CONFIG := $(ROOT)/config/bootx-i386.cfg
I386_CFLAGS := -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections \
	-fdata-sections -Wall -Wextra -O2 \
	-I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/include
I386_LDFLAGS := -m elf_i386 -nostdlib -static --gc-sections
I386_USER_CFLAGS := -I$(ROOT)/user/libc32/include \
	$(I386_CFLAGS) -fno-builtin
I386_COMMON_C_SRCS := \
	hal/early.c \
	kernel/core/early_console.c \
	kernel/core/early_kprint.c \
	kernel/core/early_boot.c \
	kernel/core/early_runtime_hooks.c \
	kernel/core/clipboard.c \
	kernel/core/console.c \
	kernel/core/tty.c \
	kernel/core/i386_shared_services.c \
	kernel/sys/syscall_dispatch.c \
	drivers/input/keyboard.c \
	drivers/bus/pci.c \
	block/block_event.c \
	block/blockdev.c \
	fs/fat32_core.c \
	fs/fat32_name.c \
	fs/fat32_dir.c \
	fs/fat32_file.c \
	fs/fat32.c \
	fs/early_vfs.c \
	fs/vfs.c \
	fs/vfs_i386.c \
	lib/string.c
I386_ARCH_C_SRCS := \
	arch/x86/i386/gdt.c \
	arch/x86/i386/idt.c \
	arch/x86/i386/keyboard.c \
	arch/x86/i386/pic.c \
	arch/x86/i386/paging.c \
	arch/x86/i386/pmm.c \
	arch/x86/i386/scheduler.c \
	arch/x86/i386/user.c \
	hal/i386/platform.c \
	arch/x86/i386/platform_boot.c
I386_ARCH_ASM_SRCS := \
	arch/x86/i386/entry.asm \
	arch/x86/i386/gdt_flush.asm \
	arch/x86/i386/isr.asm
I386_COMMON_OBJS := \
	$(I386_BUILD)/hal_early.o \
	$(I386_BUILD)/early_console.o \
	$(I386_BUILD)/early_kprint.o \
	$(I386_BUILD)/early_boot.o \
	$(I386_BUILD)/early_runtime_hooks.o \
	$(I386_BUILD)/clipboard.o \
	$(I386_BUILD)/console.o \
	$(I386_BUILD)/tty.o \
	$(I386_BUILD)/i386_shared_services.o \
	$(I386_BUILD)/syscall_dispatch.o \
	$(I386_BUILD)/input_keyboard.o \
	$(I386_BUILD)/pci.o \
	$(I386_BUILD)/block_event.o \
	$(I386_BUILD)/blockdev.o \
	$(I386_BUILD)/ata.o \
	$(I386_BUILD)/fat32_core.o \
	$(I386_BUILD)/fat32_name.o \
	$(I386_BUILD)/fat32_dir.o \
	$(I386_BUILD)/fat32_file.o \
	$(I386_BUILD)/fat32.o \
	$(I386_BUILD)/early_vfs.o \
	$(I386_BUILD)/vfs.o \
	$(I386_BUILD)/vfs_i386.o \
	$(I386_BUILD)/io.o \
	$(I386_BUILD)/string.o
I386_ARCH_C_OBJS := \
	$(I386_BUILD)/gdt.o \
	$(I386_BUILD)/idt.o \
	$(I386_BUILD)/keyboard.o \
	$(I386_BUILD)/pic.o \
	$(I386_BUILD)/paging.o \
	$(I386_BUILD)/pmm.o \
	$(I386_BUILD)/scheduler.o \
	$(I386_BUILD)/user.o \
	$(I386_BUILD)/hal_i386.o \
	$(I386_BUILD)/platform_boot.o
I386_ARCH_ASM_OBJS := \
	$(I386_BUILD)/entry.o \
	$(I386_BUILD)/gdt_flush.o \
	$(I386_BUILD)/isr.o
I386_OBJS := $(I386_ARCH_ASM_OBJS) $(I386_ARCH_C_OBJS) $(I386_COMMON_OBJS)


CFLAGS64 := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -Wall -Wextra -O2 -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include
DRV_CFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=large -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -O2 -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include
USERCFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=large -Wall -Wextra -O2 -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT)/user/libc/include -I$(ROOT)/user/libc/include/sys -I$(ROOT)/user/libc/include/nexos -I$(ROOT)/user/public -I$(ROOT)/abi
LDFLAGS64 := -nostdlib -static -m elf_x86_64
USER_ELF_BINS := $(BUILD)/DOOM.ELF $(BUILD)/IPCDEMO.ELF $(BUILD)/IMGVIEW.ELF $(BUILD)/NCC.ELF $(BUILD)/HELLO.ELF $(BUILD)/KEYDEMO.ELF $(BUILD)/YIELDDEMO.ELF $(BUILD)/BADPTR.ELF $(BUILD)/PFDEMO.ELF $(BUILD)/GPFDEMO.ELF $(BUILD)/UDDEMO.ELF $(BUILD)/DEDEMO.ELF $(BUILD)/SLEEPDEMO.ELF $(BUILD)/CATDEMO.ELF $(BUILD)/LSDEMO.ELF $(BUILD)/WDEMO.ELF $(BUILD)/GUIDEMO.ELF $(BUILD)/FORTH.ELF $(BUILD)/USH.ELF $(BUILD)/NEXBOX.ELF
INIT_SCRIPT := $(ROOT)/user/init/INIT.SH
OS_CONFIG := $(ROOT)/config/NOS.CFG
DUMMY_AC97_DRIVER_SRC := $(ROOT)/drivers/dummy/ac97_drv.c
DUMMY_AC97_DRIVER := $(BUILD)/AC97.DRV
DUMMY_HDA_DRIVER_SRC := $(ROOT)/drivers/dummy/hda_drv.c
DUMMY_HDA_DRIVER := $(BUILD)/HDA.DRV
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
QEMU_UEFI_SATA ?= -drive if=none,id=uefiboot,format=raw,file=$(UEFI_IMAGE) -drive if=none,id=uefinxfs,format=raw,file=$(NXFS_IMAGE) -device ich9-ahci,id=uefiahci -device ide-hd,drive=uefiboot,bus=uefiahci.0 -device ide-hd,drive=uefinxfs,bus=uefiahci.1
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
	kernel/core/profile.c \
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
	kernel/proc/process_core.c \
	kernel/proc/process_exec.c \
	kernel/proc/process_program_registry.c \
	kernel/sched/scheduler_core.c \
	kernel/sched/sched_policy.c \
	kernel/proc/job_control.c \
	kernel/proc/process_reap.c \
	kernel/proc/process_session.c \
	kernel/mem/address_space_core.c \
	kernel/driver/driver.c \
	kernel/proc/process_elf.c \
	arch/x86/gdt64.c \
	arch/x86/paging.c \
	arch/x86/idt64.c \
	block/block_event.c \
	block/blockdev.c \
	drivers/audio/audio.c \
	drivers/audio/pc_speaker.c \
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
	lib/string_benchmark.c \
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
	$(Q)$(HOSTCC) -O2 -Wall -Wextra -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include $(shell pkg-config --cflags fuse3) $< -o $@ $(shell pkg-config --libs fuse3)
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
	$(Q)$(LD) $(LDFLAGS64) -T $(ROOT)/kernel/linker.ld -o $@ $(OBJS)
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

.PHONY: all images kernel-i386 run-i386 check-i386 run dev run-uefi dev-uefi run-ac97 dev-ac97 run-tap dev-tap run-hda-tap dev-hda-tap run-ac97-tap dev-ac97-tap tap-up tap-down clean distclean check check-kernel check-image check-deps oneoff-user
.SILENT:
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
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/arch/x86/i386/pic.h \
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
		$(ROOT)/arch/x86/i386/keyboard.h $(ROOT)/arch/x86/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/pic.o: $(ROOT)/arch/x86/i386/pic.c $(ROOT)/arch/x86/i386/pic.h \
		$(ROOT)/arch/x86/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

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

$(I386_BUILD)/scheduler.o: $(ROOT)/arch/x86/i386/scheduler.c \
		$(ROOT)/arch/x86/i386/scheduler.h $(ROOT)/arch/x86/i386/idt.h \
		$(ROOT)/arch/x86/i386/gdt.h $(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/kernel/internal/proc/process_types_internal.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

$(I386_BUILD)/user.o: $(ROOT)/arch/x86/i386/user.c \
		$(ROOT)/arch/x86/i386/user.h $(ROOT)/arch/x86/i386/paging.h \
		$(ROOT)/arch/x86/i386/pmm.h $(ROOT)/fs/early_vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/arch/x86/i386 -c $< -o $@

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

$(I386_BUILD)/early_runtime_hooks.o: $(ROOT)/kernel/core/early_runtime_hooks.c | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

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

$(I386_BUILD)/i386_shared_services.o: $(ROOT)/kernel/core/i386_shared_services.c \
		$(ROOT)/kernel/internal/core/tty_internal.h $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/fat32.h $(ROOT)/block/blockdev.h \
		$(ROOT)/drivers/input/keyboard.h $(ROOT)/arch/x86/i386/keyboard.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/syscall_dispatch.o: $(ROOT)/kernel/sys/syscall_dispatch.c \
		$(ROOT)/kernel/public/sys/syscall_dispatch.h $(ROOT)/abi/syscall_abi.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/input_keyboard.o: $(ROOT)/drivers/input/keyboard.c \
		$(ROOT)/drivers/input/keyboard.h \
		$(ROOT)/kernel/public/input/keyboard_types.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/pci.o: $(ROOT)/drivers/bus/pci.c $(ROOT)/drivers/bus/pci.h \
		$(ROOT)/arch/x86/io.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/block_event.o: $(ROOT)/block/block_event.c $(ROOT)/block/block_event.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/blockdev.o: $(ROOT)/block/blockdev.c $(ROOT)/block/blockdev.h \
		$(ROOT)/block/block_event.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/ata.o: $(ROOT)/drivers/storage/ata.c $(ROOT)/drivers/storage/ata.h \
		$(ROOT)/drivers/bus/pci.h $(ROOT)/block/blockdev.h $(ROOT)/hal/hal.h | $(I386_BUILD)
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

$(I386_BUILD)/early_vfs.o: $(ROOT)/fs/early_vfs.c $(ROOT)/fs/early_vfs.h \
		$(ROOT)/fs/fat32.h $(ROOT)/block/blockdev.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -c $< -o $@

$(I386_BUILD)/vfs.o: $(ROOT)/fs/vfs.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/vfs_i386.o: $(ROOT)/fs/vfs_i386.c $(ROOT)/fs/vfs_internal.h \
		$(ROOT)/fs/vfs.h $(ROOT)/fs/fat32.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/hal_i386.o: $(ROOT)/hal/i386/platform.c $(ROOT)/hal/hal.h \
		$(ROOT)/arch/x86/i386/paging.h $(ROOT)/arch/x86/i386/pic.h \
		$(ROOT)/arch/x86/i386/keyboard.h $(ROOT)/arch/x86/i386/gdt.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_CFLAGS) -c $< -o $@

$(I386_BUILD)/io.o: $(ROOT)/arch/x86/io.c $(ROOT)/arch/x86/io.h | $(I386_BUILD)
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

$(I386_BUILD)/libc32_string.o: $(ROOT)/user/libc32/std/string.c \
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

$(I386_BUILD)/libc32_printf.o: $(ROOT)/user/libc32/std/printf.c \
		$(ROOT)/user/libc32/include/stdio.h $(ROOT)/user/libc32/include/stdarg.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_NLIBC): $(I386_BUILD)/libc32_syscall_asm.o \
		$(I386_BUILD)/libc32_syscall.o $(I386_BUILD)/libc32_string.o \
		$(I386_BUILD)/libc32_io.o $(I386_BUILD)/libc32_malloc.o \
		$(I386_BUILD)/libc32_printf.o | $(I386_BUILD)
	$(call log_cmd,AR32,$@)
	$(Q)rm -f $@
	$(Q)$(I386_AR) rcs $@ $^

$(I386_BUILD)/user_test32.o: $(ROOT)/user/i386/test32.c \
		$(ROOT)/user/libc32/include/nlibc.h | $(I386_BUILD)
	$(call log_cmd,CC32,$@)
	$(Q)$(I386_CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_TEST_USER): $(I386_CRT0) $(I386_BUILD)/user_test32.o \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_BUILD)/user_test32.o $(I386_NLIBC)

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

$(I386_NEXBOX_USER): $(I386_CRT0) $(I386_BUILD)/user_nexbox_lite.o \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_BUILD)/user_nexbox_lite.o $(I386_NLIBC)

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

check-i386: $(I386_KERNEL) $(I386_USER) $(I386_SCHED_USER) $(I386_TEST_USER) $(I386_APP_USER) $(I386_NEXBOX_USER)
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
	$(Q)echo "i386 boot/x ELF32 kernel checks passed"

$(I386_IMAGE): $(BOOTX_STAGE1) $(BOOTX_STAGE2) $(BOOTX_STAGE3) \
		$(I386_KERNEL) $(I386_USER) $(I386_SCHED_USER) $(I386_TEST_USER) \
		$(I386_APP_USER) \
		$(I386_NEXBOX_USER) \
		$(I386_BOOTX_CONFIG) | $(IMAGE_DIR)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s 64M $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary fat32 1MiB 100%
	$(Q)parted -s $@ set 1 boot on
	$(Q)mkfs.fat -F 32 --offset 2048 $@
	$(Q)dd if=$(BOOTX_STAGE1) of=$@ conv=notrunc bs=446 count=1
	$(Q)dd if=$(BOOTX_STAGE1) of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	$(Q)dd if=$(BOOTX_STAGE2) of=$@ conv=notrunc bs=512 seek=1
	$(Q)mmd -i $@@@1048576 ::/BOOT
	$(Q)mcopy -i $@@@1048576 $(BOOTX_STAGE3) ::/BOOT/STAGE3.SYS
	$(Q)mcopy -i $@@@1048576 $(I386_KERNEL) ::/BOOT/NEX386.ELF
	$(Q)mcopy -i $@@@1048576 $(I386_USER) ::/BOOT/USER32.ELF
	$(Q)mcopy -i $@@@1048576 $(I386_SCHED_USER) ::/BOOT/SCHED32.ELF
	$(Q)mcopy -i $@@@1048576 $(I386_TEST_USER) ::/BOOT/TEST32.ELF
	$(Q)mcopy -i $@@@1048576 $(I386_APP_USER) ::/BOOT/APP32.ELF
	$(Q)mcopy -i $@@@1048576 $(I386_NEXBOX_USER) ::/BOOT/NEXBOX32.ELF
	$(Q)mcopy -i $@@@1048576 $(I386_BOOTX_CONFIG) ::/BOOT/BOOTX.CFG

run-i386: check-i386 $(I386_IMAGE)
	qemu-system-i386 -m 128M -display gtk -serial stdio \
		-drive if=ide,index=0,media=disk,format=raw,file=$(I386_IMAGE)


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

$(DUMMY_AC97_DRIVER): $(DUMMY_AC97_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(BUILD)
	$(call log_cmd,DRV,$@)
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@

$(DUMMY_HDA_DRIVER): $(DUMMY_HDA_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(BUILD)
	$(call log_cmd,DRV,$@)
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@

$(RAMDISK_IMAGE): $(USER_ELF_BINS) $(BOOTX_CONFIG) $(FONT_HEX) $(INIT_SCRIPT) $(OS_CONFIG) $(DUMMY_AC97_DRIVER) $(DUMMY_HDA_DRIVER) $(FASM_TEST_SOURCE) $(ROOT)/config/ACTION.CAPS | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s $(RAMDISK_SIZE) $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary fat32 1MiB 100%
	$(Q)mkfs.fat -F 32 --offset 2048 $@
	$(Q)mmd -i $@@@1048576 ::/HOME
	$(Q)mmd -i $@@@1048576 ::/CMD
	$(Q)mmd -i $@@@1048576 ::/DRIVERS
	$(Q)mcopy -i $@@@1048576 $(BOOTX_CONFIG) ::/HOME/BOOTX.TXT
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
	$(Q)mcopy -i $@@@1048576 $(BUILD)/NCC.ELF ::/HOME/NCC.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/IMGVIEW.ELF ::/HOME/IMGVIEW.ELF
	$(Q)mcopy -i $@@@1048576 $(BUILD)/IPCDEMO.ELF ::/HOME/IPCDEMO.ELF
	$(Q)mcopy -i $@@@1048576 $(ROOT)/config/ACTION.CAPS ::/HOME/ACTION.CAPS
	$(Q)mcopy -i $@@@1048576 $(FASM_TEST_SOURCE) ::/HOME/TEST.ASM
	$(Q)mcopy -i $@@@1048576 $(INIT_SCRIPT) ::/INIT.SH
	$(Q)mcopy -i $@@@1048576 $(OS_CONFIG) ::/NOS.CFG
	$(Q)mcopy -i $@@@1048576 $(DUMMY_AC97_DRIVER) ::/DRIVERS/AC97.DRV
	$(Q)mcopy -i $@@@1048576 $(DUMMY_HDA_DRIVER) ::/DRIVERS/HDA.DRV
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

$(IMAGE_DIR): | $(BUILD)
	$(call log_cmd,MKDIR,$@)
	$(Q)mkdir -p $(IMAGE_DIR)

$(NXFS_TOOL): $(ROOT)/tools/nxfs_host.c $(ROOT)/fs/nxfs.c $(ROOT)/fs/nxfs_io.c $(ROOT)/fs/nxfs_internal.h $(ROOT)/fs/nxfs.h $(ROOT)/kernel/public/fs/nxfs_types.h $(ROOT)/lib/string.c | $(BUILD)
	$(call log_cmd,HOSTCC,$@)
	$(Q)$(HOSTCC) -O2 -Wall -Wextra -fno-builtin -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include $(ROOT)/tools/nxfs_host.c $(ROOT)/fs/nxfs.c $(ROOT)/fs/nxfs_io.c $(ROOT)/lib/string.c -o $@

$(NXFS_FS): $(NXFS_TOOL)
	$(call log_cmd,GEN,$@)
	$(Q)rm -f $@
	$(Q)$< mkfs $@

$(BOOT_FS_IMAGE): $(BOOTX_STAGE3) $(BOOTX_UEFI) $(BUILD)/kernel64.elf $(RAMDISK_IMAGE) $(BOOTX_CONFIG) $(FONT_HEX) | $(BUILD)
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
	$(Q)mcopy -i $@ $(FONT_HEX) ::/BOOT/FONT.HEX
	$(Q)mcopy -i $@ $(BOOTX_CONFIG) ::/BOOT/BOOTX.CFG

$(ROOT_FS_IMAGE): $(NXFS_TOOL) $(USER_ELF_BINS) $(DOOM1_WAD) $(DOOM2_WAD) $(TEST_C_SOURCE) $(TEST_WAV) $(USER_CRT0) $(USER_CRT_START) $(USER_NLIBC) $(ROOT)/user/apps/elf/user.ld $(BOOTX_CONFIG) $(FONT_HEX) $(INIT_SCRIPT) $(OS_CONFIG) $(FASM_TEST_SOURCE) $(ROOT)/config/ACTION.CAPS | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)$(NXFS_TOOL) mkfs $@
	$(Q)$(NXFS_TOOL) mkdir $@ /cmd
	$(Q)$(NXFS_TOOL) mkdir $@ /home
	$(Q)$(NXFS_TOOL) mkdir $@ /home/doom
	$(Q)$(NXFS_TOOL) mkdir $@ /system
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/include
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/include/nexos
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/include/sys
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/include/user
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/include/user/public
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/include/abi
	$(Q)$(NXFS_TOOL) mkdir $@ /system/devel/lib
	$(Q)$(NXFS_TOOL) mkdir $@ /system/font
	$(Q)$(NXFS_TOOL) mkdir $@ /system/config
	$(Q)$(NXFS_TOOL) mkdir $@ /system/session
	$(Q)$(NXFS_TOOL) mkdir $@ /system/session/images
	$(Q)$(NXFS_TOOL) mkdir $@ /system/service
	$(Q)$(NXFS_TOOL) write $@ $(INIT_SCRIPT) /init.sh
	$(Q)$(NXFS_TOOL) write $@ $(OS_CONFIG) /nos.cfg
	$(Q)$(NXFS_TOOL) write $@ $(OS_CONFIG) /system/config/nos.cfg
	$(Q)$(NXFS_TOOL) write $@ $(FONT_HEX) /system/font/font.hex

	$(Q)for f in $(ROOT)/user/libc/include/*.h; do \
		$(NXFS_TOOL) write $@ "$$f" /system/devel/include/$$(basename "$$f"); \
	done
	$(Q)for f in $(ROOT)/user/libc/include/nexos/*.h; do \
		$(NXFS_TOOL) write $@ "$$f" /system/devel/include/nexos/$$(basename "$$f"); \
	done
	$(Q)for f in $(ROOT)/user/libc/include/sys/*.h; do \
		$(NXFS_TOOL) write $@ "$$f" /system/devel/include/sys/$$(basename "$$f"); \
	done
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/user/public/sysapi.h /system/devel/include/user/public/sysapi.h
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/abi/syscall_abi.h /system/devel/include/abi/syscall_abi.h
	$(Q)$(NXFS_TOOL) write $@ $(USER_CRT0) /system/devel/lib/crt0.o
	$(Q)$(NXFS_TOOL) write $@ $(USER_CRT_START) /system/devel/lib/libc_start.o
	$(Q)$(NXFS_TOOL) write $@ $(USER_NLIBC) /system/devel/lib/libnlibc.a
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/user/apps/elf/user.ld /system/devel/lib/nexos.ld
	$(Q)$(NXFS_TOOL) write $@ $(ROOT)/config/ACTION.CAPS /home/action.caps
	$(Q)$(NXFS_TOOL) write $@ $(FASM_TEST_SOURCE) /home/test.asm
	$(Q)$(NXFS_TOOL) write $@ $(TEST_C_SOURCE) /home/test.c
	$(Q)$(NXFS_TOOL) write $@ $(TEST_WAV) /home/test.wav
	$(Q)$(NXFS_TOOL) write $@ $(TEST2_WAV) /home/test2.wav
	$(Q)$(NXFS_TOOL) write $@ $(DOOM1_WAD) /home/doom/doom1.wad
	$(Q)$(NXFS_TOOL) write $@ $(DOOM2_WAD) /home/doom/doom2.wad
	$(Q)$(NXFS_TOOL) write $@ assets/b.ppm /home/b.ppm
	$(Q)$(NXFS_TOOL) write $@ assets/a.ppm /home/a.ppm
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/USH.ELF /cmd/ush
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/NEXBOX.ELF /cmd/nexbox
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/NCC.ELF /cmd/ncc
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/DOOM.ELF /cmd/doom
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/IMGVIEW.ELF /cmd/imgview
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/IPCDEMO.ELF /cmd/ipcdemo
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/HELLO.ELF /cmd/hello
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/GUIDEMO.ELF /cmd/guidemo
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/FORTH.ELF /cmd/forth
	$(Q)rm -rf $(CMD_SUITE_WRAPPER_DIR)
	$(Q)mkdir -p $(CMD_SUITE_WRAPPER_DIR)
	$(Q)for alias in $(CMD_SUITE_NAMES); do \
		lower=$$(printf '%s' "$$alias" | tr 'A-Z' 'a-z'); \
		if [ "$$lower" = nexbox ]; then continue; fi; \
		script="$(CMD_SUITE_WRAPPER_DIR)/$$lower"; \
		printf '#!/cmd/ush\nexec /cmd/nexbox %s $$*\n' "$$lower" > "$$script"; \
		$(NXFS_TOOL) write $@ "$$script" /cmd/$$lower; \
	done

$(NXFS_IMAGE): $(NXFS_FS) | $(IMAGE_DIR)
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

$(BUILD)/kernel64.elf: $(OBJS) $(ROOT)/kernel/linker.ld
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



IMGVIEW_ELF_OBJS := $(BUILD)/user/apps/elf/imgview.o
IPCDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/ipcdemo.o

$(eval $(call define_user_elf,IMGVIEW.ELF,$(IMGVIEW_ELF_OBJS)))
$(eval $(call define_user_elf,IPCDEMO.ELF,$(IPCDEMO_ELF_OBJS)))

DOOM_ELF_OBJS := \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_math.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_platform.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/am_map.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_event.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_items.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_iwad.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_loop.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_mode.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_net.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/doomdef.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/doomgeneric.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/doomstat.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/dstrings.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/dummy.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/f_finale.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/f_wipe.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/g_game.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/gusconf.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/hu_lib.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/hu_stuff.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_cdmus.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_endoom.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_input.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_joystick.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_nexossound.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_scale.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_sound.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_system.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_timer.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_video.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/icon.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/info.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_argv.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_bbox.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_cheat.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_config.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_controls.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_fixed.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_menu.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_misc.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_random.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/memio.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/mus2mid.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_ceilng.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_doors.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_enemy.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_floor.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_inter.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_lights.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_map.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_maputl.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_mobj.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_plats.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_pspr.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_saveg.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_setup.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_sight.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_spec.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_switch.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_telept.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_tick.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_user.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_bsp.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_data.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_draw.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_plane.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_segs.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_sky.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_things.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/s_sound.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/sha1.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/sounds.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/st_lib.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/st_stuff.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/statdump.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/tables.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/v_video.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_checksum.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_file.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_file_stdc.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_wad.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/wi_stuff.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/z_zone.o \

$(eval $(call define_user_elf,DOOM.ELF,$(DOOM_ELF_OBJS)))

NCC_ELF_OBJS := \
	$(BUILD)/user/apps/elf/ncc/ncc_main.o \
	$(BUILD)/user/apps/elf/ncc/ncc_lexer.o \
	$(BUILD)/user/apps/elf/ncc/ncc_parser.o \
	$(BUILD)/user/apps/elf/ncc/ncc_codegen.o \
	$(BUILD)/user/apps/elf/ncc/ncc_link.o \
	$(BUILD)/user/apps/elf/ncc/ncc_util.o

$(eval $(call define_user_elf,NCC.ELF,$(NCC_ELF_OBJS)))

-include $(DEPFILES)

$(IMAGE): $(BOOTX_STAGE1) $(BOOTX_STAGE2) $(BOOT_FS_IMAGE) $(ROOT_FS_IMAGE) | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s $(OS_IMAGE_SIZE) $@
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
	$(Q)truncate -s $(OS_IMAGE_SIZE) $@
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
	qemu-system-x86_64 -enable-kvm \
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
	qemu-system-x86_64  -enable-kvm \
	-machine q35 \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(OVMF_VARS_IMAGE) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	$(QEMU_UEFI_SATA) \
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
	$(QEMU_UEFI_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-uefi2: $(UEFI_IMAGE) $(NXFS_IMAGE) $(OVMF_VARS_IMAGE)
	qemu-system-x86_64  -enable-kvm \
	-machine q35 \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(OVMF_VARS_IMAGE) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	$(QEMU_UEFI_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
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
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/action.caps
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/test.asm
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/test.c
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/test.wav
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/doom/doom1.wad
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /home/doom/doom2.wad
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
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/DRIVERS | grep -Eq 'AC97 +DRV'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/DRIVERS | grep -Eq 'HDA +DRV'
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

# Doomgeneric uses double math stubs; x86_64 ABI returns double in XMM regs.
$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_math.o: USERCFLAGS := $(filter-out -mno-sse -mgeneral-regs-only,$(USERCFLAGS)) -msse -mfpmath=sse

# Doomgeneric NexOS port flags
$(BUILD)/user/apps/elf/doomgeneric/%.o: USERCFLAGS += -D__NEXOS__

# Doomgeneric needs float/double ABI. Keep this limited to Doom objects.
DOOM_USERCFLAGS := $(filter-out -mno-sse -mgeneral-regs-only,$(USERCFLAGS)) -msse -mfpmath=sse -D__NEXOS__

$(BUILD)/user/apps/elf/doomgeneric/%.o: USERCFLAGS := $(DOOM_USERCFLAGS)
