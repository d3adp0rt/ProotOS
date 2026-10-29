# ADR-0003: musl as the C library

- Status: accepted
- Date: 2026-10-29

## Context

The system needs a C library, pthreads, dynamic linking and a compiler-friendly
environment. glibc is tightly coupled to the Linux kernel ABI (`rseq`, `vDSO`,
`/proc`, Linux syscalls), while ProotOS deliberately has its own syscall ABI.

## Decision

Port musl. Replace its Linux syscall layer with ProotOS syscalls, provide
`crt1`, `ld.so`, TLS and the arch layer. Keep a minimal in-tree libc for early
userland before musl is ready.

## Consequences

- Clean, portable libc with a known-good dynamic loader.
- Porting and maintaining the musl syscall layer is real work.
- glibc is explicitly out of scope.
