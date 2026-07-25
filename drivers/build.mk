# =========================
# Driver sources and module builds
# =========================

DRIVER_C_SRCS := \
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
	drivers/video/surface.c \
	drivers/video/framebuffer.c \
	drivers/video/vga.c \
	drivers/input/keyboard.c \
	drivers/input/mouse.c

I386_DRIVER_C_SRCS := \
	drivers/audio/audio.c \
	drivers/audio/pc_speaker.c \
	drivers/video/framebuffer.c \
	drivers/input/keyboard.c \
	drivers/input/mouse.c \
	drivers/bus/pci.c \
	drivers/rtc/cmos.c \
	drivers/storage/ramdisk.c \
	block/block_event.c \
	block/blockdev.c \
	drivers/net/net_event.c \
	drivers/i386/device_backend_stubs.c \
	drivers/serial/uart.c

DUMMY_AC97_DRIVER_SRC := $(ROOT)/drivers/dummy/ac97_drv.c
DUMMY_AC97_DRIVER := $(BUILD)/AC97.DRV
DUMMY_HDA_DRIVER_SRC := $(ROOT)/drivers/dummy/hda_drv.c
DUMMY_HDA_DRIVER := $(BUILD)/HDA.DRV

I386_DRV_CFLAGS := $(I386_CFLAGS) -fno-builtin
I386_TEST_DRIVER_SRC := $(ROOT)/drivers/dummy/i386_probe_drv.c
I386_TEST_DRIVER := $(I386_BUILD)/I386TEST.DRV
I386_AC97_DRIVER := $(I386_BUILD)/AC9732.DRV
I386_HDA_DRIVER := $(I386_BUILD)/HDA32.DRV

$(DUMMY_AC97_DRIVER): $(DUMMY_AC97_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(BUILD)
	$(call log_cmd,DRV,$@)
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@

$(DUMMY_HDA_DRIVER): $(DUMMY_HDA_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(BUILD)
	$(call log_cmd,DRV,$@)
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@

$(I386_TEST_DRIVER): $(I386_TEST_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(I386_BUILD)
	$(call log_cmd,DRV32,$@)
	$(Q)$(I386_CC) $(I386_DRV_CFLAGS) -c $< -o $@

$(I386_AC97_DRIVER): $(DUMMY_AC97_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(I386_BUILD)
	$(call log_cmd,DRV32,$@)
	$(Q)$(I386_CC) $(I386_DRV_CFLAGS) -c $< -o $@

$(I386_HDA_DRIVER): $(DUMMY_HDA_DRIVER_SRC) $(ROOT)/kernel/public/driver/driver_module.h $(ROOT)/kernel/public/driver/driver.h $(ROOT)/Makefile | $(I386_BUILD)
	$(call log_cmd,DRV32,$@)
	$(Q)$(I386_CC) $(I386_DRV_CFLAGS) -c $< -o $@
