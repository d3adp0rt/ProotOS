/*
 * ProotOS — PS/2 keyboard scancode set 1 driver.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "keyboard.h"

#define KBD_BUF_SIZE 256

static const char map_lower[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7',  '8', '9', '0',  '-',  '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o',  'p', '[', ']',  '\n', 0,   'a',  's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',  'x', 'c',  'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,    ' ', 0,   0,    0,    0,   0,    0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8',  '9', '-', '4',  '5',  '6', '+',  '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
};

static const char map_upper[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A',  'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z',  'X', 'C',  'V',
    'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,    0,   0,    0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5',  '6', '+',  '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,    0,
};

static volatile uint8_t g_buf[KBD_BUF_SIZE];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static int g_shift;
static int g_caps;
static int g_extended;

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void push(char c) {
    uint32_t next = (g_head + 1) % KBD_BUF_SIZE;
    if (next == g_tail)
        return;
    g_buf[g_head] = (uint8_t)c;
    g_head = next;
}

void keyboard_feed(uint8_t scancode) {
    if (scancode == 0xe0) {
        g_extended = 1;
        return;
    }
    if (scancode == 0xe1)
        return;
    if (g_extended) {
        g_extended = 0;
        if (!(scancode & 0x80)) {
            uint8_t base = scancode & 0x7f;
            if (base == 0x48) push((char)0x80);      /* up */
            else if (base == 0x50) push((char)0x81); /* down */
            else if (base == 0x49) push((char)0x82); /* page up */
            else if (base == 0x51) push((char)0x83); /* page down */
        }
        return;
    }
    if (scancode == 0x2a || scancode == 0x36) {
        g_shift = 1;
        return;
    }
    if (scancode == 0xaa || scancode == 0xb6) {
        g_shift = 0;
        return;
    }
    if (scancode == 0x3a) {
        g_caps = !g_caps;
        return;
    }
    if (scancode & 0x80)
        return;
    char c = g_shift ? map_upper[scancode] : map_lower[scancode];
    if (g_caps && c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c)
        push(c);
}

void keyboard_irq(void) {
    while (inb(0x64) & 1) {
        uint8_t scancode = inb(0x60);
        keyboard_feed(scancode);
    }
}

int keyboard_getchar(void) {
    if (g_tail == g_head)
        return -1;
    char c = (char)g_buf[g_tail];
    g_tail = (g_tail + 1) % KBD_BUF_SIZE;
    return (int)(unsigned char)c;
}

void keyboard_init(void) {
    while (inb(0x64) & 1)
        (void)inb(0x60);
    g_head = g_tail = 0;
    g_shift = g_caps = g_extended = 0;
    keyboard_feed(0x1e); /* 'a' */
    keyboard_feed(0x2a); /* shift */
    keyboard_feed(0x1e); /* 'A' */
    keyboard_feed(0xaa);
    int a = keyboard_getchar();
    int b = keyboard_getchar();
    kprintf("[kbd] PS/2 scancode self-test: %s\n", (a == 'a' && b == 'A') ? "PASS" : "FAIL");
    g_head = g_tail = 0;
}
