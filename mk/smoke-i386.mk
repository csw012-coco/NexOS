# i386 QEMU smoke and backend validation targets.

check-i386-nexbox32-full: check-host-tools-image check-host-tools-qemu-i386 check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_NEXBOX_FULL_BOOT_LOG) $(I386_BUILD)/NexOS-i386-nexbox32-full.img $(I386_BUILD)/janus-i386-nexbox32-full.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-nexbox32-full.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ selftest=1 i386.fullsmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-nexbox32-full.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-nexbox32-full.img@@1048576 \
		$(I386_BUILD)/janus-i386-nexbox32-full.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 40s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_NEXBOX_FULL_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-nexbox32-full.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE); \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 NEXBOX32 full smoke failed with status $$status"; \
			test -f $(I386_NEXBOX_FULL_BOOT_LOG) && tail -n 120 $(I386_NEXBOX_FULL_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'test32: fork smoke PASS' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'test32: fork shared mmap PASS' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'test32: fork wait exec PASS' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'tty: utf8/hangul edit selftest OK' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'I386TEST.DRV.*loaded.*ELF32/i386/REL.*init-ok.* /drivers/I386TEST.DRV' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox config validate' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox clipboard set nexbox32-clipboard-smoke' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'clipboard: utf8 roundtrip OK' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox events jobs' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox actions --table' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox action caps' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox sysinfo' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox cpuinfo' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox date --raw' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox hwclock' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox parts' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox mounts' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox df' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox blk' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox stat --table /cmd/ush' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox file --table /cmd/nexbox' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox tree --table /cmd' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox touch /ram/nexbox32-smoke.tmp' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox dd if=/cmd/ush of=/ram/nexbox32-smoke.dd bs=64 count=1' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox cpio -t /ram/nexbox32-smoke.cpio' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox session info smoke32' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox service info smoke32' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox ps' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox jobs' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox drivers' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox dmesg' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox lspci' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox doctor --table' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox font --utf8-check' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox fb --blit-smoke' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox fb --batch-smoke' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox ed --check' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox vi --check' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/doom32 -iwad $(DOOM1_GUEST_WAD) -nogui -mb 6 -smoke-frames 3' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'doomgeneric: smoke PASS frames=3' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox audio' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox rtl8139' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox32: full applet smoke PASS' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'kernel: init starting path=/system/init' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)echo "i386 NEXBOX32 full smoke passed ($(I386_NEXBOX_FULL_BOOT_LOG))"

check-i386-strict-mm: check-host-tools-image check-host-tools-qemu-i386 check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-MM,$(I386_TEST_USER))
	$(Q)rm -f $(I386_STRICT_MM_BOOT_LOG) $(I386_BUILD)/NexOS-i386-strict-mm.img $(I386_BUILD)/janus-i386-strict-mm.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-strict-mm.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.strictmm=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-strict-mm.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-strict-mm.img@@1048576 \
		$(I386_BUILD)/janus-i386-strict-mm.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_STRICT_MM_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-strict-mm.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE); \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 strict MM smoke failed with status $$status"; \
			test -f $(I386_STRICT_MM_BOOT_LOG) && tail -n 120 $(I386_STRICT_MM_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'test32: RUN /cmd/test32 strict-mm' $(I386_STRICT_MM_BOOT_LOG)
	$(Q)grep -q '\[test32\] strict MM PASS' $(I386_STRICT_MM_BOOT_LOG)
	$(Q)grep -q 'test32: strict MM PASS' $(I386_STRICT_MM_BOOT_LOG)
	$(Q)grep -q 'kernel: i386 strict MM smoke complete' $(I386_STRICT_MM_BOOT_LOG)
	$(Q)echo "i386 strict MM smoke passed ($(I386_STRICT_MM_BOOT_LOG))"

check-i386-utf8-input-parity: check-i386-nexbox32-full check-i386-gfx-editor-smoke
	$(Q)grep -q 'tty: utf8/hangul edit selftest OK' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'font: utf8/unifont check OK' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'clipboard: utf8 roundtrip OK' $(I386_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'ed: editor ready' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'vi: editor ready' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)echo "i386 UTF-8/Hangul input and editor parity smoke passed"

