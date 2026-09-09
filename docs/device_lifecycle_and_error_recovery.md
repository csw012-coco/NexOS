# Device Lifecycle and Error Recovery

## Purpose

This document defines the common lifecycle and error-recovery model for NexOS
drivers. It applies to storage, USB, network, audio, graphics, HID, and future
drivers. MSC is one important example, but the rules are not MSC-specific.

The goals are:

- prevent I/O from racing device removal;
- keep transient hardware failures inside the driver;
- make recovery bounded and observable;
- stop exposing stale hardware state to VFS and user processes;
- provide a predictable path from retry to device offline.

## Device States

Every asynchronous or removable device should have an explicit state:

```text
PROBING -> ONLINE -> RECOVERING -> ONLINE
                    |
                    v
                 OFFLINE

ONLINE -> REMOVING -> DETACHED
```

State meanings:

- `PROBING`: resources are being allocated and hardware is being initialized.
- `ONLINE`: new requests may be accepted.
- `RECOVERING`: new requests are rejected or queued; only the recovery owner
  may touch the affected hardware.
- `OFFLINE`: the device remains visible for diagnostics but rejects I/O.
- `REMOVING`: no new references may be acquired; existing I/O must drain.
- `DETACHED`: all hardware references and DMA resources have been released.

State transitions must be serialized. A driver must never publish a device as
`ONLINE` before its queues, DMA resources, endpoint state, and required device
metadata are ready.

## I/O Lifetime

A pointer returned from a device registry is not, by itself, a lifetime
guarantee. Any operation that uses a device must acquire a reference before
dereferencing it and release the reference on every exit path.

The minimum contract is:

```text
lookup by stable ID
-> acquire I/O reference
-> verify state is ONLINE
-> perform the complete operation
-> release I/O reference
```

Removal follows the reverse order:

```text
mark REMOVING
-> stop accepting new I/O
-> unregister from new lookups
-> wait for I/O references to reach zero
-> detach mounts/queues and cancel pending work
-> free DMA, rings, and driver-private resources

Filesystem mounts are long-lived device users. A mounted filesystem stores a
device reference for its entire mount lifetime and releases it only after
unmount has stopped filesystem access. Hotplug removal must therefore detach
or invalidate affected mounts before the block-device unregister path waits
for the remaining references to drain.
```

Query paths must follow the same rule as read and write paths. Device listing,
partition enumeration, procfs formatting, and diagnostic commands must use a
stable snapshot or hold a reference for the complete query.

The block layer exposes this contract as `blockdev_acquire(index)` and
`blockdev_release(dev)`. Acquire succeeds only for a registered `ONLINE`
device and increments its I/O reference count. Unregister marks the device
`REMOVING`, rejects new acquisitions, and waits for all acquired references to
drain before detaching it. `blockdev_get()` is retained only for legacy boot
and mount code; new runtime code must use acquire/release.

Do not return pointers to driver-owned names, queues, rings, or status objects
when the object can be removed or reinitialized. Copy data into a caller-owned
snapshot while the reference is held.

## Request Ownership and Serialization

Each hardware queue needs one clear owner. A request must not be submitted by
two callers at the same time unless the driver explicitly supports concurrent
submission.

For a serialized device request:

1. acquire the device request lock;
2. verify the device state and generation;
3. allocate and validate DMA resources;
4. submit the request;
5. wait for its completion or timeout;
6. validate completion status and actual length;
7. release DMA and request ownership.

The lock must cover command state that the device observes, including command
tags, endpoint rings, data toggles, recovery state, and shared transfer
buffers. A global lock is acceptable as a bootstrap solution, but per-device
serialization is preferred when more than one device can make progress.

NexOS block devices provide a per-device request lock around the driver
read/write/flush/reset callback. Hotplug removal marks the device `REMOVING`,
sets the active request cancellation flag, and waits for I/O references to
drain before unregistering the device. Drivers must make their wait loops
observe cancellation so this drain remains bounded.

## Timeout and Completion Rules

A timeout is not the same as a device protocol error. The driver must record
both:

- the transport result, such as timeout, stall, invalid completion, or short
  transfer;
- the protocol result, such as SCSI status or sense data.

Transport completion must be validated before protocol buffers are inspected.
At minimum, validate:

- completion code;
- request generation and ring epoch;
- expected TRB or descriptor address;
- actual transfer length or residual;
- command tag and response signature;
- buffer guard regions when DMA debugging is enabled.

Late completions from an aborted request must be ignored. A generation number or
equivalent request token must prevent a late event from completing a newer
request that reused the same ring slot or DMA page.

