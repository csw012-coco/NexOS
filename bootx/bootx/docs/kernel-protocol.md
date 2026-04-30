# boot/x Kernel Protocol Guide

This document explains how a kernel should consume the `boot/x` boot
protocol provided by `stage3`.

## Header To Include

Kernels should include:

```c
#include "bootx.h"
```

The protocol structs live in
[`include/bootx.h`](/home/csw012/bootx/boot/include/bootx.h).

## Entry Convention

### ELF32 kernels

`boot/x` calls the ELF entry point directly in 32-bit protected mode:

```c
void kernel_main(const struct bootx_boot_info *boot_info);
```

See:

- [`sample-kernel/kernel.c`](/home/csw012/bootx/boot/sample-kernel/kernel.c)

### ELF64 kernels

`boot/x` loads the ELF64 image, enables long mode, and jumps to the 64-bit
entry point with:

- `RDI = pointer to struct bootx_boot_info`

In C this looks like:

```c
void kernel_main64(const struct bootx_boot_info *boot_info);
```

See:

- [`sample-kernel64/kernel64.c`](/home/csw012/bootx/boot/sample-kernel64/kernel64.c)

## Validate The Hand-Off

Every kernel should check the protocol header first:

```c
if (boot_info->hdr.magic != BOOTX_MAGIC) {
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

if (boot_info->hdr.version < BOOTX_PROTOCOL_VERSION) {
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
```

Important values:

- `BOOTX_MAGIC = 0x42545831` which is `BTX1`
- current protocol version is `3`

## What The Kernel Receives

`struct bootx_boot_info` currently contains:

- protocol header
- BIOS boot drive
- partition LBA and size
- command line pointer
- memory map pointer and count
- console information
- kernel physical load base and size
- kernel entry address
- module pointer and count

The most useful fields are:

```c
boot_info->boot_drive
boot_info->cmdline
boot_info->memmap_count
boot_info->memmap
boot_info->console
boot_info->kernel_phys_addr
boot_info->kernel_phys_size
boot_info->kernel_entry
boot_info->module_count
boot_info->modules
```

## Accessing Pointers

`cmdline`, `memmap`, and `modules` are stored as low-memory addresses inside
the boot info struct. Cast them before use:

```c
const char *cmdline =
    (const char *)(uintptr_t)boot_info->cmdline;

const struct bootx_memmap_entry *memmap =
    (const struct bootx_memmap_entry *)(uintptr_t)boot_info->memmap;

const struct bootx_module *modules =
    (const struct bootx_module *)(uintptr_t)boot_info->modules;
```

This is the same pattern used by the sample kernels.

## Reading The Command Line

```c
const char *cmdline =
    (const char *)(uintptr_t)boot_info->cmdline;
```

The string is NUL-terminated and comes from `CMDLINE=` in `BOOTX.CFG`.

Example:

```ini
LABEL=Demo Kernel64
KERNEL=K64DEMO.ELF
CMDLINE=console=text root=bootx-demo64 arch=x86_64
```

## Reading The Memory Map

The memory map is an array of `struct bootx_memmap_entry`:

```c
const struct bootx_memmap_entry *memmap =
    (const struct bootx_memmap_entry *)(uintptr_t)boot_info->memmap;

for (uint32_t i = 0; i < boot_info->memmap_count; i++) {
    uint64_t base = memmap[i].base;
    uint64_t length = memmap[i].length;
    uint32_t type = memmap[i].type;
    (void)base;
    (void)length;
    (void)type;
}
```

Map types currently include:

- `BOOTX_MEMMAP_USABLE`
- `BOOTX_MEMMAP_RESERVED`
- `BOOTX_MEMMAP_ACPI_RECLAIMABLE`
- `BOOTX_MEMMAP_ACPI_NVS`
- `BOOTX_MEMMAP_BAD`
- `BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE`

## Reading Console Info

`boot/x` supports both text mode and BIOS VBE framebuffer hand-off.
The simplest way to request a framebuffer is:

```ini
CMDLINE=console=framebuffer root=bootx-demo64
```

Text mode example:

```c
if (boot_info->console.type == BOOTX_CONSOLE_TEXT) {
    uint16_t columns = boot_info->console.text_columns;
    uint16_t rows = boot_info->console.text_rows;
    uint8_t color = boot_info->console.text_color;
    (void)columns;
    (void)rows;
    (void)color;
}
```

Framebuffer example:

```c
if (boot_info->console.type == BOOTX_CONSOLE_FRAMEBUFFER) {
    uint64_t addr = boot_info->console.framebuffer_addr;
    uint32_t width = boot_info->console.width;
    uint32_t height = boot_info->console.height;
    uint32_t pitch = boot_info->console.pitch;
    uint8_t bpp = boot_info->console.framebuffer_bpp;
    (void)addr;
    (void)width;
    (void)height;
    (void)pitch;
    (void)bpp;
}
```

The current framebuffer fields include:

- `framebuffer_addr`
- `width`
- `height`
- `pitch`
- `framebuffer_bpp`
- `red_mask_size` / `red_mask_shift`
- `green_mask_size` / `green_mask_shift`
- `blue_mask_size` / `blue_mask_shift`

Current bootloader behavior:

- first tries a VBE linear framebuffer mode
- `CMDLINE` may request a specific mode with `video=1024x768`, `video=1024x768x32`, or `video=vesa:1024x768x32`
- if that is unavailable, falls back to VGA mode `13h`
- kernels should therefore handle both true-color framebuffer modes and the
  current fallback `320x200x8bpp` path

## Reading Loaded Modules

Modules come from `MODULE=` lines in `BOOTX.CFG`.

In this kernel tree, the framebuffer text console also checks for an optional `FONT.HEX` module and uses it to override the built-in 8x16 font when present.

For ELF64 higher-half kernels, `stage3` now chooses physical backing for loadable segments only after module loading is complete, so loaded modules are not overwritten by the kernel image during hand-off. Kernels should still reserve module ranges before handing memory to the general page allocator.

```c
const struct bootx_module *modules =
    (const struct bootx_module *)(uintptr_t)boot_info->modules;

for (uint32_t i = 0; i < boot_info->module_count; i++) {
    const char *name = modules[i].name;
    uint32_t address = modules[i].address;
    uint32_t size = modules[i].size;
    (void)name;
    (void)address;
    (void)size;
}
```

## Kernel Image Metadata

The bootloader also reports what it loaded:

```c
uint64_t kernel_phys_addr = boot_info->kernel_phys_addr;
uint64_t kernel_phys_size = boot_info->kernel_phys_size;
uint64_t kernel_entry = boot_info->kernel_entry;
```

This is useful for debugging and for later kernel-side physical memory
management setup.

## Minimal Example

```c
#include "bootx.h"

void kernel_main64(const struct bootx_boot_info *boot_info) {
    if (boot_info->hdr.magic != BOOTX_MAGIC) {
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    const char *cmdline =
        (const char *)(uintptr_t)boot_info->cmdline;
    const struct bootx_memmap_entry *memmap =
        (const struct bootx_memmap_entry *)(uintptr_t)boot_info->memmap;

    (void)cmdline;
    (void)memmap;

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
```

## Current Notes

- `boot/x` currently keeps protocol data structures in low memory
- ELF64 higher-half kernels are supported through bootloader paging setup
- current sample kernels still rely on the low identity mapping being present
- true high physical placement above 4GiB is not implemented yet
