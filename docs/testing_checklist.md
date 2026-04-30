# Testing Checklist

This project currently uses two levels of verification:

- Fast smoke checks with `make check`
- Manual boot validation with `make run` or `make dev`

## Fast Smoke Checks

Run these first after structural refactors:

```sh
make
make check
```

`make check` currently verifies:

- `build/kernel64.elf` exists and still has `LOAD` program headers
- `build/kernel64.elf` does not contain an `RWE` load segment
- the boot image contains `BOOT/NEX.ELF`
- the boot image contains `HOME/USH.ELF`
- the boot image contains `HOME/HELLO.ELF`
- the boot image contains `BOOTX.CFG`
- the ramdisk contains `HOME/USH.ELF`
- the ramdisk contains `HOME/HELLO.ELF`

These checks are intentionally simple. They do not replace a real boot test.

## Manual Boot Validation

Boot with:

```sh
make run
```

Use `make dev` if you want extra QEMU diagnostics.

Expected boot condition:

- the system boots to the text console
- `/HOME/USH.ELF` starts successfully
- the shell prompt appears

## Shell Smoke Checklist

Run these commands in `ush` after boot:

```text
help
pwd
ls /
ls /HOME
ps
run hello
runbg yielddemo
ps
wait
echo hello | grep hello | wc
run sleepdemo
run badptr
```

Expected results:

- `help` prints the shell command summary
- `pwd` prints the current working directory
- `ls /` and `ls /HOME` succeed without crashing
- `ps` prints process information
- `run hello` launches and returns cleanly
- `runbg yielddemo` creates a background process
- `wait` reaps a finished process cleanly
- `echo hello | grep hello | wc` succeeds as a multi-stage pipeline
- `run sleepdemo` returns after a visible delay
- `run badptr` should fail in a controlled way without corrupting the shell session

## Filesystem Smoke Checklist

If the shell is up, also verify basic path and file operations:

```text
cat /BOOTX.CFG
mounts
blk
parts
```

Expected results:

- `cat /BOOTX.CFG` reads the boot configuration file
- `mounts` shows the mounted filesystems
- `blk` shows block devices
- `parts` shows partition information

## Optional NXFS Check

If the second disk image is present, verify mount flow manually:

```text
mount auto 1 0 /mnt
ls /mnt
```

Expected result:

- mount succeeds if the second disk image is available
- listing `/mnt` does not panic or hang

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