check-i386-backend-long: check-i386-driver-active check-i386-gfx-editor-smoke check-i386-nexbox32-full
	$(Q)grep -q 'rtl8139: tx/rx smoke OK' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'ac97: backend smoke OK' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'hda: backend smoke OK' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'fb: blit smoke OK' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'fb: batch smoke OK' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: smoke PASS' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)echo "i386 net/audio/gfx backend long smoke passed"

check-i386-driver-active: check-i386-backend-audio check-i386-backend-hda check-i386-backend-ahci check-i386-backend-ehci check-i386-backend-xhci check-i386-backend-ehci-hid check-i386-backend-xhci-hid check-i386-backend-rtl8139
	$(Q)echo "i386 driver active path smoke passed"

check-i386-backend-audio: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_AUDIO_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-audio.img $(I386_BUILD)/janus-i386-backend-audio.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-audio.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.ac97smoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-audio.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-audio.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-audio.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_AUDIO_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-audio.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE) \
				-audiodev none,id=snd0 \
				-device AC97,audiodev=snd0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend audio smoke failed with status $$status"; \
			test -f $(I386_BACKEND_AUDIO_BOOT_LOG) && tail -n 120 $(I386_BACKEND_AUDIO_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'ac97: driver AC97 state=active reason=pci' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'ac97: backend smoke OK' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox ac97' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'AC97 controller' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'codec id=' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'nexbox32: PASS /cmd/nexbox ac97' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox wav --smoke /system/audio-smoke.wav' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'wav: fd stream smoke OK' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'nexbox32: PASS /cmd/nexbox wav --smoke /system/audio-smoke.wav' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_AUDIO_BOOT_LOG)
	$(Q)echo "i386 backend audio smoke passed ($(I386_BACKEND_AUDIO_BOOT_LOG))"

check-i386-backend-hda: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_HDA_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-hda.img $(I386_BUILD)/janus-i386-backend-hda.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-hda.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.hdasmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-hda.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-hda.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-hda.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_HDA_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-hda.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE) \
				-audiodev none,id=snd0 \
				-device intel-hda \
				-device hda-duplex,audiodev=snd0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend HDA smoke failed with status $$status"; \
			test -f $(I386_BACKEND_HDA_BOOT_LOG) && tail -n 120 $(I386_BACKEND_HDA_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'hda: driver HDA state=active reason=pci' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'hda: backend smoke OK' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox hda' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'HD Audio controller' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'codec_mask=' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'rings corb_size=' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'nexbox32: PASS /cmd/nexbox hda' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_HDA_BOOT_LOG)
	$(Q)echo "i386 backend HDA smoke passed ($(I386_BACKEND_HDA_BOOT_LOG))"

check-i386-gfx-editor-smoke: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-GFX,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_GFX_EDITOR_BOOT_LOG) $(I386_BUILD)/NexOS-i386-gfx-editor.img $(I386_BUILD)/janus-i386-gfx-editor.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-gfx-editor.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.gfxeditorsmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-gfx-editor.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-gfx-editor.img@@1048576 \
		$(I386_BUILD)/janus-i386-gfx-editor.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_GFX_EDITOR_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-gfx-editor.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE); \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 gfx/editor smoke failed with status $$status"; \
			test -f $(I386_GFX_EDITOR_BOOT_LOG) && tail -n 120 $(I386_GFX_EDITOR_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'gfx/editor: RUN /cmd/nexbox fb --blit-smoke' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: RUN /cmd/nexbox fb --batch-smoke' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: RUN /cmd/nexbox font --utf8-check' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: RUN /cmd/nexbox vim --check' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: RUN /cmd/nexbox ed --check' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: RUN /cmd/nexbox vi --check' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'gfx/editor: smoke PASS' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_GFX_EDITOR_BOOT_LOG)
	$(Q)echo "i386 gfx/editor smoke passed ($(I386_GFX_EDITOR_BOOT_LOG))"

check-i386-backend-ahci: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_AHCI_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-ahci.img $(I386_BUILD)/janus-i386-backend-ahci.cfg $(I386_BUILD)/nxfs-ahci-smoke.img
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-ahci.img
	$(Q)cp $(NXFS_IMAGE) $(I386_BUILD)/nxfs-ahci-smoke.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.ahcismoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-ahci.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-ahci.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-ahci.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_AHCI_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-ahci.img \
				-drive if=none,id=nxfsahci,format=raw,file=$(I386_BUILD)/nxfs-ahci-smoke.img \
				-device ich9-ahci,id=ahci \
				-device ide-hd,drive=nxfsahci,bus=ahci.0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend AHCI smoke failed with status $$status"; \
			test -f $(I386_BACKEND_AHCI_BOOT_LOG) && tail -n 120 $(I386_BACKEND_AHCI_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'ahci: controller' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci: port' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci0 sectors=' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci: read smoke OK' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci: write smoke OK' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci: flush smoke OK' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci: restore smoke OK' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'ahci: rw smoke OK' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'driver: builtin active=' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_AHCI_BOOT_LOG)
	$(Q)echo "i386 backend AHCI smoke passed ($(I386_BACKEND_AHCI_BOOT_LOG))"

