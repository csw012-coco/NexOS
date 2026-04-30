# NexBox

NexBox is the NexOS multicall userland.

Current state:
- Runtime entry is provided by `NEXBOX.ELF`
- Image aliases expose it as `/CMD/NEXBOX` and `/HOME/NEXBOX.ELF`
- Source now lives under this directory

Layout:
- `core/`: multicall dispatch and shared helpers
- `applets/text/`: pager, grep, env/help-style text commands
- `applets/fs/`: ls helpers, file copy/move, mount, fdisk, storage tools
- `applets/editor/`: line editor commands
- `applets/proc/`: process and job-control commands
- `applets/audio/`: tone, wav, mplay, audio inspection
- `applets/debug/`: dmesg, pci, meminfo, machine/debug tools
- `applets/asm/`: mini-FASM assembler applet
- `docs/`: NexBox-specific command and applet notes

Docs:
- [commands.md](/home/csw012/nos/user/apps/elf/nexbox/docs/commands.md): command groups, multicall usage, and shell-builtin boundaries

Migration rule:
- keep build behavior stable while files are being reorganized
- preserve existing command names and image aliases
- keep the runtime image name aligned with `NexBox`
