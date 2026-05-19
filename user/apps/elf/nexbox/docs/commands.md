# NexBox Commands

NexBox is the NexOS multicall userland image.

The runtime binary is `NEXBOX.ELF`, and the normal entry path is `/CMD/NEXBOX`.

Design strategy:
- [Friendly CLI + Action CLI Strategy](/home/csw012/nos/docs/cli_action_strategy.md)

NexBox follows the NexOS fd-substrate rule: kernel resources stay accessible
through file descriptors, while NexBox adds action metadata, capability checks,
typed output, and automation on top.

```text
fd layer:      open/read/write/readdir on /dev, /proc, /event, and files
friendly CLI:  cat /event/file/change
action layer:  event.read source=file.change
policy layer:  caps such as event.read, fs.read, audio.play
```

## Invocation

NexBox supports two entry styles:

- direct alias execution
  `ls`
  `cat file.txt`
  `mount /dev/disk1p1 /mnt`
  `mount boot /new_root`

- multicall form
  `nexbox ls`
  `nexbox cat file.txt`
  `nexbox mount /dev/disk1p1 /mnt`

Both forms reach the same applet dispatcher.

## Shell Boundary

The `ush` shell still owns a small set of shell-only builtins:

- `cd`
- `exit [code]`
- `exec`
- `set`
- `export`
- `alias`
- `functions`
- `history`
- `source`
- `.`

These are not NexBox applets. They run inside the shell process itself.

NexBox applets are normal user commands executed through the multicall binary.

`ush` also owns command-line control syntax around those applets and builtins:

- `cmd1 ; cmd2`
- `cmd1 && cmd2`
- `cmd1 || cmd2`
- `cmd1 | cmd2 | cmd3`
- `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`
- `name { ... }` shell functions
- `source file [args...]` or `. file [args...]`

## Applet Groups

