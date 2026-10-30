/*
 * ProotOS — GDT/TSS for kernel and user mode (M5).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x1b
#define GDT_USER_DATA 0x23
#define GDT_TSS 0x28

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp);
uint64_t tss_get_rsp0(void);
