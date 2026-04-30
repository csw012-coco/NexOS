# Naming Conventions

## Goal

This document keeps naming consistent across files and layers.

The main rule is simple:

`name should reveal ownership, level, and intent`

Examples:
- `syscall_handle_open`
- `fs_service_open`
- `vfs_open`
- `fat32_find_path`
- `nxfs_lookup_path`

These names tell us:
- which layer owns the function
- whether it is a boundary function or an internal helper
- whether it works on paths, FDs, sessions, mappings, or devices

## Prefix Rules

Use the file or subsystem prefix first.

Examples:
- `kernel_*` for kernel boot and panic helpers in `/home/csw012/nos/kernel/core/kernel.c`
- `process_*` for process lifecycle and exec helpers
- `job_*` for foreground/background job control
- `sched_*` for scheduler-facing helpers
- `syscall_*` for syscall boundary helpers
- `fs_service_*` for filesystem service layer code
- `vfs_*` for virtual filesystem routing and device-node logic
- `fat32_*` for FAT32 implementation details
- `nxfs_*` for NXFS implementation details
- `vmm_*` for virtual memory mapping helpers

Do not mix prefixes unless the function clearly belongs to another subsystem.

Bad:
- `open_file_handle` inside `fs_service_fd.c`
- `lookup_mount` inside `vfs_mount.c`

Good:
- `fs_service_open_node`
- `vfs_find_dynamic_mount`

## ABI And Userland Prefixes

Use prefix families consistently across the kernel ABI and user libc surface.

- `SYS_*`: raw syscall numbers and syscall ABI constants
- `NEX_*`: NexOS-specific user-facing extensions layered on top of the syscall ABI
- `O_*`: file open flags exposed in libc-style headers
- `STDIN_FILENO`, `STDOUT_FILENO`, `STDERR_FILENO`: standard fd constants

Examples:
- `SYS_WRITE` is the syscall number
- `write()` is the libc wrapper
- `SYS_QUERY_AUDIO` is the syscall query kind
- `NEX_QUERY_AUDIO` is the NexOS user-facing alias
- `SYS_OPEN_CREAT` maps to libc `O_CREAT`

This separation matters because it keeps these roles distinct:
- kernel ABI numbers and wire-format enums
- libc/POSIX-like wrapper functions
- NexOS-only convenience APIs

Prefer:
- `SYS_EXIT`, `SYS_OPEN`, `SYS_QUERY`
- `write`, `read`, `open`, `close`
- `NEX_READ_CHAR`, `NEX_AUDIO_CAP_TONE`

Avoid:
- reusing syscall-number names for libc wrappers
- mixing `SYS_*` and `NEX_*` in the same layer without a reason
- inventing a third prefix family for the same ABI concept

## Convenience Headers

Use umbrella headers only as convenience layers, not as the canonical home of an API.

- `unistd.h` / `fcntl.h`: the primary libc headers for file and fd APIs
- `file.h`: convenience umbrella for quick user-app development

Recommended usage:
- use `file.h` when writing small apps quickly
- use `unistd.h` / `fcntl.h` when you want clearer dependency boundaries

Keep the ownership rule intact:
- declarations should primarily live in the standard-shaped headers
- umbrella headers should mostly re-export them, plus a small number of clearly convenience-oriented helpers

## Layer Signals

Names should reflect the layer boundary.

Use `handle` for syscall entrypoints:
- `syscall_handle_exec`
- `syscall_handle_mount_query`

Use service verbs in the core layer:
- `fs_service_open`
- `fs_service_read`
- `process_exec`

Use concrete implementation verbs in filesystem backends:
- `fat32_read_file_range`
- `nxfs_write_inode`

Use hardware-agnostic intent in VMM/HAL boundaries:
- `vmm_user_writable`
- `vmm_apply_mapped_range`

Avoid names that hide the layer:
- `do_exec`
- `run_query`
- `helper1`

## Helper Naming

File-local helpers should still be explicit.

Prefer:
- `*_valid`
- `*_ready`
- `*_reset`
- `*_init`
- `*_prepare`
- `*_restore`
- `*_release`
- `*_lookup`
- `*_find_*`
- `*_copy_*`
- `*_calc_*`
- `*_apply_*`

Examples from the current tree:
- `kernel_boot_info_valid`
- `process_init_reserved_window`
- `vfs_can_access_file_node`
- `fat32_calc_sector_window`
- `nxfs_resolve_parent_dir`
- `vmm_apply_mapped_range`

Avoid vague helper names:
- `check`
- `work`
- `common`
- `temp`
- `misc`

## File-Local Ordering

Within a `.c` file, prefer this order:

1. constants and local structs
2. low-level memory/format helpers
3. validation and calculation helpers
4. lookup/select helpers
5. lifecycle or state-transition helpers
6. exported functions

This keeps readers from jumping around to understand dependencies.

Example shape:
- small `copy`/`set` helpers first
- then `*_valid`, `*_calc_*`, `*_find_*`
- then higher-level `open`, `mount`, `exec`, `read`, `write`

## Function Verb Choices

Use verbs consistently:

- `init`: initialize an object or subsystem for first use
- `reset`: clear state back to an empty/default state
- `prepare`: set up state for an upcoming operation
- `restore`: put state back after a temporary change
- `bind`: connect active global/context state to another object
- `lookup` / `find`: search existing state
- `alloc`: reserve a new slot/page/cluster/extent
- `free` / `release`: give resources back
- `read` / `write`: transfer data
- `mount`: attach a filesystem
- `open`: create a handle to an object
- `truncate`: shrink logical file contents to zero or a smaller size

Prefer `find` when searching by predicate through a set.
Prefer `lookup` when resolving a name/path/key to an object.

## Wrapper Names

If a function is just delegating to a lower-level helper, the name should still justify its existence.

Good wrappers:
- convert naming or ownership boundaries
- narrow a generic helper to a specific subsystem meaning
- enforce validation before forwarding

Examples:
- `syscall_write_process_info`
- `fs_service_open_node`
- `vfs_store_mount_entry`

If a wrapper adds no meaning, remove it instead of keeping a second name.

## State Names

State-related helpers should describe transition direction.

Prefer:
- `process_mark_exited`
- `process_clear_current`
- `job_restore_foreground_context`
- `session_finish`

Avoid:
- `process_done`
- `job_fix`
- `session_handle`

## Review Checklist

When naming new code, ask:

1. Does the prefix reveal the owning subsystem?
2. Does the verb reveal whether this validates, finds, prepares, or mutates?
3. Would the same name still make sense if moved to another file?
4. Is this helper naming a real concept, or only hiding a one-line wrapper?
5. Does the exported function name match the layer where it lives?

## Shortcut Rule

Do not name a function after the lower layer it happens to call unless that lower layer is its real owner.

Bad:
- `syscall_vmm_copy`
- `process_hal_map`

Good:
- `syscall_prepare_user_output`
- `process_map_exec_stack`

This keeps names aligned with ownership, not just implementation detail.
