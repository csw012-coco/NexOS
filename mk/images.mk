# Disk image construction and image content checks.

images: check-host-tools-image $(IMAGE) $(BIOS_IMAGE) $(UEFI_IMAGE)

$(I386_BOOT_FS_IMAGE): $(BOOTX_STAGE3) \
		$(I386_KERNEL) $(I386_USER) $(I386_SCHED_USER) $(I386_TEST_USER) \
		$(I386_APP_USER) \
		$(I386_NEXBOX_USER) \
		$(I386_NEXBOX_SUBSET_USER) \
		$(I386_USH_USER) \
		$(RAMDISK_IMAGE) \
		$(I386_BOOTX_CONFIG) $(BOOT_FONT_HEX) | $(I386_BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s 48M $@
	$(Q)mkfs.fat -F 32 $@
	$(Q)mmd -i $@ ::/BOOT
	$(Q)mcopy -i $@ $(BOOTX_STAGE3) ::/BOOT/STAGE3.SYS
	$(Q)mcopy -i $@ $(I386_KERNEL) ::/BOOT/NEX386.ELF
	$(Q)mcopy -i $@ $(I386_USER) ::/BOOT/USER32.ELF
	$(Q)mcopy -i $@ $(I386_SCHED_USER) ::/BOOT/SCHED32.ELF
	$(Q)mcopy -i $@ $(I386_TEST_USER) ::/BOOT/TEST32.ELF
	$(Q)mcopy -i $@ $(I386_APP_USER) ::/BOOT/APP32.ELF
	$(Q)mcopy -i $@ $(I386_NEXBOX_USER) ::/BOOT/NEXBOX32.ELF
	$(Q)mcopy -i $@ $(I386_NEXBOX_SUBSET_USER) ::/BOOT/NEXBOX32S.ELF
	$(Q)mcopy -i $@ $(I386_USH_USER) ::/BOOT/USH32.ELF
	$(Q)mcopy -i $@ $(BOOT_FONT_HEX) ::/BOOT/FONT.HEX
	$(Q)mcopy -i $@ $(RAMDISK_IMAGE) ::/BOOT/RAMDISK.IMG
	$(Q)mcopy -i $@ $(I386_BOOTX_CONFIG) ::/BOOT/BOOTX.CFG

$(SCRIPT_SMOKE_SH): Makefile | $(BUILD)
	$(call log_cmd,GEN,$@)
	$(Q){ \
		printf '%s\n' '#!/cmd/ush'; \
		printf '%s\n' 'echo script-smoke: PASS'; \
	} > $@

$(I386_AUDIO_SMOKE_WAV): | $(I386_BUILD)
	$(call log_cmd,GEN,$@)
	$(Q)python3 -c 'import struct, pathlib; data=bytes(4096); p=pathlib.Path("$@"); p.write_bytes(b"RIFF"+struct.pack("<I",36+len(data))+b"WAVEfmt "+struct.pack("<IHHIIHH",16,1,2,48000,48000*4,4,16)+b"data"+struct.pack("<I",len(data))+data)'

$(I386_ROOT_FS_IMAGE): $(NXFS_TOOL) \
		$(I386_TEST_USER) $(I386_APP_USER) \
		$(I386_NEXBOX_USER) $(I386_NEXBOX_SUBSET_USER) $(I386_USH_USER) \
		$(I386_DOOM_USER) \
		$(I386_TEST_DRIVER) $(I386_AC97_DRIVER) $(I386_HDA_DRIVER) \
		$(OS_CONFIG) $(FONT_HEX) $(ROOT_INIT_SCRIPT) $(SCRIPT_SMOKE_SH) \
		$(I386_AUDIO_SMOKE_WAV) | $(I386_BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)$(NXFS_TOOL) mkfs $@
	$(call nxfs_mkdirs,$@,$(NXFS_ROOT_DIRS))
	$(Q)$(NXFS_TOOL) mkdir $@ /drivers
	$(Q)$(NXFS_TOOL) write $@ $(ROOT_INIT_SCRIPT) /system/init
	$(Q)$(NXFS_TOOL) write $@ $(OS_CONFIG) /system/config/nex.scf
	$(Q)$(NXFS_TOOL) write $@ $(FONT_HEX) /system/font/font.hex
	$(Q)$(NXFS_TOOL) write $@ $(SCRIPT_SMOKE_SH) /system/script-smoke.sh
	$(Q)$(NXFS_TOOL) write $@ $(I386_AUDIO_SMOKE_WAV) /system/audio-smoke.wav
	$(call nxfs_write_optional,$@,$(ROOT_WAD_FILES))
	$(Q)$(NXFS_TOOL) write $@ $(I386_USH_USER) /cmd/ush
	$(Q)$(NXFS_TOOL) write $@ $(I386_NEXBOX_USER) /cmd/nexbox
	$(Q)$(NXFS_TOOL) write $@ $(I386_NEXBOX_SUBSET_USER) /cmd/nexbox32s
	$(Q)$(NXFS_TOOL) write $@ $(I386_TEST_USER) /cmd/test32
	$(Q)$(NXFS_TOOL) write $@ $(I386_APP_USER) /cmd/app32
	$(Q)$(NXFS_TOOL) write $@ $(I386_DOOM_USER) /cmd/doom32
	$(Q)$(NXFS_TOOL) write $@ $(I386_TEST_DRIVER) /drivers/I386TEST.DRV
	$(Q)$(NXFS_TOOL) write $@ $(I386_AC97_DRIVER) /drivers/AC9732.DRV
	$(Q)$(NXFS_TOOL) write $@ $(I386_HDA_DRIVER) /drivers/HDA32.DRV
	$(Q)rm -rf $(I386_CMD_SUITE_WRAPPER_DIR)
	$(Q)mkdir -p $(I386_CMD_SUITE_WRAPPER_DIR)
	$(Q)for alias in $(CMD_SUITE_NAMES); do \
		lower=$$(printf '%s' "$$alias" | tr 'A-Z' 'a-z'); \
		if [ "$$lower" = nexbox ]; then continue; fi; \
		script="$(I386_CMD_SUITE_WRAPPER_DIR)/$$lower"; \
		printf '#!/cmd/ush\nexec /cmd/nexbox %s $$*\n' "$$lower" > "$$script"; \
		$(NXFS_TOOL) write $@ "$$script" /cmd/$$lower; \
	done

$(I386_IMAGE): $(BOOTX_STAGE1) $(BOOTX_STAGE2) $(I386_BOOT_FS_IMAGE) $(I386_ROOT_FS_IMAGE) | $(IMAGE_DIR)
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
	$(Q)dd if=$(I386_BOOT_FS_IMAGE) of=$@ conv=notrunc bs=512 seek=$(BOOT_PART_LBA)
	$(Q)dd if=$(I386_ROOT_FS_IMAGE) of=$@ conv=notrunc bs=512 seek=$(ROOT_PART_LBA)

$(RAMDISK_IMAGE): $(BUILD)/USH.ELF $(BUILD)/NEXBOX.ELF $(RAMDISK_INIT_SCRIPT) $(OS_CONFIG) $(DUMMY_AC97_DRIVER) $(DUMMY_HDA_DRIVER) | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)truncate -s $(RAMDISK_SIZE) $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary fat32 1MiB 100%
	$(Q)mkfs.fat -F 32 --offset 2048 $@
	$(Q)mmd -i $@@@1048576 ::/CMD
	$(Q)mmd -i $@@@1048576 ::/DRIVERS
	$(Q)mcopy -i $@@@1048576 $(RAMDISK_INIT_SCRIPT) ::/init
	$(Q)mcopy -i $@@@1048576 $(OS_CONFIG) ::/NOS.CFG
	$(Q)mcopy -i $@@@1048576 $(DUMMY_AC97_DRIVER) ::/DRIVERS/AC97.DRV
	$(Q)mcopy -i $@@@1048576 $(DUMMY_HDA_DRIVER) ::/DRIVERS/HDA.DRV
	$(Q)mcopy -i $@@@1048576 $(BUILD)/USH.ELF ::/CMD/USH
	$(Q)mcopy -i $@@@1048576 $(BUILD)/NEXBOX.ELF ::/CMD/NEXBOX

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

