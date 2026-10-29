# ADR-0005: EEVDF scheduler with an exclusive class

- Status: accepted
- Date: 2026-10-29

## Context

The scheduler must be fair, support nice values, scale across CPUs and provide
a process type that consumes all available CPU when runnable.

## Decision

Implement per-CPU EEVDF runqueues with nice values and a tickless timer
(LAPIC/TSC-deadline, microsecond accounting). Add RT FIFO/RR and a
`SCHED_EXCLUSIVE` class that is picked first while runnable. IRQs, the timer and
kernel threads on other CPUs remain active; the exclusive task yields when it
blocks. A watchdog protects against a locked-up system.

## Consequences

- Fairness and low latency for normal tasks.
- Exclusive tasks can starve user space by design; kernel liveness is preserved.
- Load balancing and affinity need careful testing.
