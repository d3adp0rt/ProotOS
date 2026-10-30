/*
 * ProotOS — initramfs (cpio newc) reader (M5).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

void initramfs_init(const struct proot_bootinfo *bi);
int initramfs_find(const char *name, const void **data, size_t *size);
typedef int (*initramfs_callback)(const char *name, const void *data, size_t size, void *ctx);
void initramfs_iterate(initramfs_callback callback, void *ctx);