$(BOOT_FONT_HEX): $(FONT_HEX) $(ROOT)/tools/font_subset.py | $(BUILD)
	$(call log_cmd,GEN,$@)
	$(Q)python3 $(ROOT)/tools/font_subset.py $< $@

$(BOOTX_CONFIG_RENDERED): $(BOOTX_CONFIG) $(ROOT_FS_IMAGE) $(NXFS_TOOL) | $(BUILD)
	$(call log_cmd,GEN,$@)
	$(Q)uuid="$$( $(NXFS_TOOL) uuid $(ROOT_FS_IMAGE) )"; \
	uuid="$${uuid#uuid=}"; \
	sed -e "s/root=[^ ]*/root=UUID=$$uuid/g" \
		-e "s/init=[^ ]*/init=\/system\/init/g" \
		$(BOOTX_CONFIG) > $@

$(BOOT_FS_IMAGE): $(BOOTX_STAGE3) $(BOOTX_UEFI) $(BUILD)/kernel64.elf $(RAMDISK_IMAGE) $(BOOTX_CONFIG_RENDERED) $(BOOT_FONT_HEX) | $(BUILD)
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
	$(Q)mcopy -i $@ $(BOOT_FONT_HEX) ::/BOOT/FONT.HEX
	$(Q)mcopy -i $@ $(BOOTX_CONFIG_RENDERED) ::/BOOT/BOOTX.CFG