check-i386-backend-ehci: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_EHCI_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-ehci.img $(I386_BUILD)/janus-i386-backend-ehci.cfg $(I386_BUILD)/nxfs-ehci-smoke.img
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-ehci.img
	$(Q)cp $(NXFS_IMAGE) $(I386_BUILD)/nxfs-ehci-smoke.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.usbsmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-ehci.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-ehci.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-ehci.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 24s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_EHCI_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-ehci.img \
				-drive if=none,id=usbehci,format=raw,file=$(I386_BUILD)/nxfs-ehci-smoke.img \
				-device usb-ehci,id=ehci \
				-device usb-storage,drive=usbehci,bus=ehci.0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend EHCI smoke failed with status $$status"; \
			test -f $(I386_BACKEND_EHCI_BOOT_LOG) && tail -n 120 $(I386_BACKEND_EHCI_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'ehci: controller' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: port.*vid=' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: MSC if=' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: port.*usbmsc' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: msc read smoke OK' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: msc write smoke OK' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: msc flush smoke OK' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: msc restore smoke OK' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'ehci: msc rw smoke OK' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_EHCI_BOOT_LOG)
	$(Q)echo "i386 backend EHCI MSC smoke passed ($(I386_BACKEND_EHCI_BOOT_LOG))"

check-i386-backend-xhci: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_XHCI_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-xhci.img $(I386_BUILD)/janus-i386-backend-xhci.cfg $(I386_BUILD)/nxfs-xhci-smoke.img
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-xhci.img
	$(Q)cp $(NXFS_IMAGE) $(I386_BUILD)/nxfs-xhci-smoke.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.usbsmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-xhci.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-xhci.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-xhci.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 24s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_XHCI_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-xhci.img \
				-drive if=none,id=usbxhci,format=raw,file=$(I386_BUILD)/nxfs-xhci-smoke.img \
				-device qemu-xhci,id=xhci \
				-device usb-storage,drive=usbxhci,bus=xhci.0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend XHCI smoke failed with status $$status"; \
			test -f $(I386_BACKEND_XHCI_BOOT_LOG) && tail -n 120 $(I386_BACKEND_XHCI_BOOT_LOG); \
			exit $$status; \
	fi
	$(Q)grep -q 'xhci.*controller' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xhci: MSC probe slot=' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xusbmsc' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xhci: msc read smoke OK' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xhci: msc write smoke OK' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xhci: msc flush smoke OK' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xhci: msc restore smoke OK' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'xhci: msc rw smoke OK' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_XHCI_BOOT_LOG)
	$(Q)echo "i386 backend XHCI MSC smoke passed ($(I386_BACKEND_XHCI_BOOT_LOG))"

check-i386-backend-ehci-hid: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_EHCI_HID_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-ehci-hid.img $(I386_BUILD)/janus-i386-backend-ehci-hid.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-ehci-hid.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.usbhidsmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-ehci-hid.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-ehci-hid.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-ehci-hid.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_EHCI_HID_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-ehci-hid.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE) \
				-device usb-ehci,id=ehci \
				-device usb-kbd,bus=ehci.0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend EHCI HID smoke failed with status $$status"; \
			test -f $(I386_BACKEND_EHCI_HID_BOOT_LOG) && tail -n 120 $(I386_BACKEND_EHCI_HID_BOOT_LOG); \
			exit $$status; \
	fi
	$(Q)grep -q 'ehci: controller' $(I386_BACKEND_EHCI_HID_BOOT_LOG)
	$(Q)grep -q 'ehci: port.*vid=' $(I386_BACKEND_EHCI_HID_BOOT_LOG)
	$(Q)grep -q 'ehci: port.*hidkbd' $(I386_BACKEND_EHCI_HID_BOOT_LOG)
	$(Q)grep -q 'ehci: hid keyboard smoke OK' $(I386_BACKEND_EHCI_HID_BOOT_LOG)
	$(Q)grep -q 'usb: hid keyboard smoke OK' $(I386_BACKEND_EHCI_HID_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_EHCI_HID_BOOT_LOG)
	$(Q)echo "i386 backend EHCI HID smoke passed ($(I386_BACKEND_EHCI_HID_BOOT_LOG))"

