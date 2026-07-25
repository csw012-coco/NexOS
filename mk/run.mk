# QEMU run and development targets.

run-i386: $(I386_IMAGE) $(NXFS_IMAGE)
	$(I386_QEMU) -m 128M -display gtk -serial stdio \
		-drive if=ide,index=0,media=disk,format=raw,file=$(I386_IMAGE) \
		-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE)

run: arch-run

run-x86_64: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64) -enable-kvm \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
	$(QEMU_X86_64)  -enable-kvm \
	-machine q35 \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(OVMF_VARS_IMAGE) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	$(QEMU_UEFI_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-uefi: $(UEFI_IMAGE) $(NXFS_IMAGE) $(OVMF_VARS_IMAGE)
	$(QEMU_X86_64) \
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
	$(QEMU_X86_64) -enable-kvm \
		-machine q35 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS_IMAGE) \
		$(QEMU_SERIAL) \
		$(QEMU_NET) \
		$(QEMU_UEFI_SATA) \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,bus=xhci.0 \
		-device intel-hda \
		-device hda-duplex,audiodev=snd0 \
		-audiodev $(QEMU_AUDIODEV)

run-hda: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-hda: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-usb: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_USB_MSC) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-usb: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_USB_MSC) \
	$(QEMU_USB_HID) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

run-xhci: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_XHCI_MSC) \
	$(QEMU_XHCI_HID) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-ac97: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-tap: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device intel-hda \
	-device hda-duplex,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-hda-tap: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
	$(QEMU_X86_64) \
	$(QEMU_SERIAL) \
	$(QEMU_NET_TAP) \
	-drive if=ide,index=0,media=disk,format=raw,file=$(IMAGE) \
	$(QEMU_NXFS_SATA) \
	-device AC97,audiodev=snd0 \
	-audiodev $(QEMU_AUDIODEV)

dev-ac97-tap: $(IMAGE) $(NXFS_IMAGE)
	$(QEMU_X86_64)  \
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
