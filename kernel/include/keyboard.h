/*
 * ProotOS — PS/2 keyboard (M7b).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

void keyboard_init(void);
void keyboard_irq(void);
int keyboard_getchar(void);
void keyboard_feed(uint8_t scancode);
