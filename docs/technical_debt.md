# Technical Debt Register

## TD-001
Title: Former builtin-user ABI depended on kernel-owned headers
Applicable Modes: Solo, Small, Large
Risk Score: 8
Impact Area: `user`, `syscall`, layer boundaries
Status: Closed
Notes:
- User-facing headers should depend on syscall ABI only.
- The old builtin-only `userprog.h` path has been removed entirely.
- `nlibc.h` and the split user headers continue to depend on the user-owned syscall ABI header (`user/public/sysapi.h`) instead of pulling `kernel/public/sys/syscall.h` directly into user code.

## TD-002
Title: VFS depends on syscall-facing and TTY-facing types
Applicable Modes: Solo, Small, Large
Risk Score: 9
Impact Area: `fs/vfs`, `kernel/fs_service`, syscall boundary
Status: Closed
Notes:
- `vfs_*` query/read/write APIs now stay on VFS-facing types, and the old public `fs_service*.h` layer exposing `struct tty *` and `struct syscall_dirent` was removed from `kernel/public`.
- Syscall-facing file/path services are now internal-only and consumed directly by syscall implementation units.
- Root-query services now also stay on VFS-facing APIs: `fs_service_root_query.c` enumerates the active root mount through `vfs_opendir_root_mount()` and `vfs_readdir()` instead of branching on FAT32/NXFS internals or casting `mount.fs_data`.
- The old public `file.h` surface is gone, and even internal shared file headers now use forward declarations instead of pulling `tty.h`/`vfs.h` into every consumer.
- The file and syscall-facing FD service layers no longer thread `struct tty *` through their operation signatures; TTY-aware behavior is now concentrated in the device backend implementation.
- Console-backed file devices now receive their endpoint through `file.private_data` / process-owned `console_handle`, so the device backend no longer directly depends on `g_user_tty`.
- Any remaining `g_user_tty` usage is outside the VFS/file service boundary and no longer represents a VFS-to-TTY type coupling debt.

## TD-003
Title: TTY directly references VGA driver details
Applicable Modes: Solo, Small, Large
Risk Score: 6
Impact Area: `kernel/tty`, display abstraction, HAL boundary
Status: Closed
Notes:
- `tty.c` no longer hardcodes the VGA ANSI palette or direct text width assumptions; those details now flow through `console` helpers.
- The remaining hardware-facing logic is concentrated in `console.c`/HAL, which is a better fit for display-specific behavior.
- Early boot trace code now also routes row/column writes through `tty`/`console` helpers instead of open-coding `hal_display_*` and `HAL_TEXT_*` logic in the kernel boot orchestration units.
- `console.h` is now opaque and the only remaining display-layout coupling lives in the internal console implementation, which is the intended ownership boundary for text-mode hardware details.

## TD-004
Title: Shell variable store and libc `environ` are duplicated
Applicable Modes: Solo, Small, Large
Risk Score: 6
Impact Area: `user/apps/elf/ush.c`, `user/libc/std/env.c`, process environment model
Status: Closed
Notes:
- Exported shell variables now use libc `environ` as the single source of truth; `ush` keeps only shell-only variables in its local table.
- `set NAME=value` now updates shell-local state only for non-exported vars, while `export` moves values into `environ` instead of mirroring them in two stores.
- `set` now shows only shell-local variables, while `export` and `env` show only the exported environment, so the user-facing boundary matches the storage boundary instead of blending the two views.

## TD-005
Title: `LSDEMO.ELF` and `/CMD/LS` still use different command implementations
Applicable Modes: Solo, Small, Large
Risk Score: 5
Impact Area: `user/apps/elf/ls.c`, `user/apps/elf/cmdsuite.c`, command consistency
Status: Closed
Notes:
- `LSDEMO.ELF` and `/CMD/LS` now share the same `cmd_ls_shared.c` implementation, so listing behavior and formatting no longer drift between the two binaries.
- The binaries remain separate entry points, but the duplicated `ls` logic itself is no longer carried in two places.

