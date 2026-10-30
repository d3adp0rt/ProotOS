/*
 * ProotOS — block device interface (M6b).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

struct block_device {
    const char *name;
    uint64_t size;
    int (*read)(struct block_device *dev, uint64_t offset, void *buf, size_t len);
    int (*write)(struct block_device *dev, uint64_t offset, const void *buf, size_t len);
    void *priv;
};

int ramdisk_init(const void *data, size_t size);
struct block_device *ramdisk_device(void);
