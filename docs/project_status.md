# Project Status

Operating Mode: Solo / Bring-up

Why This Mode:
- The project is currently being developed as a single-developer kernel bring-up and stabilization effort.
- Architectural rules still apply, but enforcement is primarily self-review and lightweight build checks.

Current Enforcement:
- SOSP-01, SOSP-02, SOSP-03, SOSP-06, SOSP-07, SOSP-11 are treated as must-hold rules.
- SOSP-04, SOSP-10, SOSP-12, SOSP-15 use warning-level enforcement unless risk becomes immediate.
- Technical debt must be registered even when immediate cleanup is deferred.

Review Cadence:
- Self-review on each structural change.
- Monthly SOSP review for debt and layer-boundary drift.

Current Focus Areas:
- Keep the Friendly CLI + Action CLI strategy aligned across shell, NexBox, procfs, and capability work.
- Keep README/docs aligned with implemented behavior; mark smoke-only, stub, and experimental paths explicitly.
- Finish i386 structural parity by shrinking remaining scheduler/process glue and split syscall adapters into thin arch backends.
- Continue fork/COW/MM cleanup validation for per-process mapping tables, shared mappings, and fault exits.
- Continue driver/backend parity validation for AHCI, EHCI/XHCI, USB MSC/HID, RTL8139, AC97/HDA, framebuffer, editor, and Doom-like user programs.
- Re-review VFS, pseudo-fs, and mount-query surfaces for leftover service-to-filesystem shortcuts.
- Keep build and docs aligned with SOSP v1.4 governance requirements.

Related Strategy Docs:
- [Friendly CLI + Action CLI Strategy](cli_action_strategy.md)
