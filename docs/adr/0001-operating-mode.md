# ADR 0001: Initial Operating Mode

Date: 2026-04-08
Status: Accepted

## Context

SOSP v1.4 requires the project to declare an operating mode and adjust enforcement accordingly.
The repository did not yet record that baseline decision.

## Decision

The project adopts `Solo / Bring-up` as the current SOSP operating mode.

## Consequences

- Core structural rules remain mandatory, especially shortcut prevention and layer direction.
- Governance checks are lightweight and primarily self-enforced.
- Technical debt may be deferred temporarily, but it must be recorded with impact and risk.
- Future transition to `Small Team / Stabilization` must add a migration plan and update ownership/review rules.
