/*
 * ProotOS — exception diagnostics.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "proc.h"

static const char *exception_names[32] = {"divide error",
                                          "debug",
                                          "NMI",
                                          "breakpoint",
                                          "overflow",
                                          "bound range",
                                          "invalid opcode",
                                          "device not available",
                                          "double fault",
                                          "coprocessor segment overrun",
                                          "invalid TSS",
                                          "segment not present",
                                          "stack fault",
                                          "general protection",
                                          "page fault",
                                          "reserved",
                                          "x87 floating point",
                                          "alignment check",
                                          "machine check",
                                          "SIMD floating point",
                                          "virtualization",
                                          "control protection",
                                          "reserved",
                                          "reserved",
                                          "reserved",
                                          "reserved",
                                          "reserved",
                                          "reserved",
                                          "hypervisor injection",
                                          "VMM communication",
                                          "security",
                                          "reserved"};

static uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

void exception_dispatch(struct trap_frame *tf) {
    if (tf->vector < 32) {
        kprintf("\nEXCEPTION %llu: %s (error=0x%llx)\n", (unsigned long long)tf->vector,
                exception_names[tf->vector], (unsigned long long)tf->error);
    } else {
        kprintf("\nEXCEPTION %llu (error=0x%llx)\n", (unsigned long long)tf->vector,
                (unsigned long long)tf->error);
    }
    kprintf("  RIP=%p CS=0x%llx RFLAGS=0x%llx\n", (void *)tf->rip, (unsigned long long)tf->cs,
            (unsigned long long)tf->rflags);
    kprintf("  RSP=%p SS=0x%llx\n", (void *)tf->rsp, (unsigned long long)tf->ss);
    kprintf("  RAX=%p RBX=%p RCX=%p RDX=%p\n", (void *)tf->rax, (void *)tf->rbx, (void *)tf->rcx,
            (void *)tf->rdx);
    kprintf("  RSI=%p RDI=%p RBP=%p\n", (void *)tf->rsi, (void *)tf->rdi, (void *)tf->rbp);
    if (tf->vector == 14) {
        kprintf("  CR2=%p (faulting address)\n", (void *)read_cr2());
    }
    if ((tf->cs & 3) == 3) {
        int code = (tf->vector == 14) ? 139 : (int)(128 + tf->vector);
        kprintf("[proc] user-mode exception -> signal %d\n", code - 128);
        process_exit(code);
    }
    panic("unrecoverable CPU exception %llu", (unsigned long long)tf->vector);
}
