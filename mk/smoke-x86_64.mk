# =========================
# x86_64 smoke targets
# =========================
X86_64_NEXBOX_FULL_INIT := $(BUILD)/nexbox64-full-init.sh
X86_64_NEXBOX_FULL_SCRIPT := $(BUILD)/nexbox64-full-smoke.sh
X86_64_NEXBOX_FULL_BOOTX_CONFIG := $(BUILD)/bootx-nexbox64-full.cfg
X86_64_NEXBOX_FULL_ROOT := $(BUILD)/root-nexbox64-full.nxfs
X86_64_NEXBOX_FULL_IMAGE := $(IMAGE_DIR)/NexOS-nexbox64-full.img
X86_64_NEXBOX_FULL_BOOT_LOG := $(BUILD)/nexbox64-full-smoke.log

$(X86_64_NEXBOX_FULL_INIT): Makefile mk/smoke-x86_64.mk | $(BUILD)
	$(call log_cmd,GEN,$@)
		$(Q){ \
			printf '%s\n' '#!/cmd/ush'; \
			printf '%s\n' 'exec /cmd/ush --tty /dev/tty --init /system/nexbox64-full-smoke'; \
		} > $@

$(X86_64_NEXBOX_FULL_SCRIPT): Makefile mk/smoke-x86_64.mk | $(BUILD)
	$(call log_cmd,GEN,$@)
	$(Q){ \
		printf '%s\n' '#!/cmd/ush'; \
		printf '%s\n' '/cmd/nexbox echo nexbox64 || exit 1'; \
		printf '%s\n' '/cmd/nexbox help || exit 1'; \
		printf '%s\n' '/cmd/nexbox pwd || exit 1'; \
		printf '%s\n' '/system/script-smoke.sh direct || exit 1'; \
		printf '%s\n' '/cmd/nexbox tty || exit 1'; \
		printf '%s\n' '/cmd/nexbox env || exit 1'; \
		printf '%s\n' '/cmd/nexbox font --table || exit 1'; \
		printf '%s\n' '/cmd/nexbox font sample || exit 1'; \
		printf '%s\n' '/cmd/nexbox font --utf8-check || exit 1'; \
		printf '%s\n' '/cmd/nexbox clipboard --utf8-check || exit 1'; \
		printf '%s\n' '/cmd/nexbox uname -a || exit 1'; \
		printf '%s\n' '/cmd/nexbox sysinfo || exit 1'; \
		printf '%s\n' '/cmd/nexbox meminfo || exit 1'; \
		printf '%s\n' '/cmd/nexbox minfo || exit 1'; \
		printf '%s\n' '/cmd/nexbox date --raw || exit 1'; \
		printf '%s\n' '/cmd/nexbox hwclock || exit 1'; \
		printf '%s\n' '/cmd/nexbox parts || exit 1'; \
		printf '%s\n' '/cmd/nexbox mounts || exit 1'; \
		printf '%s\n' '/cmd/nexbox df || exit 1'; \
		printf '%s\n' '/cmd/nexbox blk || exit 1'; \
		printf '%s\n' '/cmd/nexbox drivers || exit 1'; \
		printf '%s\n' '/cmd/nexbox cat /proc/devices || exit 1'; \
		printf '%s\n' '/cmd/nexbox cat /proc/mounts || exit 1'; \
		printf '%s\n' '/cmd/nexbox cat /proc/drivers || exit 1'; \
		printf '%s\n' '/cmd/nexbox dmesg || exit 1'; \
		printf '%s\n' '/cmd/nexbox lspci || exit 1'; \
		printf '%s\n' '/cmd/nexbox doctor --table || exit 1'; \
		printf '%s\n' '/cmd/nexbox mapper info fb || exit 1'; \
		printf '%s\n' '/cmd/nexbox action info gfx.fb || exit 1'; \
		printf '%s\n' '/cmd/nexbox action policy explain gfx.fb || exit 1'; \
		printf '%s\n' '/cmd/nexbox fb || exit 1'; \
		printf '%s\n' '/cmd/nexbox fb --smoke || exit 1'; \
		printf '%s\n' '/cmd/nexbox fb --blit-smoke || exit 1'; \
		printf '%s\n' '/cmd/nexbox fb --batch-smoke || exit 1'; \
		printf '%s\n' '/cmd/nexbox cat /proc/fb || exit 1'; \
		printf '%s\n' '/cmd/nexbox mapper info ed || exit 1'; \
		printf '%s\n' '/cmd/nexbox mapper info vi || exit 1'; \
		printf '%s\n' '/cmd/nexbox mapper info vim || exit 1'; \
		printf '%s\n' '/cmd/nexbox action info editor.ed || exit 1'; \
		printf '%s\n' '/cmd/nexbox action info editor.vi || exit 1'; \
		printf '%s\n' '/cmd/nexbox action policy explain editor.ed || exit 1'; \
		printf '%s\n' '/cmd/nexbox action policy explain editor.vi || exit 1'; \
		printf '%s\n' '/cmd/nexbox ed --check || exit 1'; \
		printf '%s\n' '/cmd/nexbox vi --check || exit 1'; \
		printf '%s\n' '/cmd/nexbox vim --check || exit 1'; \
		printf '%s\n' '/cmd/doom -iwad $(DOOM1_GUEST_WAD) -smoke-frames 3 || exit 1'; \
		printf '%s\n' '/cmd/nexbox echo "nexbox64: full applet smoke PASS" || exit 1'; \
		printf '%s\n' 'exit 0'; \
	} > $@

