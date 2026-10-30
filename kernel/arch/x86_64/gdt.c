/*
 * ProotOS — GDT with 64-bit kernel/user segments and a 64-bit TSS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gdt.h"

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct tss g_tss;
static uint64_t g_gdt[7];
static struct gdt_ptr g_gdt_ptr;

static uint64_t make_desc(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    return ((uint64_t)(limit & 0xffff)) | ((uint64_t)(base & 0xffffff) << 16) |
           ((uint64_t)access << 40) | ((uint64_t)((limit >> 16) & 0x0f) << 48) |
           ((uint64_t)(flags & 0xf0) << 48) | ((uint64_t)((base >> 24) & 0xff) << 56);
}

static void load_gdt(void) {
    __asm__ volatile("lgdt %0" : : "m"(g_gdt_ptr) : "memory");
    __asm__ volatile("pushq $0x08\n"
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
                     :
                     : "rax", "memory");
}

void gdt_init(void) {
    memset(&g_tss, 0, sizeof(g_tss));
    g_tss.iomap_base = sizeof(g_tss);

    g_gdt[0] = 0;
    g_gdt[1] = make_desc(0, 0xfffff, 0x9a, 0xa0); /* kernel code */
    g_gdt[2] = make_desc(0, 0xfffff, 0x92, 0xc0); /* kernel data */
    g_gdt[3] = make_desc(0, 0xfffff, 0xfa, 0xa0); /* user code */
    g_gdt[4] = make_desc(0, 0xfffff, 0xf2, 0xc0); /* user data */

    uint64_t base = (uint64_t)(uintptr_t)&g_tss;
    uint32_t limit = sizeof(g_tss) - 1;
    g_gdt[5] = make_desc((uint32_t)base, limit, 0x89, 0x00);
    g_gdt[6] = base >> 32;

    g_gdt_ptr.limit = sizeof(g_gdt) - 1;
    g_gdt_ptr.base = (uint64_t)(uintptr_t)g_gdt;
    load_gdt();
    __asm__ volatile("ltr %w0" : : "r"((uint16_t)GDT_TSS));
    kprintf("[gdt] kernel/user segments + TSS installed\n");
}

void tss_set_rsp0(uint64_t rsp) {
    g_tss.rsp0 = rsp;
}

uint64_t tss_get_rsp0(void) {
    return g_tss.rsp0;
}
