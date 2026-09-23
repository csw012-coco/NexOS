# Testing Checklist

This project currently uses three levels of verification:

- Fast build/ELF checks with `make check`
- QEMU smoke checks
- Manual boot validation with `make run` or `make dev`

## Stabilization Gates

Use these named gates for the current stabilization effort. A stabilization
task is not complete until the relevant gate passes on both x86_64 and i386, or
the gap is explicitly documented as partial coverage.

```sh
make check-stabilization-base
make check-stabilization-smoke
make check-stabilization-mm
make check-syscall-invalid
make check-vfs-block-stress
make check-stabilization-driver
```

`make check-stabilization` runs all current non-loop stabilization gates. Use
`make check-boot-loop` for repeated boot validation; it defaults to 10
x86_64/i386 iterations and can be adjusted with `BOOT_LOOP_COUNT=50` or
`BOOT_LOOP_COUNT=100` for release-candidate runs.

Current coverage notes:

- `check-x86_64-mm-stress` runs `/cmd/mmstress pmm-vmm` and validates
  MAP_FIXED/partial `munmap`, fault cleanup, invalid-pointer cleanup,
  fork/COW cleanup, shared-mmap lifecycle behavior, PMM/VMM mmap pressure,
  and child-exit page cleanup.
- `/cmd/mmstress` defaults to the same strict-MM + PMM/VMM coverage. Use
  `/cmd/mmstress strict-mm` for only the historical strict-MM set, and
  `/cmd/mmstress pmm-vmm-only` for only the PMM/VMM pressure path.
- Use `/cmd/mmstress soak [iterations]` or `/cmd/mmstress pmm-vmm-soak
  [iterations]` for long PMM/VMM leak checks. The soak mode compares PMM
  `free_pages` before and after every iteration and again at the end. The
  default is 256 iterations; the current maximum is 10000.
- `check-i386-mm-stress` combines strict-MM and full NEXBOX32 smoke so i386
  process lifetime, fork/COW, mmap, shared mapping, and cleanup paths stay
  covered.
- Crash, hang, stale-pointer exposure, and data corruption found by these gates
  are immediate bugs. Do not file them as deferrable technical debt.

## Fast Smoke Checks

Run these first after structural refactors:

```sh
make ARCH=x86_64 check
make ARCH=i386 check
make check-kernel
make check-stabilization-base
```

These checks currently verify that the kernel/userland artifacts build and that
the produced kernel ELF/image shape is sane. They do not replace a real boot
test.

## QEMU Smoke Checks

Use these after i386 process, syscall, VFS, driver, MM, or applet changes:

```sh
make check-x86_64-nexbox-full
make check-x86_64-stress
make check-i386-smoke
make check-i386-nexbox32-full
make check-i386-strict-mm
```

`make check-x86_64-stress` repeatedly exercises the stable boot path, `dbg
stability`, `/ram` runtime file creation/removal, and service
list/info/start/duplicate-start/stop. Use it after service, VFS, syscall,
scheduler, or block I/O recovery changes.

Use `make check-syscall-invalid` after syscall validation changes. Use
`make check-vfs-block-stress` after VFS, pseudo-fs, block query, mount, or
runtime block-device lifetime changes. Use `make check-stabilization-driver`
after driver probe, query, recovery, or backend smoke changes.

Useful i386 TEST32 commands after boot:

```text
/cmd/test32 fork
/cmd/test32 fork-cow-cleanup
/cmd/test32 fork-map-table
/cmd/test32 fork-mmap-exec
/cmd/test32 fork-wait-exec
/cmd/test32 exec-fail-cleanup
/cmd/test32 mmap-fixed
/cmd/test32 mmap-prot
/cmd/test32 mprotect
/cmd/test32 shared-fault-cleanup
/cmd/test32 invalid-pointer-cleanup
/cmd/test32 shm-lifecycle
```

## Manual Boot Validation

Boot with:

```sh
make ARCH=x86_64 run
make ARCH=i386 run
```

Use the architecture-specific dev/run targets if you want extra QEMU diagnostics
or a particular virtual device set.

Expected boot condition:

- the system boots to the framebuffer/text console
- `/system/init` runs and returns
- `/cmd/ush` starts successfully
- the shell prompt appears

## Shell Smoke Checklist

Run these commands in `ush` after boot:

