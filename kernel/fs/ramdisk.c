/*
 * ProotOS — RAM disk block device (M6b).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "block.h"
#include "mm.h"

static struct block_device g_ramdisk;
static uint8_t *g_data;
static uint64_t g_size;

static int rd_read(struct block_device *dev, uint64_t offset, void *buf, size_t len) {
    (void)dev;
    if (offset + len > g_size)
        return -1;
    memcpy(buf, g_data + offset, len);
    return 0;
}

static int rd_write(struct block_device *dev, uint64_t offset, const void *buf, size_t len) {
    (void)dev;
    if (offset + len > g_size)
        return -1;
    memcpy(g_data + offset, buf, len);
    return 0;
}

int ramdisk_init(const void *data, size_t size) {
    g_data = kmalloc(size);
    if (!g_data)
        return -1;
    memcpy(g_data, data, size);
    g_size = size;
    g_ramdisk.name = "ramdisk0";
    g_ramdisk.size = size;
    g_ramdisk.read = rd_read;
    g_ramdisk.write = rd_write;
    g_ramdisk.priv = g_data;
    kprintf("[ramdisk] %llu KiB readonly source copied to RAM\n", (unsigned long long)(size >> 10));
    return 0;
}

struct block_device *ramdisk_device(void) {
    return g_ramdisk.name ? &g_ramdisk : 0;
}
