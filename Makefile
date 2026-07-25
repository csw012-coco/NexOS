# =========================
# 설정
# =========================
ROOT := $(CURDIR)
BUILD := $(ROOT)/build
ASSETS := $(ROOT)/assets
DOOM_ASSETS := $(ASSETS)/doom
DOOM1_WAD := $(DOOM_ASSETS)/DOOM1.WAD
DOOM2_WAD := $(DOOM_ASSETS)/DOOM2.WAD
TNT_WAD := $(DOOM_ASSETS)/TNT.WAD
PLUTONIA_WAD := $(DOOM_ASSETS)/PLUTONIA.WAD
DOOM_GUEST_DIR ?= /home/doom
DOOM1_GUEST_WAD := $(DOOM_GUEST_DIR)/doom1.wad
DOOM2_GUEST_WAD := $(DOOM_GUEST_DIR)/doom2.wad
TNT_GUEST_WAD := $(DOOM_GUEST_DIR)/tnt.wad
PLUTONIA_GUEST_WAD := $(DOOM_GUEST_DIR)/plutonia.wad
MOTD_CFG := $(ASSETS)/motd/motd.scf
TEST_C_SOURCE := $(ASSETS)/c/test.c
TEST_WAV := $(ASSETS)/audio/test.wav
TEST2_WAV := $(ASSETS)/audio/test2.wav
TEST3_WAV := $(ASSETS)/audio/test3.wav
TEST4_WAV := $(ASSETS)/audio/test4.wav
FONT_HEX := $(ASSETS)/fonts/font.hex
BOOT_FONT_HEX := $(BUILD)/font-boot.hex
ROOT_WAD_FILES := \
	"$(DOOM1_WAD):$(DOOM1_GUEST_WAD)" \
	"$(DOOM2_WAD):$(DOOM2_GUEST_WAD)" \
	"$(TNT_WAD):$(TNT_GUEST_WAD)" \
	"$(PLUTONIA_WAD):$(PLUTONIA_GUEST_WAD)"
NXFS_AUDIO_FILES := \
	"$(TEST_WAV):/test.wav" \
	"$(TEST2_WAV):/test2.wav" \
	"$(TEST3_WAV):/test3.wav" \
	"$(TEST4_WAV):/test4.wav"
NXFS_ROOT_DIRS := \
	/cmd \
	/home \
	$(DOOM_GUEST_DIR) \
	/system \
	/system/devel \
	/system/devel/include \
	/system/devel/include/nexos \
	/system/devel/include/sys \
	/system/devel/include/user \
	/system/devel/include/user/public \
	/system/devel/include/abi \
	/system/devel/lib \
	/system/font \
	/system/config \
	/system/session \
	/system/session/images \
	/system/service
IMAGE_DIR := $(BUILD)/images
CMD_SUITE_WRAPPER_DIR := $(BUILD)/cmd-wrappers
BOOT := $(ROOT)/boot
BOOTX_DIR := $(ROOT)/bootloader/bootx
BOOTX_BUILD := $(BOOTX_DIR)/build
BOOTX_CONFIG := $(ROOT)/config/bootx.cfg
BOOTX_CONFIG_RENDERED := $(BUILD)/bootx.generated.cfg
IMAGE := $(IMAGE_DIR)/NexOS.img
BIOS_IMAGE := $(IMAGE_DIR)/NexOS-bios.img
UEFI_IMAGE := $(IMAGE_DIR)/NexOS-uefi.img
OS_IMAGE_SIZE ?= 256M
NXFS_IMAGE := $(IMAGE_DIR)/nxfs.img
RAMDISK_IMAGE := $(BUILD)/ramdisk.img
RAMDISK_SIZE ?= 4M
NXFS_TOOL := $(BUILD)/nxfs_host
NXFS_FS := $(BUILD)/nxfs.fs
BOOT_FS_IMAGE := $(BUILD)/boot.fat
ROOT_FS_IMAGE := $(BUILD)/root.nxfs
BOOT_PART_LBA := 2048
ROOT_PART_LBA := 100352
MODE ?= solo
.DEFAULT_GOAL := all
include mk/toolchains.mk
include mk/targets-i386.mk
include drivers/build.mk
include fs/build.mk

