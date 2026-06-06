dmesg compatibility helper scripts

This folder provides small helper scripts to work around missing `dmesg -T` / `dmesg -C` and `grep -E` on minimal systems.

Files:
- `dmesg_to_human.sh` : converts dmesg lines that start with `[123.456789]` into human relative time `[HH:MM:SS.mmm]` so you don't need `dmesg -T`.
  Usage examples:
    ./dmesg_to_human.sh            # runs `dmesg` and prints converted timestamps
    ./dmesg_to_human.sh > out.txt  # save to file
    cat serial.log | ./dmesg_to_human.sh  # convert a saved log piped in

- `grepE_compat.sh` : portable wrapper for extended-regex search.
  Usage example:
    ./grepE_compat.sh "HDAMOD|DBG|underrun|empty" serial.log

Notes on clearing the kernel message buffer (`dmesg -C`):
- Clearing the kernel ring buffer requires privileges and support from the kernel/userland `dmesg` utility. On many minimal systems (or custom OSes) `dmesg -C` is not available.
- There is no completely portable, safe way to clear the kernel ring buffer from a script on all targets. The usual approaches are:
  - Run `dmesg -C` as root (if available).
  - Use a small C helper that calls the kernel klogctl/syslog syscall (requires headers and privileges).
  - Reboot the machine.

If you want, I can add a small C utility that attempts to call `klogctl(SYSLOG_ACTION_CLEAR, ...)` to clear the ring buffer on systems where that syscall is available. Tell me if you want that and whether you can build it on the target (gcc present) or prefer a host-side build.

How to capture logs on the device without `-T`/`-C`:
1. Start playing audio on the device until the underrun/empty logs appear.
2. On the device, dump dmesg to a file:

    dmesg > /tmp/dmesg_raw.txt

3. Transfer `/tmp/dmesg_raw.txt` to the host (or paste it here). On the host run:

    ./tools/dmesg_to_human.sh < /tmp/dmesg_raw.txt > dmesg_T.txt
    ./tools/grepE_compat.sh "HDAMOD|DBG|underrun|empty" dmesg_T.txt > hda_debug_excerpt.txt

4. Send me `hda_debug_excerpt.txt` (or paste contents) and I'll analyze timing and indices.
