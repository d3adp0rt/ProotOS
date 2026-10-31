/*
 * ProotOS — timer and RTC interfaces (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

void timer_init(void);
uint64_t timer_ticks(void);
void timer_sleep_ms(uint64_t ms);
void irq_dispatch(struct trap_frame *tf);
void rtc_print(void);
uint32_t rtc_hms(void);
