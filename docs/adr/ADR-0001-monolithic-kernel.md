# ADR-0001: Monolithic kernel with in-kernel drivers

- Status: accepted
- Date: 2026-10-29

## Context

ProotOS targets a demo/research prototype with storage, USB, network, audio and
GUI. A microkernel would require IPC, user-space driver servers and a more
complex boot path before the first useful demo.

## Decision

Use a monolithic kernel. Drivers live in the kernel. Provide a minimal
ELF module loader and a UIO-like API for optional user-space drivers.

## Consequences

- Faster bring-up and simpler debugging.
- Kernel bugs can take down the system.
- Driver isolation is limited; user-space drivers are opt-in.