$(ROOT_FS_IMAGE): $(NXFS_TOOL) $(USER_ELF_BINS) $(TEST_C_SOURCE) $(USER_CRT0) $(USER_CRT_START) $(USER_NLIBC) $(ROOT)/user/apps/elf/user.ld $(FONT_HEX) $(ROOT_INIT_SCRIPT) $(OS_CONFIG) $(FASM_TEST_SOURCE) $(ROOT)/config/ACTION.CAPS $(SCRIPT_SMOKE_SH) | $(BUILD)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@
	$(Q)$(NXFS_TOOL) mkfs $@
	$(call nxfs_mkdirs,$@,$(NXFS_ROOT_DIRS))
	$(Q)$(NXFS_TOOL) write $@ $(ROOT_INIT_SCRIPT) /system/init
	$(Q)$(NXFS_TOOL) write $@ $(OS_CONFIG) /system/config/nex.scf
	$(Q)$(NXFS_TOOL) write $@ $(FONT_HEX) /system/font/font.hex
	$(Q)$(NXFS_TOOL) write $@ $(SCRIPT_SMOKE_SH) /system/script-smoke.sh
	$(Q)$(NXFS_TOOL) write $@ $(MOTD_CFG) /system/config/motd.scf
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
	$(call nxfs_write_optional,$@,$(ROOT_WAD_FILES))
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/USH.ELF /cmd/ush
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/NEXBOX.ELF /cmd/nexbox
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/NCC.ELF /cmd/ncc
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/DOOM.ELF /cmd/doom
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/IMGVIEW.ELF /cmd/imgview
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/IPCDEMO.ELF /cmd/ipcdemo
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/HELLO.ELF /cmd/hello
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/GUIDEMO.ELF /cmd/guidemo
	$(Q)$(NXFS_TOOL) write $@ $(BUILD)/FORTH.ELF /cmd/forth
	$(Q)$(NXFS_TOOL) write $@ assets/calc /calc
	$(Q)rm -rf $(CMD_SUITE_WRAPPER_DIR)
	$(Q)mkdir -p $(CMD_SUITE_WRAPPER_DIR)
	$(Q)for alias in $(CMD_SUITE_NAMES); do \
		lower=$$(printf '%s' "$$alias" | tr 'A-Z' 'a-z'); \
		if [ "$$lower" = nexbox ]; then continue; fi; \
		script="$(CMD_SUITE_WRAPPER_DIR)/$$lower"; \
		printf '#!/cmd/ush\nexec /cmd/nexbox %s $$*\n' "$$lower" > "$$script"; \
		$(NXFS_TOOL) write $@ "$$script" /cmd/$$lower; \
	done

NXFS_PART_IMAGE := $(IMAGE_DIR)/nxfs.part

$(NXFS_IMAGE): $(NXFS_TOOL) | $(IMAGE_DIR)
	$(call log_cmd,IMAGE,$@)
	$(Q)rm -f $@ $(NXFS_PART_IMAGE)
	$(Q)truncate -s 511M $(NXFS_PART_IMAGE)
	$(Q)$(NXFS_TOOL) mkfs $(NXFS_PART_IMAGE)
	$(call nxfs_write_optional,$(NXFS_PART_IMAGE),$(NXFS_AUDIO_FILES))
	$(Q)truncate -s 512M $@
	$(Q)parted -s $@ mklabel msdos
	$(Q)parted -s $@ mkpart primary 1MiB 100%
	$(Q)dd if=$(NXFS_PART_IMAGE) of=$@ conv=notrunc bs=512 seek=2048

$(IMAGE): $(BOOTX_STAGE1) $(BOOTX_STAGE2) $(BOOT_FS_IMAGE) $(ROOT_FS_IMAGE) | $(IMAGE_DIR)
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

check-image: check-host-tools-image $(IMAGE) $(BIOS_IMAGE) $(UEFI_IMAGE) $(NXFS_IMAGE) $(ROOT_FS_IMAGE)
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
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/init
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
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/config/nex.scf
	@if [ -f "$(DOOM1_WAD)" ]; then $(NXFS_TOOL) exists $(ROOT_FS_IMAGE) $(DOOM1_GUEST_WAD); else printf '%s\n' '[check] skipping missing DOOM1.WAD'; fi
	@if [ -f "$(DOOM2_WAD)" ]; then $(NXFS_TOOL) exists $(ROOT_FS_IMAGE) $(DOOM2_GUEST_WAD); else printf '%s\n' '[check] skipping missing DOOM2.WAD'; fi
	@if [ -f "$(TNT_WAD)" ]; then $(NXFS_TOOL) exists $(ROOT_FS_IMAGE) $(TNT_GUEST_WAD); else printf '%s\n' '[check] skipping missing TNT.WAD'; fi
	@if [ -f "$(PLUTONIA_WAD)" ]; then $(NXFS_TOOL) exists $(ROOT_FS_IMAGE) $(PLUTONIA_GUEST_WAD); else printf '%s\n' '[check] skipping missing PLUTONIA.WAD'; fi
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/font/font.hex
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/session/images
	@$(NXFS_TOOL) exists $(ROOT_FS_IMAGE) /system/service
	@printf '%s\n' '[check] verifying ramdisk contents'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/CMD | grep -Ei 'USH'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/CMD | grep -Ei 'NEXBOX'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/DRIVERS | grep -Eq 'AC97 +DRV'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/DRIVERS | grep -Eq 'HDA +DRV'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/ | grep -Eiq 'init'
	@mdir -i $(RAMDISK_IMAGE)@@1048576 ::/ | grep -Eq 'NOS +CFG'