check-i386-backend-xhci-hid: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_XHCI_HID_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-xhci-hid.img $(I386_BUILD)/janus-i386-backend-xhci-hid.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-xhci-hid.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.usbhidsmoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-xhci-hid.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-xhci-hid.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-xhci-hid.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_XHCI_HID_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-xhci-hid.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE) \
				-device qemu-xhci,id=xhci \
				-device usb-kbd,bus=xhci.0; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend XHCI HID smoke failed with status $$status"; \
			test -f $(I386_BACKEND_XHCI_HID_BOOT_LOG) && tail -n 120 $(I386_BACKEND_XHCI_HID_BOOT_LOG); \
			exit $$status; \
	fi
	$(Q)grep -q 'xhci.*controller' $(I386_BACKEND_XHCI_HID_BOOT_LOG)
	$(Q)grep -q 'xhci: hid keyboard smoke OK' $(I386_BACKEND_XHCI_HID_BOOT_LOG)
	$(Q)grep -q 'xhci: hid keyboard smoke OK count=' $(I386_BACKEND_XHCI_HID_BOOT_LOG)
	$(Q)grep -q 'usb: hid keyboard smoke OK' $(I386_BACKEND_XHCI_HID_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_XHCI_HID_BOOT_LOG)
	$(Q)echo "i386 backend XHCI HID smoke passed ($(I386_BACKEND_XHCI_HID_BOOT_LOG))"

check-i386-backend-rtl8139: check-i386-elf $(I386_IMAGE) $(NXFS_IMAGE) $(I386_JANUS_SMOKE_CONFIG)
	$(call log_cmd,QEMU32-BACKEND,$(I386_NEXBOX_USER))
	$(Q)rm -f $(I386_BACKEND_RTL8139_BOOT_LOG) $(I386_BUILD)/NexOS-i386-backend-rtl8139.img $(I386_BUILD)/janus-i386-backend-rtl8139.cfg
	$(Q)cp $(I386_IMAGE) $(I386_BUILD)/NexOS-i386-backend-rtl8139.img
	$(Q)sed '/^[[:space:]]*cmdline / s/$$/ i386.rtl8139smoke=1/' $(I386_JANUS_SMOKE_CONFIG) > $(I386_BUILD)/janus-i386-backend-rtl8139.cfg
	$(Q)mcopy -o -i $(I386_BUILD)/NexOS-i386-backend-rtl8139.img@@1048576 \
		$(I386_BUILD)/janus-i386-backend-rtl8139.cfg ::/BOOT/JANUS.CFG
	$(Q)set +e; \
			timeout 20s $(I386_QEMU) -m 128M \
				-display none -no-reboot -no-shutdown \
				-debugcon file:$(I386_BACKEND_RTL8139_BOOT_LOG) \
				-global isa-debugcon.iobase=0xe9 \
				-serial null \
				-drive if=ide,index=0,media=disk,format=raw,file=$(I386_BUILD)/NexOS-i386-backend-rtl8139.img \
				-drive if=ide,index=1,media=disk,format=raw,file=$(NXFS_IMAGE) \
				-nic user,model=rtl8139; \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "i386 backend RTL8139 smoke failed with status $$status"; \
			test -f $(I386_BACKEND_RTL8139_BOOT_LOG) && tail -n 120 $(I386_BACKEND_RTL8139_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'rtl8139: controller' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'rtl8139: tx/rx smoke OK' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox rtl8139' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'RTL8139 controller' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'mac 52:54:00:12:34:56' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox ifconfig' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'state up link=up speed=100Mbps' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'ipv4 10.0.2.15' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox netstat' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'active sockets: not tracked yet' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'nexbox32: RUN /cmd/nexbox route' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'default.*10.0.2.2.*rtl8139' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'rtl8139: command smoke OK' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)grep -q 'kernel: services online' $(I386_BACKEND_RTL8139_BOOT_LOG)
	$(Q)echo "i386 backend RTL8139 TX/RX smoke passed ($(I386_BACKEND_RTL8139_BOOT_LOG))"
