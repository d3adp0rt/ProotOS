/*
 * ProotOS — final transition from UEFI to the kernel.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loader.h"

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t gdt[3] = {
    0x0000000000000000ULL,
    0x00af9a000000ffffULL, /* 64-bit kernel code */
    0x00af92000000ffffULL, /* 64-bit kernel data */
};

static struct gdt_descriptor gdtr = {
    .limit = sizeof(gdt) - 1,
    .base = (uint64_t)(uintptr_t)gdt,
};

static void load_gdt(void) {
    __asm__ volatile("lgdt %0\n"
                     "pushq $0x08\n"
                     "leaq 1f(%%rip), %%rax\n"
                     "pushq %%rax\n"
                     "lretq\n"
                     "1:\n"
                     "movw $0x10, %%ax\n"
                     "movw %%ax, %%ds\n"
                     "movw %%ax, %%es\n"
                     "movw %%ax, %%fs\n"
                     "movw %%ax, %%gs\n"
                     "movw %%ax, %%ss\n"
                     :
                     : "m"(gdtr)
                     : "rax", "memory");
}

static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void jump_to_kernel(uint64_t entry, uint64_t bootinfo_virt, uint64_t pml4_phys) {
    __asm__ volatile("cli");

    /* Make sure 5-level paging is off: our tables are 4-level. */
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~(1ULL << 12); /* CR4.LA57 */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    /* NX is used for HHDM/framebuffer mappings. */
    uint64_t efer = rdmsr(0xc0000080);
    efer |= (1ULL << 11); /* EFER.NXE */
    wrmsr(0xc0000080, efer);

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    load_gdt();

    typedef void (*kernel_entry_t)(uint64_t) __attribute__((sysv_abi));
    kernel_entry_t fn = (kernel_entry_t)(uintptr_t)entry;
    fn(bootinfo_virt);

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