## TD-006
Title: Internal/public include boundaries still leak across kernel layers
Applicable Modes: Solo, Small, Large
Risk Score: 8
Impact Area: `kernel/public/*`, `kernel/internal/*`, layer direction
Status: Closed
Notes:
- Public headers were narrowed so `process.h`/`job_control.h` no longer expose `fat32_volume *`, and FS-facing public types were split into smaller type headers.
- Internal FS service headers were decomposed by role (`root_query`, `mount_query`, `path`, `fd`), root-query services now depend on VFS-facing APIs instead of direct FAT32/NXFS entry points, and syscall query code now reaches block/audio/PCI mechanism through core-owned query helpers instead of including driver/block headers directly.
- The old public `file.h` surface was fully internalized, so `struct file` and file-operation mechanisms now live behind internal FS headers only, and `kernel/public/proc/process.h` now exposes an opaque `struct process` plus `struct process_snapshot` for public query/wait APIs.
- `job_control.h` now also reports process state through `process_snapshot`, which reduces direct public dependence on kernel runtime object layouts.
- Internal FS service headers for `path` and `fd` were also narrowed to declaration-only boundaries with forward declarations, so concrete file/process/VFS includes now live in the implementation units instead of the shared internal interface layer.
- `kernel/public/core/tty.h` now exposes only the opaque `struct tty` and the TTY API; the concrete TTY layout moved to `kernel/internal/core/tty_internal.h`, so only `kernel.c`/`tty.c` and other implementation-side code that truly needs the storage layout can see it.
- `kernel/public/core/console.h` now follows the same pattern: the public surface is an opaque `struct console` plus the console API, while the concrete console layout lives in `kernel/internal/core/console_internal.h`.
- `struct vfs_mount_info` was moved into the public `vfs_types.h` minimal type surface, allowing the internal mount/root query headers to drop direct `fs/fat32.h` / `fs/nxfs.h` / `fs/vfs.h` includes in favor of forward declarations plus narrow public type headers.
- TTY-facing public APIs also no longer need to include the keyboard driver header directly; the minimal keyboard event/keycode ABI now lives in `kernel/public/input/keyboard_types.h`, while the driver keeps the implementation-specific scancode handling surface.
- `kernel/public/*` no longer directly includes `kernel/internal/*`, `fs/*.h`, `drivers/*`, or `hal/*` implementation headers as part of its exported surface, so the acute public/internal boundary leak for this cleanup wave is considered resolved.

## TD-008
Title: Syscall query layer reached block and driver mechanisms directly
Applicable Modes: Solo, Small, Large
Risk Score: 8
Impact Area: `kernel/sys/syscall_query*.c`, `kernel/core`, layer direction
Status: Closed
Notes:
- Query/tone/play paths that used to include `block/blockdev.h`, `drivers/bus/pci.h`, `drivers/audio/ac97.h`, and `drivers/audio/audio.h` now route through the core-owned `system_query` service instead.
- The syscall layer is back to user-buffer validation, marshaling, and forwarding, while the hardware-facing mechanism calls live under `kernel/core/system_query.c`.
- This keeps the SOSP direction as `syscall -> core service -> mechanism` instead of `syscall -> block/driver`.

## TD-009
Title: Bootstrapping still owns builtin FAT32 backing storage in `kernel/core`
Applicable Modes: Solo, Small, Large
Risk Score: 4
Impact Area: `kernel/core/kernel.c`, VFS bootstrap ownership
Status: Closed
Notes:
- Builtin FAT32/NXFS backing storage now lives inside `struct vfs`, so `kernel/core/kernel.c` no longer owns a separate `g_fat32` bootstrap object just to seed VFS.
- `vfs_init()` now initializes builtin storage internally, which keeps builtin filesystem lifetime and ownership inside the VFS layer instead of leaking it into boot orchestration code.
- Boot/init code also uses `vfs_node_file_size()` for probe logging, so the remaining bootstrap path no longer touches FAT32/NXFS-specific node storage directly.

## TD-007
Title: God files still concentrate multiple responsibilities
Applicable Modes: Solo, Small, Large
Risk Score: 7
Impact Area: `user/apps/elf/cmdsuite.c`, `user/apps/elf/ush*.c`, `kernel/core/kernel*.c`, `kernel/sys/syscall_query*.c`
Status: Closed
Notes:
- The largest original files in this cleanup wave were split into focused translation units:
  `ush.c` now delegates to `ush_exec.c`, `ush_parse.c`, `ush_editor.c`, and `ush_vars.c`;
  `kernel.c` now delegates to `kernel_boot.c`, `kernel_init.c`, and `kernel_panic.c`;
  `syscall_query.c` now delegates to `syscall_query_fat.c`, `syscall_query_mount.c`, `syscall_query_kmsg.c`, and `syscall_query_pci.c`.
- `cmdsuite` and FS service/query code were also decomposed; `cmdsuite` now splits text/basic commands, storage, proc, debug, and dispatch responsibilities across separate translation units, and the shared utility file is no longer doubled as the command router or mixed with unrelated command families.
- The remaining larger files in this area are now primarily single-responsibility implementation units rather than multi-domain “god files”, so the acute concentration risk for this cleanup wave is considered resolved.
