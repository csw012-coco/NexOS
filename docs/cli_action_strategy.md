# Friendly CLI + Action CLI Strategy

NexOS should keep familiar Unix-style commands while adding a semantic action layer underneath them.

The goal is not to replace the basic command line with a more difficult one.
The goal is to keep the friendly surface and map it to actions that the OS can understand, inspect, secure, and automate.

## Core Idea

Users may call either form:

```sh
cat a.txt
file.read path=a.txt
```

Both forms should reach the same feature.

```text
friendly command: cat a.txt
action command:   file.read path=a.txt
semantic action:  file.read
capability:       fs.read
output type:      text
```

Friendly commands are for humans.
Action commands are the stable semantic interface.

## Mapper Layer

The Mapper Layer is the official translation table between those two forms.
It maps a friendly command string to an action id, typed params, output format, and capability metadata.

```text
cat <path>  -> file.read path:path      text
ls [path]   -> fs.list  path?:path      dirent-list
ps          -> proc.list none           table/processes
```

Inside NexBox this table is inspectable:

```sh
mapper
mapper --table
mapper info cat
action map --table
```

This keeps the user-facing CLI easy while giving tools a stable action object shape:

```json
{"action":"file.read","params":{"path":"hi.txt"},"format":"text"}
```

## Strategy

NexOS keeps the Unix substrate and adds structured layers above it.

```text
Friendly CLI
  cat a.txt
  ls /HOME
  ps
  tone 440

Action Layer
  file.read path=a.txt
  fs.list path=/HOME
  proc.list
  audio.tone hz=440 ms=500

Capability Layer
  fs.read
  proc.read
  audio.play

Typed Output Layer
  text
  table
  json
  event
```

The short command remains valid. The action form exposes the meaning.

## EventFS

EventFS gives event sources a stable filesystem surface:

```text
/event/timer
/event/input/keyboard
/event/input/mouse
/event/net/status
/event/file/change
/event/block/change
```

This keeps event automation aligned with the rest of NexOS:

```sh
cat /event/timer
cat /event/input/keyboard
cat /event/block/change
cat --json /event/file/change
cat /event/file/change | as event | pick op=write | select type path bytes | to json
on event.timer interval=1000 run meminfo
on event.input.keyboard key=a run echo pressed-a
on event.input.mouse button=left run echo click
on event.net.status state=up run dhcp
on event.block.change op=partition run blk
hotplug scan
hotplug watch
on --daemon event.file.change /HOME/a.txt run build
events jobs
events log J123456
events stop J123456
on file.change /HOME/a.txt run build
```

`/event/input/keyboard` and `/event/input/mouse` are backed by kernel input queues.
Reading keyboard events does not steal input from the shell TTY. `/event/file/change`
is backed by VFS mutation events, `/event/net/status` is backed by queued network
status changes, and `/event/block/change` reports block device and partition discovery.

State remains in `/proc`, devices remain in `/dev`, and events live in `/event`.

## Design Rules

1. Friendly commands must remain available.
2. Friendly commands should map to action metadata whenever possible.
3. Action names should describe intent, not implementation.
4. Capability checks should attach to action semantics, not ad hoc command strings.
5. Typed output should be declared by the action and usable by pipes.
6. Automation should prefer action names because they are more stable and inspectable.
7. Event subscriptions should trigger actions, not raw shell fragments, when possible.

## Why This Matters

### Capability-First OS

Actions give the system a natural place to attach capability flags.

```text
audio.tone
  caps: audio.play
  cap_flags: 0x00000800
```

This allows NexOS to ask:

```text
Is this process allowed to perform audio.play?
```

instead of:

```text
Did the user type tone?
```

## Action Permission Policy

Action policy lives in `/HOME/ACTION.CAPS`. The file is intentionally readable:

```text
mask 0x00007FFF
deny cap net.raw
deny action net.dhcp
allow action net.config
```

Useful commands:

```sh
action policy
action policy explain net.dhcp
action policy deny cap net.configure
action policy deny action audio.tone
action policy allow action net.config
action policy reset
```

Explicit `deny action <name>` wins first. Explicit `allow action <name>` can permit a
single action even when its broader capability is disabled. Otherwise the action's
capability flags are checked against the allowed capability mask.

### Better UX

Beginners can keep using:

```sh
cat a.txt
ls /HOME
```

Power users and tools can use:

```sh
file.read path=a.txt
fs.list path=/HOME
```

The system supports both.

### Better Automation

Action output can become typed:

```sh
actions --table | pick group=audio | to json
actions --table | select name caps summary | to json
```

Scripts can inspect names, groups, capability flags, and output types without scraping arbitrary prose.

### Event API Direction

Future event subscriptions should follow the same model:

```sh
on file.change path=a.txt run build
```

Internally:

```text
event.subscribe file.change path=a.txt
action.run build
```

This keeps event handling discoverable and policy-aware.

NexOS starts with a small polling-backed form:

```sh
on file.change a.txt run build
on -n 250ms -1 file.change a.txt run file.read path=a.txt
```

The `-1` form exits after the first event, which is useful for tests and scripts.

## Current Implementation Direction

The current NexOS bring-up uses these first steps:

- `actions` lists registered actions.
- `actions --table` emits registered actions as a NexOS typed table with quoted values.
- `action info <name>` shows action metadata.
- `action run <name> [args...]` executes an action through its backing command.
- `/proc/actions` exposes action metadata to scripts.
- `on file.change <path> run <command> [args...]` runs a command when a file changes.
- Friendly commands executed through NexBox are checked through the Mapper Layer table.
- Dotted action names can be invoked directly from `ush`, for example:

```sh
file.read path=a.txt
```

- Named arguments such as `path=a.txt` are parsed against the action input schema.
- Named arguments are reordered into schema order before the backing command runs.
- Typed schema fields such as `hz:int`, `path:path`, and `host:host` are validated before execution.

## Examples

```sh
cat a.txt
file.read path=a.txt
```

Same feature, different entry points.
Both entry points are policy-aware: if `fs.read` is denied, `cat a.txt` and
`file.read path=a.txt` are both denied.

Current friendly mappings include:

```text
cat      -> file.read
ls       -> fs.list
cp       -> fs.copy
mounts   -> fs.mounts
blk      -> fs.block_devices
ps       -> proc.list
jobs     -> proc.jobs
kill     -> proc.kill
ping     -> net.ping
dns      -> net.dns
dhcp     -> net.dhcp
ifconfig -> net.config
wget     -> net.http_get
audio    -> audio.list
tone     -> audio.tone
mplay    -> audio.play_wav
dmesg    -> debug.kmsg
lspci    -> debug.pci
meminfo  -> system.mem
cpuinfo  -> system.cpu
session  -> session.image
uname    -> system.uname
select   -> table.select
sort-by  -> table.sort
count-by -> table.count
to       -> table.json
view     -> table.view
```

```sh
action info audio.tone
audio.tone hz=440 ms=500
audio.tone ms=500 hz=440
audio.tone hz=abc ms=500
```

The action form makes the semantic target explicit.
Both `audio.tone` examples call the backing `tone` command as `tone 440 500`.
The final example is rejected by the action layer because `hz` must be an integer.

```sh
actions --table | select name caps
actions --table | pick group=table | view table
actions --table | select name caps summary | to json
```

Action metadata participates in typed pipes.

## Roadmap

1. Expand the friendly command to action mapping where one command has multiple semantic modes.
2. Move action metadata and friendly mappings toward a shared registry to avoid duplicate tables.
3. Expand typed validation with richer kinds such as enum, existing path, writable path, and URL.
4. Add event subscriptions that trigger actions.
