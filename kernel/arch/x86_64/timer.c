/*
 * ProotOS — PIC remap, PIT timer and IRQ dispatch (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "timer.h"
#include "apic.h"
#include "sched.h"
#include "keyboard.h"
#include "mouse.h"

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xa0
#define PIC2_DATA 0xa1
#define PIT_CH0 0x40
#define PIT_CMD 0x43

static volatile uint64_t g_ticks;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static void pic_remap(void) {
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait(); /* master vectors 32..39 */
    outb(PIC2_DATA, 0x28);
    io_wait(); /* slave vectors 40..47 */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    outb(PIC1_DATA, 0xfc); /* unmask IRQ0 + IRQ1 */
    outb(PIC2_DATA, 0xff);
}

void timer_init(void) {
    pic_remap();

    uint32_t divisor = 1193182 / 1000; /* 1 kHz */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xff));
    outb(PIT_CH0, (uint8_t)(divisor >> 8));

    g_ticks = 0;
    kprintf("[pit] 1 kHz timer armed, IRQ0 -> vector 32\n");
}

void timer_irq(void) {
    g_ticks++;
}

uint64_t timer_ticks(void) {
    return g_ticks;
}

void timer_sleep_ms(uint64_t ms) {
    uint64_t target = g_ticks + ms;
    while (g_ticks < target) {
        __asm__ volatile("hlt");
    }
}

void irq_dispatch(struct trap_frame *tf) {
    int need_resched = 0;
    if (tf->vector == 32) {
        timer_irq();
        sched_tick();
        need_resched = sched_need_resched();
    } else if (tf->vector == 33) {
        keyboard_irq();
    } else if (tf->vector == 44) {
        mouse_irq();
    } else if (tf->vector != 39 && tf->vector != 47) {
        kprintf("[irq] unexpected vector %llu\n", (unsigned long long)tf->vector);
    }

    if (tf->vector >= 40)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
    lapic_eoi();
    if (need_resched) sched_maybe_resched(need_resched);
}
