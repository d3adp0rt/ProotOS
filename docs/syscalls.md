# Syscall ABI (M5)

ProotOS uses its own ABI, not Linux compatibility.

- Entry: `int $0x80` (DPL3 IDT gate).
- Number: `rax`.
- Arguments: `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`.
- Return: `rax`; negative values are `-errno`.
- The kernel switches to the thread's kernel stack via TSS.RSP0, runs the
  handler, and returns with `iretq`.

| Number | Name | Arguments | Returns |
|---:|---|---|---|
| 1 | `write` | fd, buf, len | bytes written / `-EFAULT`, `-EBADF` |
| 2 | `getpid` | — | pid |
| 3 | `yield` | — | 0 |
| 4 | `sleep_ms` | ms | 0 |
| 57 | `fork` | — | child pid in parent, 0 in child |
| 60 | `exit` | status | does not return |
| 61 | `wait4` | pid (-1 = any), status_ptr, options, rusage | child pid |

## User address space

| Region | Address |
|---|---|
| ELF image | `0x400000` upward |
| Stack top | `0x00007ffffff00000` |
| Stack guard | below the mapped 16 pages |
| User limit | `0x0000800000000000` |

Each process has its own PML4. Entries 256..511 (kernel, HHDM, framebuffer,
vmalloc) are copied from the kernel PML4; the lower half belongs to the process.

## Current limits

- `fork` deep-copies the address space; COW is planned.
- `execve`, `pipe`, signal delivery and `ptrace` are not implemented yet.
- All processes run as uid 0.
- `int 0x80` is the current entry; `syscall/sysret` is a later optimization.