SCRIPT_SMOKE_SH := $(BUILD)/script-smoke.sh
BUILD_OPT_FLAGS ?= -O2
SECTION_FLAGS := -ffunction-sections -fdata-sections
CFLAGS64 := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -pipe $(SECTION_FLAGS) -Wall -Wextra $(BUILD_OPT_FLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include
DRV_CFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=large -pipe $(SECTION_FLAGS) -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra $(BUILD_OPT_FLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include
USERCFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=large -pipe $(SECTION_FLAGS) -Wall -Wextra $(BUILD_OPT_FLAGS) -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT)/user/libc/include -I$(ROOT)/user/libc/include/sys -I$(ROOT)/user/libc/include/nexos -I$(ROOT)/user/public -I$(ROOT)/abi
LDFLAGS64 := -nostdlib -static -m elf_x86_64 --gc-sections
ROOT_INIT_SCRIPT := $(ROOT)/user/init/init_root.sh
RAMDISK_INIT_SCRIPT := $(ROOT)/user/init/init_ramdisk.sh
OS_CONFIG := $(ROOT)/config/NEX.SCF
FASM_TEST_SOURCE := $(ROOT)/user/examples/fasm/test.asm
CMD_SUITE_NAMES := NEXBOX HELP ACTIONS ACTION MAPPER ECHO CLEAR PWD TTY ENV FONT WHICH TYPE LS CAT LESS HEXDUMP GREP DATE HWCLOCK SLEEP WATCH ON EVENTS CLIPBOARD WC HEAD TAIL FIND AS PICK SELECT SORT-BY COUNT-BY TO VIEW ED VI VIM TOUCH MV CP MKDIR RMDIR RM ASM STAT DU TREE FILE BLK PARTS FDISK DD MKFS DF MOUNTS PROGS FATLS FATFIND FATREAD CPIO MOUNT UMOUNT HOTPLUG RUN RUNELF RUNBG PS SESSION SERVICE JOBS WAIT ALARM TIMEOUT KILL FG BG SWITCH_ROOT REBOOT DMESG LSPCI AC97 HDA RTL8139 RTL8139TX RTL8139RX ARP ROUTE NETSTAT PING DNS DHCP IFCONFIG HTTP WGET NC AUDIO TONE WAV MPLAY DOCTOR NEXCTL SYSINFO MEMINFO MINFO UNAME CPUINFO CONFIG DBG
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

include kernel/build.mk
include user/libc/build.mk

define log_cmd
	$(Q)printf '%-7s %s\n' "$(1)" "$(2)"
endef

define nxfs_mkdirs
	$(Q)for dir in $(2); do \
		$(NXFS_TOOL) mkdir $(1) "$$dir"; \
	done
endef

define nxfs_write_optional
	$(Q)for entry in $(2); do \
		host=$${entry%%:*}; \
		target=$${entry#*:}; \
		if [ -f "$$host" ]; then \
			$(NXFS_TOOL) write $(1) "$$host" "$$target"; \
		else \
			printf '%-7s %s\n' "SKIP" "$$host"; \
		fi; \
	done
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

all: arch-build

all-x86_64: images $(NXFS_IMAGE)


.PHONY: all all-x86_64 images arch arch-build arch-check arch-run arch-unsupported kernel-i386 run-i386 check-i386 run run-x86_64 dev run-uefi dev-uefi run-ac97 dev-ac97 run-tap dev-tap run-hda-tap dev-hda-tap run-ac97-tap dev-ac97-tap tap-up tap-down clean distclean check check-x86_64 check-x86_64-nexbox-full check-i386-gfx-editor-smoke check-i386-utf8-input-parity check-i386-driver-active check-i386-backend-long check-i386-backend-audio check-i386-backend-hda check-i386-backend-ahci check-i386-backend-ehci check-i386-backend-xhci check-i386-backend-ehci-hid check-i386-backend-xhci-hid check-i386-backend-rtl8139 check-all check-kernel check-image check-deps check-host-tools check-host-tools-i386 check-host-tools-x86_64 check-host-tools-image check-host-tools-qemu-i386 check-host-tools-qemu-x86_64 oneoff-user
.SILENT:
arch: arch-build

arch-build:
	$(Q)$(MAKE) $(ARCH_BUILD_TARGET)

arch-check:
	$(Q)$(MAKE) $(ARCH_CHECK_TARGET)

arch-run:
	$(Q)$(MAKE) $(ARCH_RUN_TARGET)

arch-unsupported:
	$(Q)printf '%s\n' "unsupported ARCH=$(ARCH) (expected i386 or x86_64)" >&2
	$(Q)exit 2

include user/apps/build.mk
include mk/images.mk
include mk/run.mk



include mk/host-tools.mk
include mk/checks.mk


oneoff-user: $(USER_CRT0) $(USER_CRT_START) $(USER_NLIBC) $(ROOT)/user/apps/elf/user.ld | $(BUILD)
	@if [ -z "$(SRC)" ]; then echo "usage: make oneoff-user SRC=./aa.c OUT=AA.ELF"; exit 1; fi
	@if [ -z "$(OUT)" ]; then echo "usage: make oneoff-user SRC=./aa.c OUT=AA.ELF"; exit 1; fi
	$(call log_cmd,CC,$(BUILD)/oneoff_user.o)
	$(Q)$(CC) $(USERCFLAGS) -c $(SRC) -o $(BUILD)/oneoff_user.o
	$(call log_cmd,LD,$(BUILD)/$(OUT))
	$(Q)$(LD) $(LDFLAGS64) -T $(ROOT)/user/apps/elf/user.ld -o $(BUILD)/$(OUT) $(USER_CRT0) $(USER_CRT_START) $(BUILD)/oneoff_user.o $(USER_NLIBC)

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

DEPFILES := $(KERNEL_C_OBJS:.o=.d) $(USER_NLIBC_C_OBJS:.o=.d) $(USER_NLIBC_ASM_OBJS:.o=.d) $(USER_CRT_C_OBJS:.o=.d) $(USER_CRT_ASM_OBJS:.o=.d) $(USER_ELF_C_OBJS:.o=.d)

-include $(DEPFILES)




include mk/smoke-x86_64.mk




clean:
	rm -rf $(BUILD) $(IMAGE) $(BIOS_IMAGE) $(UEFI_IMAGE)

distclean: clean
	rm -rf $(NXFS_IMAGE)
.PHONY: bootx-loader

bootx-loader:
	$(Q)$(MAKE) -C $(BOOTX_DIR)

$(BOOTX_STAGE1) $(BOOTX_STAGE2) $(BOOTX_STAGE3) $(BOOTX_UEFI): bootx-loader
