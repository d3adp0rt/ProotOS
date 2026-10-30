/*
 * ProotOS — early IDT setup (M1: exceptions only).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr_default(void);
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static void (*const stubs[32])(void) = {isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
                                        isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
                                        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
                                        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31};

static void (*const irq_stubs[16])(void) = {irq0, irq1, irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
                                            irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15};

static void set_gate(int vector, void (*handler)(void), uint8_t type_attr) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)(addr & 0xffff);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xffff);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xffffffff);
    idt[vector].zero = 0;
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    for (int i = 0; i < 256; i++)
        set_gate(i, isr_default, 0x8e);
    for (int i = 0; i < 32; i++)
        set_gate(i, stubs[i], 0x8e);
    for (int i = 0; i < 16; i++)
        set_gate(32 + i, irq_stubs[i], 0x8e);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtp) : "memory");
}
