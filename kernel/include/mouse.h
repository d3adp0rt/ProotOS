/*
 * ProotOS — PS/2 mouse (M10).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

struct mouse_event {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
    int8_t wheel;
    uint16_t pad;
};

void mouse_init(void);
void mouse_irq(void);
int mouse_read(struct mouse_event *out);
