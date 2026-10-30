/*
 * ProotOS — ATA PIO primary-channel driver.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ata.h"
#include "mm.h"

#define ATA_DATA 0x1f0
#define ATA_ERROR 0x1f1
#define ATA_COUNT 0x1f2
#define ATA_LBA0 0x1f3
#define ATA_LBA1 0x1f4
#define ATA_LBA2 0x1f5
#define ATA_DRIVE 0x1f6
#define ATA_STATUS 0x1f7
#define ATA_CMD 0x1f7
#define ATA_ALT 0x3f6

static struct block_device g_dev;
static int g_ready;
static uint64_t g_sectors;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void delay400(void) {
    for (int i = 0; i < 4; i++)
        (void)inb(ATA_ALT);
}

static int wait_ready(int want_drq) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & 0x01)
            return -1; /* ERR */
        if (!(s & 0x80) && (!want_drq || (s & 0x08)))
            return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int ata_read(struct block_device *dev, uint64_t offset, void *buf, size_t len) {
    (void)dev;
    uint8_t *dst = buf;
    while (len) {
        uint64_t lba = offset / 512;
        if (lba >= g_sectors)
            return -1;
        size_t in_sector = (size_t)(offset % 512);
        size_t n = 512 - in_sector;
        if (n > len)
            n = len;
        if (wait_ready(0) != 0)
            return -1;
        outb(ATA_DRIVE, (uint8_t)(0xe0 | ((lba >> 24) & 0x0f)));
        outb(ATA_COUNT, 1);
        outb(ATA_LBA0, (uint8_t)lba);
        outb(ATA_LBA1, (uint8_t)(lba >> 8));
        outb(ATA_LBA2, (uint8_t)(lba >> 16));
        outb(ATA_CMD, 0x20);
        delay400();
        if (wait_ready(1) != 0)
            return -1;
        uint8_t sector[512];
        for (int i = 0; i < 256; i++)
            *(uint16_t *)(sector + i * 2) = inw(ATA_DATA);
        memcpy(dst, sector + in_sector, n);
        dst += n;
        offset += n;
        len -= n;
    }
    return 0;
}

static int ata_write(struct block_device *dev, uint64_t offset, const void *buf, size_t len) {
    (void)dev;
    const uint8_t *src = buf;
    while (len) {
        uint64_t lba = offset / 512;
        if (lba >= g_sectors)
            return -1;
        size_t in_sector = (size_t)(offset % 512);
        size_t n = 512 - in_sector;
        if (n > len)
            n = len;
        uint8_t sector[512];
        if (in_sector || n != 512) {
            if (ata_read(dev, lba * 512, sector, 512) != 0)
                return -1;
        } else {
            memset(sector, 0, 512);
        }
        memcpy(sector + in_sector, src, n);
        if (wait_ready(0) != 0)
            return -1;
        outb(ATA_DRIVE, (uint8_t)(0xe0 | ((lba >> 24) & 0x0f)));
        outb(ATA_COUNT, 1);
        outb(ATA_LBA0, (uint8_t)lba);
        outb(ATA_LBA1, (uint8_t)(lba >> 8));
        outb(ATA_LBA2, (uint8_t)(lba >> 16));
        outb(ATA_CMD, 0x30);
        delay400();
        if (wait_ready(1) != 0)
            return -1;
        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, *(uint16_t *)(sector + i * 2));
        outb(ATA_CMD, 0xe7); /* flush */
        src += n;
        offset += n;
        len -= n;
    }
    return 0;
}

struct block_device *ata_init(void) {
    g_ready = 0;
    outb(ATA_DRIVE, 0xa0);
    delay400();
    outb(ATA_COUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, 0xec);
    delay400();
    uint8_t status = inb(ATA_STATUS);
    if (status == 0 || status == 0xff || wait_ready(0) != 0) {
        kprintf("[ata] no primary master\n");
        return 0;
    }
    if (wait_ready(1) != 0) {
        kprintf("[ata] identify no DRQ, status=0x%x\n", inb(ATA_STATUS));
        return 0;
    }
    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);
    uint64_t sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) | ((uint64_t)id[102] << 32) |
                       ((uint64_t)id[103] << 48);
    if (!sectors)
        sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    if (!sectors) {
        kprintf("[ata] identify returned zero size\n");
        return 0;
    }
    g_sectors = sectors;
    g_dev.name = "sda";
    g_dev.size = sectors * 512;
    g_dev.read = ata_read;
    g_dev.write = ata_write;
    g_dev.priv = 0;
    g_ready = 1;
    kprintf("[ata] primary master: %llu sectors (%llu MiB)\n", (unsigned long long)sectors,
            (unsigned long long)(sectors / 2048));
    return &g_dev;
}

struct block_device *ata_device(void) {
    return g_ready ? &g_dev : 0;
}
