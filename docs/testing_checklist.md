# Testing Checklist

This project currently uses three levels of verification:

- Fast build/ELF checks with `make check`
- QEMU smoke checks
- Manual boot validation with `make run` or `make dev`

## Fast Smoke Checks

Run these first after structural refactors:

```sh
make ARCH=x86_64 check
make ARCH=i386 check
make check-kernel
```

These checks currently verify that the kernel/userland artifacts build and that
the produced kernel ELF/image shape is sane. They do not replace a real boot
test.

## QEMU Smoke Checks

Use these after i386 process, syscall, VFS, driver, MM, or applet changes:

```sh
make check-i386-smoke
make check-i386-nexbox32-full
```

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
