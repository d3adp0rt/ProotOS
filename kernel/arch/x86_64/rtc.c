/*
 * ProotOS — CMOS RTC reader (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timer.h"

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int update_in_progress(void) {
    return (cmos_read(0x0a) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t value) {
    return (uint8_t)((value & 0x0f) + ((value >> 4) * 10));
}

void rtc_print(void) {
    for (int spin = 0; spin < 1000000 && update_in_progress(); spin++) {
        __asm__ volatile("pause");
    }

    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t status_b = cmos_read(0x0b);

    if (!(status_b & 0x04)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = bcd_to_bin(hour & 0x7f) | (hour & 0x80);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }
    if (!(status_b & 0x02) && (hour & 0x80)) {
        hour = (uint8_t)(((hour & 0x7f) + 12) % 24);
    }

    kprintf("[rtc] 20%02u-%02u-%02u %02u:%02u:%02u UTC\n", year, month, day, hour, min, sec);
}