The block layer also exposes `blockdev_cancel_request(dev)` and
`blockdev_request_cancelled(dev)`. A block operation installs a bounded
deadline when it begins; driver wait loops should check the cancellation
predicate and return through their normal recovery path. Cancellation does not
make an in-flight DMA transfer safe by itself: the driver must still quarantine
the request, drain or reset the queue, and reject its late completion.

Each driver may provide `blockdev_reset_fn` through `dev->reset`. The common
`blockdev_reset(dev)` wrapper changes the state to `RECOVERING`, calls the
driver callback without holding the registry lock, and publishes `ONLINE` or
`OFFLINE` from the callback result. The callback owns the hardware-specific
sequence, including request serialization and queue cleanup.

Every block device keeps total failures, consecutive failures, the last error
code, and a short last-error reason. Generic block I/O records the result
automatically; drivers should call `blockdev_record_failure()` when they know a
more precise phase or protocol reason.

## Recovery Escalation

Recovery should be bounded and progressively stronger:

```text
retry same request
-> reset request/queue state
-> reset endpoint or channel
-> clear protocol-specific error state
-> reset the device or port
-> re-enumerate/reinitialize
-> mark OFFLINE
```

The exact operations are hardware-specific, but the escalation policy is
common. Recovery must not silently retry forever and must not allow normal I/O
to race with recovery.

For USB MSC BOT, the normal transport recovery sequence is:

```text
stop/finish the failed transfer
-> Bulk-Only Mass Storage Reset
-> ClearFeature HALT on bulk-in
-> ClearFeature HALT on bulk-out
-> reset xHCI endpoint state
-> reset transfer-ring dequeue state
-> delay for device recovery
-> retry the command
```

If this sequence fails repeatedly, a USB port reset or re-enumeration is safer
than reusing the old ring and command state.

## Failure Classification

Errors should be classified before choosing a recovery action:

| Class | Examples | Action |
| --- | --- | --- |
| transient | timeout, temporary not-ready, unit attention | delay and retry |
| endpoint/queue | stall, halted endpoint, ring state error | reset endpoint or queue |
| protocol | malformed response, invalid tag, bad length | discard request and recover |
| medium | bad sector, write protect, medium error | return I/O error; do not retry forever |
| removal | disconnect, inaccessible controller, no device | detach and reject new I/O |
| fatal | repeated recovery failure, corrupted DMA guard | offline device and preserve diagnostics |

Sense data is useful only when a valid protocol response was received. A
transport failure with no valid response must not be classified from stale sense
data. For example, `phase=1 status=255` in the MSC implementation represents a
bulk transport failure while sending the CBW, not a SCSI sense result.

## Queue and Filesystem Interaction

When a device enters recovery or removal:

- new block requests must be rejected or held behind the recovery owner;
- mounted filesystems must receive a bounded I/O error rather than hang;
- cached data from a failed request must be invalidated;
- partition metadata must not be published half-updated;
- device and partition queries must return a consistent snapshot;
- after detach, VFS mount objects must not dereference the old device pointer.

A filesystem operation must never assume that a block device remains present
between two separate registry lookups. Use a stable mount binding, a reference,
or an operation-specific snapshot.

## Observability

Every recovery attempt should record enough information to distinguish a
hardware problem from a driver state bug:

- device ID and controller/channel/endpoint;
- operation and logical address where applicable;
- request generation and ring epoch;
- completion code and residual/actual length;
- current device state;
- recovery step and attempt number;
- final action: retry, I/O error, offline, or detach.

Logs should be rate-limited for repeated failures. A concise summary should
remain available through the kernel message facility and device diagnostics.

## Driver Review Checklist

Before considering a driver stable, verify:

1. Is publication delayed until initialization is complete?
2. Can removal race with every public I/O and query path?
3. Does every hardware use hold a lifetime reference?
4. Are requests serialized or explicitly concurrent-safe?
5. Are timeouts and late completions handled?
6. Are response lengths, tags, signatures, and status fields validated?
7. Is recovery bounded and escalated?
8. Are failed buffers, rings, and caches invalidated or quarantined?
9. Does repeated failure transition the device to `OFFLINE`?
10. Can VFS and user diagnostics observe a consistent state?

## Application to NexOS

The current MSC work covers several parts of this model: serialized MSC I/O,
partition scanning before publication, transfer-length validation, sense-based
classification, DMA/ring quarantine, and offline handling after repeated
failures.

The remaining shared direction is to apply the same lifecycle contract to all
device registries and VFS bindings. In particular, raw `block_device *` values
should gradually be replaced with stable operation handles or scoped
references, and each driver should expose a bounded recovery path instead of
leaving recovery policy to callers.