```text
help
pwd
ls /
ls /boot
ls /cmd
ls /system
ls /proc
ls /event
ps
run /cmd/test32
sleep 1 &
ps
wait
config get init
config set --runtime shell.prompt nex_prompt
config source shell.prompt
config unset --runtime shell.prompt
config validate
service define demo sleep 1
service enable demo
service reconcile
service list
service disable demo
service set demo restart on-failure
service set demo backoff_ms 250
service set demo max_retries 3
service info demo
echo hello | grep hello | wc
```

Expected results:

- `help` prints the shell command summary
- `pwd` prints the current working directory
- `ls /`, `/boot`, `/cmd`, `/system`, `/proc`, and `/event` succeed without crashing
- `ps` prints process information
- `run /cmd/test32` launches and returns cleanly
- `sleep 1 &` creates a background job with a printed PID
- `wait` reaps a finished process cleanly
- `config get/set/source/unset/validate` handles layered settings cleanly
- `service reconcile` starts enabled services that are not running
- `echo hello | grep hello | wc` succeeds as a multi-stage pipeline
- failed external commands return to the prompt without corrupting the shell session

## Filesystem Smoke Checklist

If the shell is up, also verify basic path and file operations:

```text
ls /boot
cat /proc/mounts
mounts
blk
parts
df
```

Expected results:

- `/boot` lists the mounted boot filesystem
- `/proc/mounts` and `mounts` show the mounted filesystems
- `blk` shows block devices
- `parts` shows partition information
- `df` reports mounted filesystem space information

## Optional NXFS Check

If the second disk image is present, verify mount flow manually:

```text
mount auto 1 0 /mnt
ls /mnt
```

Expected result:

- mount succeeds if the second disk image is available
- listing `/mnt` does not panic or hang

## Optional HD Audio Check

Boot with:

```sh
make run-hda
```

Then run:

```text
hda
audio
```

Expected results:

- `hda` reports controller state when the QEMU device is present
- `audio` lists available audio devices or reports a clean missing-hardware state

Audio, network, USB, and graphics checks are still experimental. Treat successful
smoke output as coverage of the current path, not a claim of complete device
support.

## Optional Graphics Check

When the framebuffer is available, run:

```text
fb
fb --smoke
fb --blit-smoke
```

Expected results:

- `fb` reports framebuffer geometry and address information
- `fb --smoke` and `fb --blit-smoke` complete without panicking
- graphics unavailable is reported cleanly when the boot mode or device set does
  not expose a framebuffer

## Fault Regression Checks

These are good to run after memory-management or syscall changes:

- invalid user pointer access still fails cleanly
- `page_alloc` still returns a user page address, and `page_free` succeeds only when given that same user page address
- `dbg stability` reports current credentials, the last syscall trace, and block device state without requiring extra debug capabilities
- `wait`, `kill`, `fg`, and `bg` reject bad PIDs cleanly
- the shell stays responsive after a failed `run`

## When To Run

Run at least `make check` after:

- process or scheduler changes
- syscall or VMM changes
- image layout or boot configuration changes
- filesystem service or VFS changes

Run the full boot checklist after:

- exec or session changes
- paging or user-memory changes
- shell changes
- filesystem refactors
- i386 boot/service glue, driver discovery, or syscall adapter refactors

Run the stabilization gates after:

- syscall boundary changes: `make check-syscall-invalid`
- MM/process lifetime changes: `make check-stabilization-mm`
- VFS/block/device lifetime changes: `make check-vfs-block-stress`
- driver backend changes: `make check-stabilization-driver`
- release-candidate validation: `make check-stabilization` plus
  `make check-boot-loop BOOT_LOOP_COUNT=100`

## Next Stabilization Debts

Use this list to choose the next high-value hardening task:

- Timer IRQ USB split: verify with `make check-stabilization-driver` and USB
  HID/MSC manual smoke, then run `make check-boot-loop`.
- Kernel stack guard and double-fault IST: verify with a deliberate debug-only
  stack overflow/fault injection path and panic diagnostics.
- `blockdev` async queue lock split: verify with `make check-vfs-block-stress`
  and USB/rootfs rebind smoke.
- Panic backtrace: verify by triggering a controlled kernel panic and checking
  serial output for a stable frame chain.
- Common lock/IRQ-safe rules: verify with lock-order assertions in debug builds
  before any SMP work.
- Memory poisoning/debug build: verify with PMM/VMM soak, invalid-free tests,
  and `make check-stabilization-mm`.
