# NexOS

NexOS is a small experimental OS kernel with x86_64 and i386 builds.

The current tree focuses on making the i386 port behave like the same NexOS
kernel model with a 32-bit architecture backend, while keeping genuinely
architecture-specific pieces such as ELF32 loading, int 0x40 entry, i386 page
tables, and context switching under the i386 layer.

## Current i386 Status

Implemented and covered by the current smoke checks:

- BootX-based i386 boot to framebuffer console.
- ELF32 userland loading and `/system/init` startup.
- `USH32.ELF` shell startup and NEXBOX32 command execution.
- i386 syscall request adapter for the current libc32/userland surface.
- FAT32/NXFS/root/ramdisk mounts plus pseudo filesystems:
  - `/dev`
  - `/proc`
  - `/event`
  - `/boot`
  - `/ram`
- NEXBOX32 full smoke for the enabled command set.
- i386 driver discovery and state reporting for built-in drivers and ELF32
  `.DRV` files.
- i386 fork/COW smoke, fork/mmap/exec smoke, and shared mmap lifecycle smoke.
- `MAP_FIXED`, partial `munmap`, `mprotect`, mmap protection fault handling,
  and mmap cleanup paths used by the current TEST32 coverage.
- Shared-memory object lifetime now uses the common `address_space_core`
  shm object/refcount path; the i386 compat layer keeps only the 32-bit
  ABI-facing handle/name/mapping view.

Still in progress:

- Reducing `process32.c`, i386 scheduler glue, and syscall compat code until
  they are mostly arch backend/adapters.
- Moving more i386 mmap/process state to per-process address-space lifecycle.
- Full parity for every x86_64 applet/backend path.
- Deeper backend validation for AHCI, USB, RTL8139, AC97/HDA, gfx/editor, and
  Doom-like user programs.

## Build And Check

Useful checks:

```sh
make CCACHE= ARCH=i386 check
make CCACHE= check-kernel
make CCACHE= check-i386-smoke
make CCACHE= check-i386-nexbox32-full
```

Architecture-aware entry points are available through `ARCH`:

```sh
make ARCH=i386
make ARCH=x86_64
make ARCH=i386 run
make ARCH=x86_64 run
make ARCH=i386 check
make ARCH=x86_64 check
```

## Toolchains

Host tools expected by the main build/check targets:

- `cc`, `ld`, `ar`
- `nasm`
- `readelf`
- `dd`, `truncate`
- `mkfs.fat`
- `mcopy`, `mdir`
- `parted`
- `timeout`

QEMU is needed for run/smoke targets:

- `qemu-system-x86_64`
- `qemu-system-i386`

The i386 kernel/userland build currently uses the Makefile-configured host
32-bit path (`I386_CC`, `I386_LD`, `I386_AR`). Override those variables if your
host needs a different command.

The x86_64 build expects an `x86_64-elf-*` cross toolchain by default. The
Makefile first tries `PATH`, then `$(HOME)/opt/cross/bin`:

```sh
x86_64-elf-gcc
x86_64-elf-ld
x86_64-elf-ar
```

Override `CROSS_PREFIX`, `QEMU_X86_64`, or `I386_QEMU` when the commands have
site-local names or live outside `PATH`.

Optional Doom WAD assets are read from `assets/doom` and copied into the guest
filesystem under `/home/doom`. They are not required for the normal build.

## Notes

The i386 port is not considered complete merely because it boots and runs the
current smoke suite. The remaining goal is structural parity: common kernel
core for process, scheduler, syscall, memory, VFS, and drivers, with i386 only
owning the 32-bit hardware/ABI backend pieces.
