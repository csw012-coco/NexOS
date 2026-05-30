ROOT := $(CURDIR)
BUILD := $(ROOT)/build
CC := gcc
LD := ld
NASM := nasm
QEMU_IMG := qemu-img
CLANG := clang
LLD_LINK := lld-link

CFLAGS := -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -Wall -Wextra -I$(ROOT)/include
CFLAGS64 := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -Wall -Wextra -I$(ROOT)/include
UEFI_CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar -fno-builtin -fno-stack-protector -mno-red-zone -Wall -Wextra -I$(ROOT)/include
LDFLAGS32 := -m elf_i386
LDFLAGS64 := -m elf_x86_64

COMMON_C := $(ROOT)/common/runtime.c $(ROOT)/common/console.c $(ROOT)/common/bios.c $(ROOT)/common/disk.c $(ROOT)/common/fat16.c

STAGE2_OBJS := $(BUILD)/stage2_start.o $(BUILD)/realmode_stage2.o $(COMMON_C:$(ROOT)/common/%.c=$(BUILD)/common_%.o) $(BUILD)/stage2_main.o
STAGE3_COMMON_C := $(ROOT)/common/runtime.c $(ROOT)/common/console.c $(ROOT)/common/disk.c $(ROOT)/common/fat16.c
STAGE3_OBJS := $(BUILD)/stage3_start.o $(BUILD)/stage3_long_mode.o $(STAGE3_COMMON_C:$(ROOT)/common/%.c=$(BUILD)/common_stage3_%.o) $(BUILD)/stage3_main.o $(BUILD)/stage3_services.o

all: $(BUILD)/bootx.img $(BUILD)/BOOTX64.EFI

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/stage1.bin: $(ROOT)/stage1/bootsect.asm $(BUILD)/stage2.bin | $(BUILD)
	$(eval STAGE2_SECTORS := $(shell size=0; [ -f $(BUILD)/stage2.bin ] && size=$$(stat -c%s $(BUILD)/stage2.bin); echo $$(( (size + 511) / 512 )); ))
	$(NASM) -f bin -DSTAGE2_SECTORS=$(if $(STAGE2_SECTORS),$(STAGE2_SECTORS),32) $< -o $@

$(BUILD)/stage2_start.o: $(ROOT)/stage2/start.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/stage3_start.o: $(ROOT)/stage3/start.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/stage3_long_mode.o: $(ROOT)/stage3/long_mode.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/realmode_stage2.o: $(ROOT)/common/realmode.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/common_%.o: $(ROOT)/common/%.c $(ROOT)/include/bootx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/common_stage3_%.o: $(ROOT)/common/%.c $(ROOT)/include/bootx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage2_main.o: $(ROOT)/stage2/main.c $(ROOT)/include/bootx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage3_main.o: $(ROOT)/stage3/main.c $(ROOT)/include/bootx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage3_services.o: $(ROOT)/stage3/services.c $(ROOT)/include/bootx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uefi_main.obj: $(ROOT)/uefi/main.c $(ROOT)/include/bootx.h | $(BUILD)
	$(CLANG) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/BOOTX64.EFI: $(BUILD)/uefi_main.obj
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $<

$(BUILD)/stage2.elf: $(STAGE2_OBJS) $(ROOT)/link/stage2.ld
	$(LD) $(LDFLAGS32) -T $(ROOT)/link/stage2.ld -o $@ $(STAGE2_OBJS)

$(BUILD)/stage3.elf: $(STAGE3_OBJS) $(ROOT)/link/stage3.ld
	$(LD) $(LDFLAGS32) -T $(ROOT)/link/stage3.ld -o $@ $(STAGE3_OBJS)

$(BUILD)/stage2.bin: $(BUILD)/stage2.elf
	objcopy -O binary $< $@

$(BUILD)/stage3.sys: $(BUILD)/stage3.elf
	objcopy -O binary $< $@

$(BUILD)/kernel.o: $(ROOT)/sample-kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/kernel.o $(ROOT)/link/kernel.ld
	$(LD) $(LDFLAGS32) -T $(ROOT)/link/kernel.ld -o $@ $(BUILD)/kernel.o

$(BUILD)/kernel64.o: $(ROOT)/sample-kernel64/kernel64.c
	$(CC) $(CFLAGS64) -c $< -o $@

$(BUILD)/kernel64.elf: $(BUILD)/kernel64.o $(ROOT)/link/kernel64.ld
	$(LD) $(LDFLAGS64) -T $(ROOT)/link/kernel64.ld -o $@ $(BUILD)/kernel64.o

$(BUILD)/bootx.img: $(BUILD)/stage2.bin $(BUILD)/stage1.bin $(BUILD)/stage3.sys $(BUILD)/BOOTX64.EFI $(BUILD)/kernel.elf $(BUILD)/kernel64.elf $(ROOT)/tools/bootx.cfg $(ROOT)/tools/INITRD.BIN
	truncate -s 128M $@
	parted -s $@ mklabel msdos
	parted -s $@ mkpart primary fat32 1MiB 100%
	parted -s $@ set 1 boot on
	mkfs.fat -F 32 --offset 2048 $@
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc bs=446 count=1
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	dd if=$(BUILD)/stage2.bin of=$@ conv=notrunc bs=512 seek=1
	mmd -i $@@@1048576 ::/BOOT
	mmd -i $@@@1048576 ::/EFI
	mmd -i $@@@1048576 ::/EFI/BOOT
	mmd -i $@@@1048576 ::/MOD
	mcopy -i $@@@1048576 $(BUILD)/stage3.sys ::/STAGE3.SYS
	mcopy -i $@@@1048576 $(BUILD)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@@@1048576 $(BUILD)/kernel.elf ::/BOOT/KERNEL.ELF
	mcopy -i $@@@1048576 $(BUILD)/kernel64.elf ::/BOOT/K64DEMO.ELF
	mcopy -i $@@@1048576 $(ROOT)/tools/INITRD.BIN ::/MOD/INITRD.BIN
	mcopy -i $@@@1048576 $(ROOT)/tools/bootx.cfg ::/BOOTX.CFG

$(BUILD)/bootx.vmdk: $(BUILD)/bootx.img
	$(QEMU_IMG) convert -f raw -O vmdk $< $@

run: $(BUILD)/bootx.img
	qemu-system-x86_64 -vga std -drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/bootx.img

dev: $(BUILD)/bootx.img
	qemu-system-x86_64 \
		-vga std \
		-drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/bootx.img \
		-display none -monitor none -serial none \
		-debugcon stdio -no-reboot -no-shutdown

run32: $(BUILD)/bootx.img
	qemu-system-i386 -vga std -drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/bootx.img

vmdk: $(BUILD)/bootx.vmdk

clean:
	rm -rf $(BUILD)

.PHONY: all clean run dev run32 vmdk
