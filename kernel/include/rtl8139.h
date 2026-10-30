/*
 * ProotOS — RTL8139 network driver (M7c).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

int rtl8139_init(void);
int rtl8139_send(const void *frame, size_t len);
const uint8_t *rtl8139_mac(void);
