# NexOS Terrible Code Audit

This document tracks code that matches the project-specific "terrible code"
signals from `user/init/find_terrible_code.txt`.

## xHCI / USB

### Global active controller copy

- File: `drivers/usb/xhci_core.c`
- Lines: global `g_xhci`, `g_xhci_controllers`, `g_xhci_active_controller`, and `g_xhci_busy`.
- Score: 15+ / explosive.

The xHCI driver keeps a mutable global active-controller copy and swaps it with
`xhci_select_controller()`. Normal paths can work, but interleaved HID polling,
MSC I/O, hotplug scans, or future IRQ-driven paths can mutate rings/MMIO through
the wrong active controller if the global state changes at the wrong time.

The current `volatile uint32_t g_xhci_busy` is not a real lock. It prevents some
reentrancy in cooperative paths, but it does not define ownership, IRQ masking,
or nested failure behavior.

Target shape:

- pass `struct xhci_state *xhci` through command, transfer, HID, MSC, and hub paths;
- replace global active-controller swapping with per-device controller pointers;
- replace `g_xhci_busy` with a real driver lock or explicit non-reentrant poll
  contract.

### Event consumers still own policy

- File: `drivers/usb/xhci.c`
- Lines: command wait and transfer wait loops directly pop events.
- Score: 9-12 / dangerous.

Command and transfer wait loops now match TRB physical addresses and defer some
wrong-type events, which fixed a large class of stale-event failures. The
remaining smell is architectural: multiple waiters still consume from the same
event ring and decide locally what to keep or drop.

Target shape:

- one event pump owns `xhci_pop_event()`;
- event pump routes by event type, slot, endpoint, and TRB pointer;
- command/transfer callers wait on request records instead of polling the raw
  event ring directly.

### HID interrupt timeout lifetime is ambiguous

- File: `drivers/usb/xhci_hid.c`
- Lines: pending interrupt report submit/wait path.
- Score: 8-11 / dangerous.

On timeout, an interrupt request remains pending. That can be valid if the event
arrives late, but there is no explicit request state saying whether the TRB is
live, timed out, recovered, or retired. A lost completion can leave keyboard or
mouse polling dependent on stale pending state.

Target shape:

- explicit HID request state: idle, submitted, completed, timed-out, recovering;
- timeout recovery decides whether to keep waiting, stop/reset endpoint, or
  retire the request;
- late events are matched to request generation IDs and never complete a new
  request accidentally.

### DMA allocation failure paths

- File: `drivers/usb/xhci_core.c`
- Status: partially fixed.
- Score before fix: 6-8 / dangerous.

`xhci_alloc_page()` allocated a PMM page and returned failure for pages above the
32-bit DMA range or direct-map failure without releasing the page. This is now
fixed by freeing the page before returning failure.

Remaining target:

- add a matching free path for partially allocated xHCI controller/device
  resources when later setup fails.

### Scratchpad requirement truncation

- File: `drivers/usb/xhci_core.c`
- Status: partially fixed.
- Score before fix: 8-10 / terrible.

The driver silently truncated the controller-reported scratchpad count to
`XHCI_MAX_SCRATCHPADS`. That can leave the controller configured with fewer
scratchpads than it asked for. The code now fails controller init instead of
silently creating a mismatched DCBAA.

Remaining target:

- either raise the supported scratchpad table size or allocate the scratchpad
  array dynamically from the exact controller requirement.