### Text

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/text/`

Commands:
- `help`
- `actions`
- `action`
- `mapper`
- `echo`
- `clear`
- `pwd`
- `tty`
- `env`
- `font`
- `which`
- `type`
- `cat`
- `less`
- `hexdump`
- `grep`
- `date`
- `hwclock`
- `wc`
- `head`
- `tail`
- `find`
- `on`
- `as`
- `pick`
- `select`
- `sort-by`
- `count-by`
- `to`
- `view`
- `session`
- `config`
- `uname`

Notes:
- `actions` lists NexOS Action Registry entries.
- `actions --table` emits the registry as a typed table for pipes, with quoted values when a column contains spaces.
- `action info <name>` prints an action's group, schemas, capability tags, and backing command.
- `action caps` prints the numeric capability flag legend used by `action info` and `/proc/actions`.
- `cat /proc/caps` prints the kernel-side capability registry, and `cat /proc/devices` prints devfs node permissions.
- `action allowed` prints the current action capability allow mask.
- `action policy` prints the Action Permission Policy from `/HOME/ACTION.CAPS`.
- `action policy explain <action>` explains why an action is allowed or denied.
- `action policy actions` prints every action with its current allow/deny decision.
- `action policy allow cap <cap|mask|all>` and `action policy deny cap <cap|mask|all>` update capability policy.
- `action policy allow action <name>` and `action policy deny action <name>` add action-specific policy rules.
- `action policy clear action <name>` removes an action-specific policy rule.
- `action allow <cap|mask|all>`, `action deny <cap|mask|all>`, `action allow action <name>`, `action deny action <name>`, `action clear action <name>`, and `action reset` are short forms for policy updates.
- `action list --table` is the same typed table form as `actions --table`.
- `action map` or `mapper` lists the Mapper Layer table from friendly command to action id, typed params, output format, and caps.
- `mapper --table` emits that map as a typed table for pipes.
- `mapper info <command>` shows one friendly command's mapped action object shape.
- `action run <name> [args...]` executes the action through its registered command.
- Dotted action names can also be invoked directly from `ush`, such as `file.read path=a.txt`.
- Named action arguments like `path=a.txt` are parsed against the action input schema.
- Named action arguments are reordered into schema order, so `audio.tone ms=500 hz=440` runs like `tone 440 500`.
- Typed schema fields validate basic values before execution, such as `int`, `path`, `host`, `word`, and `text`.
- `cat --json /event/...` renders EventFS event lines as a JSON array.
- `date --iso`, `date +%s`, and `date --raw` expose the CMOS RTC in ISO, Unix-time, and diagnostic forms.
- `hwclock` prints CMOS RTC mode, validity, raw status registers, and Unix time.
- `tty` prints the terminal connected to standard input, such as `/dev/tty`, `/dev/tty2`, `/dev/tty3`, or `/dev/ttyS0`.
- `font` shows the active text grid, cell size, and `/system/font/font.hex` availability; `font sample` prints a glyph sample.
- `/proc/rtc` exposes the same RTC snapshot for scripts.
- Friendly NexBox commands also use action capability checks through the Mapper Layer table.
  For example, `cat a.txt` is checked as `file.read` with `fs.read`.
- `session save <name>` writes `/SYSTEM/SESSION/images/<name>.simg` plus a restore script `<name>.ush`.
- `session list` lists saved session images.
- `session info <name>` prints the saved image metadata.
- `session load <name>` restores cwd and exported environment when run from `ush`; direct applet execution prints the source command to use.
- `service define <name> <command> [args...]` creates `/SYSTEM/SERVICE/<name>.svc`.
- `service enable <name>` marks a service for `service boot`.
- `service start|stop|restart <name>` manages the background process and stores runtime pid metadata in `<name>.run`.
- `service list` shows enabled/running state, pid, and command.
- `service boot` starts enabled services and is called by the default `INIT.SH` after `switch_root`.
- `service reconcile` starts enabled services that are not currently running.
- `service supervise [interval]` runs a foreground supervisor loop; use `service supervise 1s &` to run it as a daemon.
- `config` manages layered settings. Effective lookup order is runtime, user, then system.
- `config get <key>`, `config set [--user|--system|--runtime] <key> <value>`, `config unset ...`, `config list`, `config source <key>`, `config schema [key]`, and `config validate` are supported.
- `as table` marks a headered text stream as a NexOS typed table.
- `as event` converts EventFS text lines into a typed event table.
- `pick <column=value>` filters a typed table or headered text table.
- `select <column> [column...]` projects a typed table to selected columns.
- Table pipes preserve quoted values, so fields like `caps` and `summary` can flow into `to json`.
- `sort-by <column>` sorts typed table rows by a column.
- `count-by <column>` counts rows grouped by a column value.
- `to json` converts a typed table to JSON.
- `view table` renders a typed table without metadata lines.
- `on [-n interval] [-1] file.change <path> run <command> [args...]` watches a file and runs a friendly command or dotted action when its contents change.
- `on [-1] event.timer [interval=<ms>] run <command> [args...]` runs a command on `/event/timer` ticks.
- `on [-v] [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args...]` runs a command when matching keyboard events arrive; `-v` prints the matched event.
- `on [-v] [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args...]` runs a command when matching mouse events arrive.
- `on [-v] [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args...]` runs a command when matching network status events arrive.
- `on [-v] [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args...]` runs a command when block devices or partitions appear.
- `on --daemon ...` starts an event watcher as a background event job.
- `events as-table` is an alias for `as event`.
- `events jobs`, `events log <id>`, and `events stop <id|pid>` inspect and manage event jobs.
- `type` and `which` know about shell-only builtins as well as `/CMD/*` applets.
- Relative paths follow the current process working directory.

### Filesystem

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/fs/`

Commands:
- `ls`
- `touch`
- `mv`
- `cp`
- `mkdir`
- `rmdir`
- `rm`
- `stat`
- `du`
- `tree`
- `file`
- `blk`
- `parts`
- `fdisk`
- `df`
- `mounts`
- `hotplug`
- `fatls`
- `fatfind`
- `fatread`
- `mount`
- `umount`
- `switch_root`
- `progs`
- `run`
- `runelf`
- `runbg`

Notes:
- `run` executes through the normal resolver path.
- `runelf` forces ELF execution.
- `runbg` starts the command as a background job.
- `fdisk` currently targets MBR-style partition editing.
- `stat <path>` prints type, size, and raw filesystem attributes; `stat --table <path>` emits a typed table.
- `du [-a] [-s] [path]` summarizes file or directory usage; `du --table <path>` emits `path`, `size`, and `type`.
- `tree [path]` prints a recursive directory tree; `tree --table [path]` emits `path`, `depth`, `type`, and `size`.
- `file <path>` guesses basic file kinds such as ELF, WAV, CPIO, text, config, assembly, script, binary, and directory.
- `df` shows mounted filesystem space usage; `df --table` emits byte counts as a typed table.
- `hotplug scan` mounts discovered partitions at `/media_diskXpY` when possible.
- `hotplug mount <disk> <part>` mounts one partition at `/media_diskXpY`.
- `hotplug watch` starts a background event job that reacts to `/event/block/change`.

### Editor

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/editor/`

Commands:
- `ed`
- `vi`
- `vim`

Notes:
- `ed` supports `[address]command` style editing such as `,p`, `2,5d`, `$p`, and `3c`.
- `vi` and `vim` open the same small modal editor with normal, insert, and command modes.
- Mini vi normal mode supports `h/j/k/l`, arrow keys, `i`, `a`, `o`, `O`, `x`, and `dd`.
- Mini vi command mode supports `:w`, `:w <path>`, `:q`, `:q!`, `:wq`, and `:x`.

### Process

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/proc/`

Commands:
- `ps`
- `jobs`
- `wait`
- `kill`
- `fg`
- `bg`

Notes:
- `jobs` focuses on background-job state.
- `wait` supports both “last exited process” and `wait <pid>`.

### Audio

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/audio/`

Commands:
- `audio`
- `tone`
- `wav`
- `mplay`
- `ac97`
- `hda`

Notes:
- `mplay` is the user-facing WAV playback command.
- `./file.wav` style execution is routed to the WAV playback path by `ush`.

### Debug

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/debug/`

Commands:
- `dmesg`
- `lspci`
- `doctor`
- `meminfo`
- `minfo`
- `cpuinfo`
- `config`
- `dbg`

Notes:
- These applets are user-facing diagnostics, not shell builtins.
- `doctor` summarizes core system health across kernel identity, memory, procfs, EventFS, storage, process, RTC, network, and audio queries.
- `doctor --table` emits the same health checks as a typed table with `check`, `status`, and `detail` columns.
- The same kernel snapshots are also exposed through procfs-lite:
  `/proc/meminfo`, `/proc/mounts`, `/proc/uptime`, `/proc/rtc`, `/proc/kmsg`, `/proc/actions`, and `/proc/<pid>/status`.
- Storage drivers currently include legacy ATA and AHCI SATA. AHCI registers SATA disks as block devices such as `ahci0` and supports sector read/write with cache flush.
- EventFS exposes event sources through `/event`:
  `/event/timer`, `/event/input/keyboard`, `/event/input/mouse`,
  `/event/net/status`, `/event/file/change`, and `/event/block/change`.
  `/event/file/change` is backed by VFS create/write/truncate/mkdir/rmdir/unlink events, and
  `/event/net/status` is backed by queued network status changes.

### Assembler

Source:
- `/home/csw012/nos/user/apps/elf/nexbox/applets/asm/`

Commands:
- `asm`

Notes:
- This is a small assembler for NexOS, not a full upstream FASM port.

### Boot Root

`mount boot <target>` mounts the partition bootx loaded NexOS from. This avoids
hard-coding `/dev/disk0p1`, which can change on real hardware depending on disk
enumeration order.

## Current Command Line

The current built-in applet list exposed by `help` is:

`help actions action mapper echo clear pwd tty env font which type ls cat less hexdump grep date hwclock sleep watch on events wc head tail find as pick select sort-by count-by to view ed vi vim touch mv cp mkdir rmdir rm asm stat du tree file blk parts fdisk df mounts progs fatls fatfind fatread cpio mount umount hotplug run runelf runbg ps session service jobs wait alarm timeout kill fg bg reboot switch_root dmesg lspci ac97 hda rtl8139 rtl8139tx rtl8139rx arp route netstat ping dns dhcp ifconfig http wget nc audio tone wav mplay doctor nexctl sysinfo meminfo minfo uname cpuinfo config dbg`

## Naming Notes

- `NEXBOX.ELF` is the runtime image name.
- `/CMD/NEXBOX` is the multicall entry alias.
- Legacy `cmdsuite` naming may still appear in some internal symbol names during migration, but user-facing naming should prefer `NexBox`.
