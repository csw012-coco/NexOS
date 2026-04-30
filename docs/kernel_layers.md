# Kernel Layers

## Goal

Related architectural rule set:
- [/home/csw012/nos/docs/SOSP.md](/home/csw012/nos/docs/SOSP.md)

This kernel follows a strict top-down dependency rule:

`user -> syscall -> core -> vmm/hal`

The goal is to keep policy in upper layers and hardware details in lower layers.

## Layer Summary

### `user`

User programs issue requests only through the user API and syscalls.

Examples:
- `/home/csw012/nos/user/apps/elf/ush.c`
- `/home/csw012/nos/user/apps/elf/hello.c`

Rules:
- Must not access kernel internals directly.
- Must not depend on VFS, scheduler, paging, or HAL internals.

### `syscall`

The syscall layer is a thin request boundary.

Examples:
- `/home/csw012/nos/kernel/sys/syscall.c`
- `/home/csw012/nos/kernel/sys/syscall_proc.c`
- `/home/csw012/nos/kernel/sys/syscall_fs_path.c`
- `/home/csw012/nos/kernel/sys/syscall_fs_fd.c`
- `/home/csw012/nos/kernel/sys/syscall_mem.c`

Responsibilities:
- Decode syscall numbers and arguments.
- Validate and copy user buffers.
- Forward requests to core services.

Rules:
- Must stay thin.
- Must not implement process, scheduler, filesystem, or paging policy.
- Must not call HAL paging helpers directly.

### `core`

The core layer owns kernel policy.

Examples:
- Process: `/home/csw012/nos/kernel/proc/process_core.c`, `/home/csw012/nos/kernel/proc/process_exec.c`
- Scheduler: `/home/csw012/nos/kernel/sched/scheduler_core.c`
- Jobs: `/home/csw012/nos/kernel/proc/job_control.c`
- Filesystem services: `/home/csw012/nos/kernel/fs/fs_service_path.c`, `/home/csw012/nos/kernel/fs/fs_service_fd.c`

Responsibilities:
- Process lifecycle, exec, wait, exit.
- Scheduling and foreground/background control.
- Filesystem service policy above VFS.

Rules:
- May call `vmm` and `hal` abstractions.
- Must not depend on architecture-specific paging details.
- Must not bypass shared service layers.

### `vmm`

The VMM layer is the memory-management abstraction boundary between core code and paging implementation.

Examples:
- `/home/csw012/nos/kernel/vmm.h`
- `/home/csw012/nos/kernel/mem/vmm.c`

Responsibilities:
- Mapping and unmapping.
- User buffer accessibility checks.
- Copy helpers for user memory.
- Address-space root switching.

Rules:
- Core code should prefer `vmm_*` over lower paging helpers.
- Expose intent-oriented operations, not page-table structure details.

### `hal`

The HAL and arch layers contain hardware-facing implementation details.

Examples:
- `/home/csw012/nos/hal/hal.h`
- `/home/csw012/nos/arch/x86/paging.c`
- `/home/csw012/nos/build` inputs under `arch/x86/` and `hal/`

Responsibilities:
- CPU, interrupts, paging implementation, platform operations.

Rules:
- May contain architecture-specific details.
- Must not absorb process or filesystem policy from upper layers.

## Shortcut Rule

Shortcut is forbidden.

Do not bypass intermediate layers just because a lower-level helper looks convenient.

Bad:
- `syscall -> hal`
- `syscall -> paging`
- `process/scheduler -> arch/x86 paging details`

Good:
- `syscall -> process`
- `syscall -> fs_service`
- `process -> vmm`
- `vmm -> hal`

## Dependency Rules

Allowed direction:

`user -> syscall -> core -> vmm -> hal`

Also allowed:

`core -> fs/vfs`

Not allowed:

`user -> core`
- direct kernel subsystem calls

`syscall -> hal`
- direct paging or CPU detail access

`core -> arch/x86 specific page-table functions`
- direct PML4/PTE-style coupling outside `vmm` or HAL

## Review Checklist

When adding or reviewing code, check these first:

1. Is this code calling across layers directly when a wrapper/service already exists?
2. Is syscall code staying thin, or is policy leaking into it?
3. Is core code relying on architecture details instead of `vmm` or HAL abstractions?
4. Does the file name still match the responsibility of the code inside it?
5. Is a new file actually reducing responsibility, or only spreading code around?

## Current Mapping

The current intended structure is:

- `user`
  `/home/csw012/nos/user/apps/elf/*`

- `syscall`
  `/home/csw012/nos/kernel/syscall*.c`

- `core`
  `/home/csw012/nos/kernel/process*.c`
  `/home/csw012/nos/kernel/sched/scheduler_core.c`
  `/home/csw012/nos/kernel/proc/job_control.c`
  `/home/csw012/nos/kernel/fs_service*.c`

- `vmm`
  `/home/csw012/nos/kernel/mem/vmm.c`

- `hal/arch`
  `/home/csw012/nos/hal/*`
  `/home/csw012/nos/arch/x86/*`

This document is intentionally short. If a future change conflicts with this layering, fix the layering first instead of adding a shortcut.
