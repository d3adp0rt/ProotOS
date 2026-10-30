/*
 * ProotOS — cpio newc initramfs lookup.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "initramfs.h"

static const uint8_t *g_base;
static size_t g_size;

static uint32_t hex8(const uint8_t *p) {
    uint32_t value = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t c = p[i];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= (uint32_t)(c - 'A' + 10);
    }
    return value;
}

void initramfs_init(const struct proot_bootinfo *bi) {
    g_base = 0;
    g_size = 0;
    if (!bi || !bi->initramfs_base || bi->initramfs_size < 6)
        return;
    g_base = (const uint8_t *)(uintptr_t)(PROOT_HHDM_BASE + bi->initramfs_base);
    g_size = bi->initramfs_size;
}

int initramfs_find(const char *name, const void **data, size_t *size) {
    if (!g_base)
        return -1;
    size_t offset = 0;
    while (offset + 110 <= g_size) {
        const uint8_t *hdr = g_base + offset;
        if (memcmp(hdr, "070701", 6) != 0)
            return -1;
        uint32_t filesize = hex8(hdr + 54);
        uint32_t namesize = hex8(hdr + 94);
        if (namesize == 0 || offset + 110 + namesize > g_size)
            return -1;
        const char *entry = (const char *)(hdr + 110);
        if (strcmp(entry, name) == 0) {
            size_t data_off = offset + 110 + namesize;
            data_off = (data_off + 3) & ~(size_t)3;
            if (data_off + filesize > g_size)
                return -1;
            *data = g_base + data_off;
            *size = filesize;
            return 0;
        }
        if (strcmp(entry, "TRAILER!!!") == 0)
            break;
        size_t next = offset + 110 + namesize;
        next = (next + 3) & ~(size_t)3;
        next += filesize;
        next = (next + 3) & ~(size_t)3;
        offset = next;
    }
    return -1;
}
