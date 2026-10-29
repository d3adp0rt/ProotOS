# MISRA C:2012 deviations

ProotOS uses C23 and an adapted subset of MISRA C:2012. This file records
deviations so that `-Werror` builds stay honest instead of silently ignoring
rules.

## Standing deviations

| Rule | Deviation | Rationale |
|---|---|---|
| Dir 4.8 / 4.9 | No function-like macros for some register/port access | Inline asm helpers are clearer and safer. |
| Rule 11.4 | Integer/pointer conversions at hardware boundaries | Required for MMIO, page tables, UEFI ABI. |
| Rule 11.6 | `uintptr_t` conversions for physical addresses | Kernel address model. |
| Rule 17.4 | `panic()` is `noreturn` but may be called from non-void functions | Documented via `__attribute__((noreturn))`. |
| Rule 21.6 | Custom `printf` instead of stdio | Freestanding environment. |
| Rule 22.1 | No `malloc/free` in the kernel | Explicit buddy/slab/vmalloc only. |

## Rules

- No hidden allocation and no allocation in IRQ/atomic context.
- Every deviation must be listed here with a reason.
- New code should prefer MISRA-compliant constructs; deviations are reviewed.