$(X86_64_NEXBOX_FULL_ROOT): $(ROOT_FS_IMAGE) $(X86_64_NEXBOX_FULL_INIT) $(X86_64_NEXBOX_FULL_SCRIPT) $(NXFS_TOOL) | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)cp $(ROOT_FS_IMAGE) $@
	$(Q)$(NXFS_TOOL) write $@ $(X86_64_NEXBOX_FULL_INIT) /system/init
	$(Q)$(NXFS_TOOL) write $@ $(X86_64_NEXBOX_FULL_SCRIPT) /system/nexbox64-full-smoke

$(X86_64_NEXBOX_FULL_BOOTX_CONFIG): $(X86_64_NEXBOX_FULL_ROOT) $(NXFS_TOOL) Makefile mk/smoke-x86_64.mk | $(BUILD)
	$(call log_cmd,GEN,$@)
	$(Q)uuid="$$( $(NXFS_TOOL) uuid $(X86_64_NEXBOX_FULL_ROOT) | sed 's/^uuid=//' )"; \
	{ \
		printf '%s\n' 'LABEL=NexOS(nexbox64-full-smoke)'; \
		printf '%s\n' 'KERNEL=BOOT/NEX.ELF'; \
		printf '%s\n' 'MODULE=BOOT/RAMDISK.IMG'; \
		printf '%s\n' 'MODULE=BOOT/FONT.HEX'; \
		printf '%s\n' "CMDLINE=console=framebuffer video=1024x768x32 root=UUID=$$uuid arch=x86_64 init=/system/init"; \
	} > $@

$(X86_64_NEXBOX_FULL_IMAGE): $(IMAGE) $(X86_64_NEXBOX_FULL_ROOT) $(X86_64_NEXBOX_FULL_BOOTX_CONFIG) | $(IMAGE_DIR)
	$(call log_cmd,IMAGE,$@)
	$(Q)cp $(IMAGE) $@
	$(Q)mcopy -o -i $@@@1048576 $(X86_64_NEXBOX_FULL_BOOTX_CONFIG) ::/BOOT/BOOTX.CFG
	$(Q)dd if=$(X86_64_NEXBOX_FULL_ROOT) of=$@ conv=notrunc bs=512 seek=$(ROOT_PART_LBA)

check-x86_64-nexbox-full: check-host-tools-qemu-x86_64 check-deps check-kernel $(X86_64_NEXBOX_FULL_IMAGE) $(NXFS_IMAGE)
	$(call log_cmd,QEMU64,$(X86_64_NEXBOX_FULL_IMAGE))
	$(Q)rm -f $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)set +e; \
		timeout 24s $(QEMU_X86_64) -enable-kvm -m 256M \
			-display vnc=127.0.0.1:77 -no-reboot -no-shutdown \
			-debugcon file:$(X86_64_NEXBOX_FULL_BOOT_LOG) \
			-global isa-debugcon.iobase=0xe9 \
			-serial none \
			$(QEMU_NET) \
			-drive if=ide,index=0,media=disk,format=raw,file=$(X86_64_NEXBOX_FULL_IMAGE); \
		status=$$?; \
		if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
			echo "x86_64 NEXBOX full smoke failed with status $$status"; \
			test -f $(X86_64_NEXBOX_FULL_BOOT_LOG) && tail -n 120 $(X86_64_NEXBOX_FULL_BOOT_LOG); \
			exit $$status; \
		fi
	$(Q)grep -q 'kernel: services online' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'kernel: system/init' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'kernel: init probe ok' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'kernel: console shell starting /cmd/ush' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'fb: blit smoke OK' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'fb: batch smoke OK' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'font: utf8/unifont check OK' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'clipboard: utf8 roundtrip OK' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'ed: editor ready' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'vi: editor ready' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'doomgeneric: smoke PASS frames=3' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)grep -q 'nexbox64: full applet smoke PASS' $(X86_64_NEXBOX_FULL_BOOT_LOG)
	$(Q)echo "x86_64 NEXBOX full smoke passed ($(X86_64_NEXBOX_FULL_BOOT_LOG))"
