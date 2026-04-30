# boot/x

`boot/x` is a small BIOS x86 bootloader prototype with a 3-stage layout:

- `stage1`: MBR bootstrap in LBA 0
- `stage2`: embedded-area loader in sectors after the MBR
- `stage3`: file-based loader inside a FAT16 partition

Current MVP scope:

- x86 BIOS only
- MBR partition table only
- FAT16/FAT32 root directory support
- 8.3 filenames only
- text menu + ELF32 kernel load
- boot/x protocol v0 handoff
- ELF64 detection + long mode handoff skeleton

boot/x protocol v0 currently passes:

- `magic/version/size`
- boot drive and partition information
- command line string
- bootloader-provided memory map
- text console information
- kernel physical load base/size/entry
- loaded module list

ELF64 support status:

- `ELF32` kernels boot today
- `ELF64` kernels are now detected and routed through a dedicated loader path
- long mode transition scaffold is present
- current long mode path is intended for low physical-address x86_64 kernels with identity-mapped entry/load addresses below 4GiB
- higher-half kernels and richer x86_64 paging layouts are the next step

The default disk image layout is:

```text
[ LBA 0      ] stage1 (MBR bootstrap)
[ LBA 1..N   ] stage2 in embedding area
[ LBA 2048.. ] FAT16/FAT32 partition with STAGE3.SYS, BOOTX.CFG, kernels/modules
```

Build and run:

```sh
cd boot/x
make
make run
```

`make run` uses `qemu-system-x86_64` so the ELF64 long mode path can be tested.
If you want the old 32-bit VM target explicitly, use:

```sh
make run32
```

Default config format:

```ini
LABEL=Demo Kernel
KERNEL=BOOT/KERNEL.ELF
CMDLINE=console=text root=bootx-demo
```

Framebuffer entries can also request a specific VBE mode from the command line:

```ini
CMDLINE=console=framebuffer video=1024x768x32
CMDLINE=console=framebuffer video=vesa:800x600
```

Multiple entries are supported by separating blocks with a blank line.
FAT32 subdirectories such as `BOOT/` and `MOD/` are supported.

Optional module lines:

```ini
LABEL=With Module
KERNEL=BOOT/KERNEL.ELF
CMDLINE=demo=1
MODULE=MOD/INITRD.BIN
MODULE=MOD/EXTRA.BIN
```

This repository ships a sample module at:

- [`tools/INITRD.BIN`](/home/csw012/bootx/tools/INITRD.BIN)

The default 64-bit demo entry already loads it with:

```ini
MODULE=MOD/INITRD.BIN
```

Kernel-side usage of the boot protocol is documented in:

- [`docs/kernel-protocol.md`](/home/csw012/bootx/boot/docs/kernel-protocol.md)
