# i386 build artifacts, object lists, and target rules.

I386_BUILD := $(BUILD)/i386
I386_KERNEL := $(I386_BUILD)/kernel-i386.elf
I386_USER := $(I386_BUILD)/USER32.ELF
I386_SCHED_USER := $(I386_BUILD)/SCHED32.ELF
I386_TEST_USER := $(I386_BUILD)/TEST32.ELF
I386_APP_USER := $(I386_BUILD)/APP32.ELF
I386_NEXBOX_USER := $(I386_BUILD)/NEXBOX32.ELF
I386_NEXBOX_SUBSET_USER := $(I386_BUILD)/NEXBOX32_SUBSET.ELF
I386_NEXBOX_FULL_USER := $(I386_BUILD)/NEXBOX32_FULL.ELF
I386_NEXBOX_FULL_LOG := $(I386_BUILD)/nexbox32-full-link.log
I386_USH_USER := $(I386_BUILD)/USH32.ELF
I386_DOOM_USER := $(I386_BUILD)/DOOM32.ELF
I386_NLIBC := $(I386_BUILD)/libnlibc32.a
I386_CRT0 := $(I386_BUILD)/libc32_crt0.o
I386_IMAGE := $(IMAGE_DIR)/NexOS-i386.img
I386_BOOT_FS_IMAGE := $(I386_BUILD)/boot.fat
I386_ROOT_FS_IMAGE := $(I386_BUILD)/root.nxfs
I386_CMD_SUITE_WRAPPER_DIR := $(BUILD)/cmd-wrappers-i386
I386_AUDIO_SMOKE_WAV := $(I386_BUILD)/audio-smoke.wav
I386_BOOT_LOG := $(I386_BUILD)/boot-smoke.log
I386_NEXBOX_FULL_BOOT_LOG := $(I386_BUILD)/nexbox32-full-smoke.log
I386_GFX_EDITOR_BOOT_LOG := $(I386_BUILD)/gfx-editor-smoke.log
I386_BACKEND_AUDIO_BOOT_LOG := $(I386_BUILD)/backend-audio-smoke.log
I386_BACKEND_HDA_BOOT_LOG := $(I386_BUILD)/backend-hda-smoke.log
I386_BACKEND_AHCI_BOOT_LOG := $(I386_BUILD)/backend-ahci-smoke.log
I386_BACKEND_EHCI_BOOT_LOG := $(I386_BUILD)/backend-ehci-smoke.log
I386_BACKEND_XHCI_BOOT_LOG := $(I386_BUILD)/backend-xhci-smoke.log
I386_BACKEND_EHCI_HID_BOOT_LOG := $(I386_BUILD)/backend-ehci-hid-smoke.log
I386_BACKEND_XHCI_HID_BOOT_LOG := $(I386_BUILD)/backend-xhci-hid-smoke.log
I386_BACKEND_RTL8139_BOOT_LOG := $(I386_BUILD)/backend-rtl8139-smoke.log

I386_BOOTX_CONFIG := $(ROOT)/config/bootx-i386.cfg
I386_QEMU ?= qemu-system-i386
I386_BOOT_TIMEOUT ?= 16
I386_CFLAGS := -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections \
	-fdata-sections -Wall -Wextra -O2 \
	-I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include -I$(ROOT) -I$(ROOT)/include -I$(BOOTX_DIR)/include/include
I386_LDFLAGS := -m elf_i386 -nostdlib -static --gc-sections
I386_USER_CFLAGS := -I$(ROOT)/user/libc32/include \
	$(I386_CFLAGS) -fno-builtin

include arch/x86/i386/sources.mk
include arch/x86/i386/objects.mk
include arch/x86/i386/rules.mk
