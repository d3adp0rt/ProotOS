/*
 * ProotOS — legacy virtio-blk over PCI I/O BAR.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "virtio_blk.h"
#include "pci.h"
#include "mm.h"

#define VIRTIO_VENDOR 0x1af4
#define VIRTIO_BLK_LEGACY 0x1001
#define VIRTIO_BLK_MODERN 0x1042

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
    uint16_t used_event;
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[256];
    uint16_t avail_event;
} __attribute__((packed));

struct virtio_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

static struct block_device g_dev;
static uint16_t g_io;
static int g_ready;
static uint64_t g_capacity;
static uint64_t g_queue_phys;
static uint64_t g_data_phys;
static uint16_t g_qsize;
static uint16_t g_avail_idx;
static uint16_t g_used_idx;

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static int virtio_request(uint64_t sector, int write, void *data) {
    uint8_t *q = (uint8_t *)PHYS_TO_VIRT(g_queue_phys);
    struct vring_desc *desc = (struct vring_desc *)q;
    struct vring_avail *avail = (struct vring_avail *)(q + 0x1000);
    struct vring_used *used = (struct vring_used *)(q + 0x2000);
    struct virtio_hdr *hdr = (struct virtio_hdr *)(q + 0x3000);
    uint8_t *status = (uint8_t *)(q + 0x3010);

    hdr->type = write ? 1 : 0;
    hdr->reserved = 0;
    hdr->sector = sector;
    *status = 0xff;

    desc[0].addr = g_queue_phys + 0x3000;
    desc[0].len = sizeof(*hdr);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;
    desc[1].addr = g_data_phys;
    desc[1].len = 512;
    desc[1].flags = VRING_DESC_F_NEXT | (write ? 0 : VRING_DESC_F_WRITE);
    desc[1].next = 2;
    desc[2].addr = g_queue_phys + 0x3010;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next = 0;

    memcpy(PHYS_TO_VIRT(g_data_phys), data, 512);
    avail->ring[g_avail_idx % g_qsize] = 0;
    g_avail_idx++;
    avail->idx = g_avail_idx;
    __asm__ volatile("mfence" : : : "memory");
    outw(g_io + 0x10, 0);

    for (int spin = 0; spin < 10000000; spin++) {
        __asm__ volatile("mfence" : : : "memory");
        if (used->idx != g_used_idx) {
            g_used_idx = used->idx;
            __asm__ volatile("mfence" : : : "memory");
            if (*status != 0) {
                kprintf("[virtio] request status=%u\n", *status);
                return -1;
            }
            memcpy(data, PHYS_TO_VIRT(g_data_phys), 512);
            return 0;
        }
        __asm__ volatile("pause");
    }
    kprintf("[virtio] request timeout\n");
    return -1;
}

static int v_read(struct block_device *dev, uint64_t offset, void *buf, size_t len) {
    (void)dev;
    uint8_t *dst = buf;
    while (len) {
        uint64_t sector = offset / 512;
        size_t in_sector = (size_t)(offset % 512);
        size_t n = 512 - in_sector;
        if (n > len)
            n = len;
        uint8_t tmp[512];
        if (virtio_request(sector, 0, tmp) != 0)
            return -1;
        memcpy(dst, tmp + in_sector, n);
        dst += n;
        offset += n;
        len -= n;
    }
    return 0;
}

static int v_write(struct block_device *dev, uint64_t offset, const void *buf, size_t len) {
    (void)dev;
    const uint8_t *src = buf;
    while (len) {
        uint64_t sector = offset / 512;
        size_t in_sector = (size_t)(offset % 512);
        size_t n = 512 - in_sector;
        if (n > len)
            n = len;
        uint8_t tmp[512];
        if (in_sector || n != 512) {
            if (v_read(dev, sector * 512, tmp, 512) != 0)
                return -1;
        } else {
            memset(tmp, 0, 512);
        }
        memcpy(tmp + in_sector, src, n);
        if (virtio_request(sector, 1, tmp) != 0)
            return -1;
        src += n;
        offset += n;
        len -= n;
    }
    return 0;
}

struct block_device *virtio_blk_init(void) {
    g_ready = 0;
    struct pci_device *pci = pci_find_device(VIRTIO_VENDOR, VIRTIO_BLK_LEGACY);
    if (!pci)
        pci = pci_find_device(VIRTIO_VENDOR, VIRTIO_BLK_MODERN);
    if (!pci) {
        kprintf("[virtio] no virtio-blk device\n");
        return 0;
    }
    if (!(pci->bar[0] & 1)) {
        kprintf("[virtio] BAR0 is not an I/O BAR (modern-only device?)\n");
        return 0;
    }
    g_io = (uint16_t)(pci->bar[0] & ~3ULL);
    kprintf("[virtio] blk at I/O 0x%x device=%04x\n", g_io, pci->device);

    outb(g_io + 0x12, 0);
    outb(g_io + 0x12, 1);
    outb(g_io + 0x12, 3);
    (void)inl(g_io + 0x00);
    outl(g_io + 0x04, 0);
    outb(g_io + 0x12, 11);
    if (inb(g_io + 0x12) != 11) {
        kprintf("[virtio] features negotiation failed\n");
        return 0;
    }

    g_queue_phys = pmm_alloc_pages(4, PMM_ZONE_ANY);
    g_data_phys = pmm_alloc_pages(1, PMM_ZONE_ANY);
    if (!g_queue_phys || !g_data_phys)
        return 0;
    memset(PHYS_TO_VIRT(g_queue_phys), 0, 4 * PAGE_SIZE);
    memset(PHYS_TO_VIRT(g_data_phys), 0, PAGE_SIZE);

    outw(g_io + 0x0e, 0);
    g_qsize = inw(g_io + 0x0c);
    if (g_qsize > 256)
        g_qsize = 256;
    kprintf("[virtio] queue size=%u\n", g_qsize);
    outl(g_io + 0x08, (uint32_t)(g_queue_phys >> 12));
    g_avail_idx = 0;
    g_used_idx = 0;

    outb(g_io + 0x12, 15);
    uint32_t lo = inl(g_io + 0x14);
    uint32_t hi = inl(g_io + 0x18);
    g_capacity = ((uint64_t)hi << 32) | lo;
    if (!g_capacity) {
        kprintf("[virtio] zero capacity\n");
        return 0;
    }
    g_dev.name = "vda";
    g_dev.size = g_capacity * 512;
    g_dev.read = v_read;
    g_dev.write = v_write;
    g_dev.priv = 0;
    g_ready = 1;
    kprintf("[virtio] capacity %llu sectors (%llu MiB)\n", (unsigned long long)g_capacity,
            (unsigned long long)(g_dev.size >> 20));
    return &g_dev;
}

struct block_device *virtio_blk_device(void) {
    return g_ready ? &g_dev : 0;
}
