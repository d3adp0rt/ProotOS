/*
 * ProotOS — PS/2 mouse on the 8042 auxiliary port (M10).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mouse.h"
#include "spinlock.h"

#define MOUSE_QUEUE 128

static struct mouse_event g_queue[MOUSE_QUEUE];
static volatile uint32_t g_head, g_tail;
static spinlock_t g_lock = SPINLOCK_INIT;
static uint8_t g_packet[3];
static int g_index;

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port));
}

static int wait_input_empty(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 0x02))
            return 0;
    return -1;
}

static int wait_output_full(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 0x01)
            return 0;
    return -1;
}

static int mouse_command(uint8_t cmd) {
    if (wait_input_empty() != 0)
        return -1;
    outb(0x64, 0xd4);
    if (wait_input_empty() != 0)
        return -1;
    outb(0x60, cmd);
    if (wait_output_full() != 0)
        return -1;
    uint8_t ack = inb(0x60);
    return ack == 0xfa ? 0 : -1;
}

static void push(const struct mouse_event *ev) {
    uint32_t next = (g_head + 1) % MOUSE_QUEUE;
    if (next == g_tail)
        return;
    g_queue[g_head] = *ev;
    g_head = next;
}

void mouse_irq(void) {
    while (inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        if (!(status & 0x20))
            break;
        uint8_t b = inb(0x60);
        if (g_index == 0 && !(b & 0x08))
            continue; /* resynchronise on the first packet byte */
        g_packet[g_index++] = b;
        if (g_index < 3)
            continue;
        g_index = 0;
        uint8_t flags = g_packet[0];
        if (flags & 0xc0)
            continue; /* overflow */
        struct mouse_event ev;
        int dx = (int)g_packet[1];
        int dy = (int)g_packet[2];
        if (flags & 0x10) dx -= 256;
        if (flags & 0x20) dy -= 256;
        ev.dx = (int16_t)dx;
        ev.dy = (int16_t)(-dy); /* PS/2 Y grows upwards */
        ev.buttons = (uint8_t)(flags & 0x07);
        ev.pad = 0;
        push(&ev);
    }
}

int mouse_read(struct mouse_event *out) {
    int got = 0;
    uint64_t flags = spin_lock_irqsave(&g_lock);
    if (g_tail != g_head && out) {
        *out = g_queue[g_tail];
        g_tail = (g_tail + 1) % MOUSE_QUEUE;
        got = 1;
    }
    spin_unlock_irqrestore(&g_lock, flags);
    return got;
}

void mouse_init(void) {
    g_head = g_tail = 0;
    g_index = 0;

    if (wait_input_empty() != 0)
        return;
    outb(0x64, 0xa8); /* enable auxiliary device */

    if (wait_input_empty() != 0)
        return;
    outb(0x64, 0x20); /* read controller configuration */
    if (wait_output_full() != 0)
        return;
    uint8_t cfg = inb(0x60);
    cfg |= 0x02;   /* aux interrupt */
    cfg &= (uint8_t)~0x20; /* aux clock enabled */
    if (wait_input_empty() != 0)
        return;
    outb(0x64, 0x60);
    if (wait_input_empty() != 0)
        return;
    outb(0x60, cfg);

    mouse_command(0xf6); /* defaults */
    mouse_command(0xf3); /* sample rate */
    mouse_command(0x64); /* 100 Hz */
    mouse_command(0xe8); /* resolution */
    mouse_command(0x03); /* 8 counts/mm */
    if (mouse_command(0xf4) != 0) {
        kprintf("[mouse] PS/2 aux init failed\n");
        return;
    }
    kprintf("[mouse] PS/2 aux enabled (IRQ12)\n");
}
