/*
 * ProotOS — AHCI/SATA driver with IDENTIFY and READ/WRITE DMA EXT.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ahci.h"
#include "pci.h"
#include "mm.h"

#define AHCI_CLASS 0x01
#define AHCI_SUBCLASS 0x06
#define AHCI_PROGIF 0x01
#define HBA_PORT_SIG_SATA 0x00000101

struct ahci_port {
    volatile uint32_t *regs;
    uint64_t clb_phys;
    uint64_t fis_phys;
    uint64_t ct_phys;
    uint64_t bounce_phys;
    uint64_t sectors;
};

static struct ahci_port g_port;
static struct block_device g_dev;
static int g_ready;

static inline uint32_t r32(volatile uint32_t *p, uint32_t off) {
    return p[off / 4];
}
static inline void w32(volatile uint32_t *p, uint32_t off, uint32_t v) {
    p[off / 4] = v;
}

static int port_wait(volatile uint32_t *p, uint32_t off, uint32_t mask, uint32_t value,
                     int timeout_ms) {
    for (int i = 0; i < timeout_ms * 1000; i++) {
        if ((r32(p, off) & mask) == value)
            return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int ahci_command(struct ahci_port *port, uint64_t lba, uint16_t count, int write,
                        uint8_t command, uint64_t buffer_phys) {
    volatile uint32_t *p = port->regs;
    uint8_t *clb = (uint8_t *)PHYS_TO_VIRT(port->clb_phys);
    uint8_t *fis = (uint8_t *)PHYS_TO_VIRT(port->fis_phys);
    uint8_t *ct = (uint8_t *)PHYS_TO_VIRT(port->ct_phys);
    memset(clb, 0, 1024);
    memset(ct, 0, 1024);

    uint32_t *h = (uint32_t *)clb;
    h[0] = 5 | (write ? (1u << 6) : 0) | (1u << 16); /* CFL=5, W, PRDTL=1 */
    h[2] = (uint32_t)port->ct_phys;
    h[3] = (uint32_t)(port->ct_phys >> 32);

    memset(fis, 0, 64);
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = command;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);

    uint8_t *prdt = ct + 0x80;
    *(uint64_t *)(prdt + 0) = buffer_phys;
    *(uint32_t *)(prdt + 8) = 511; /* 512 bytes */
    *(uint32_t *)(prdt + 12) = 0;

    __asm__ volatile("wbinvd" : : : "memory");
    w32(p, 0x10, 0xffffffff); /* clear IS */
    w32(p, 0x38, 1);          /* command slot 0 */
    if (port_wait(p, 0x38, 1, 0, 2000) != 0) {
        kprintf("[ahci] command timeout, CI=0x%x IS=0x%x CMD=0x%x\n", r32(p, 0x38), r32(p, 0x10),
                r32(p, 0x18));
        return -1;
    }
    __asm__ volatile("wbinvd" : : : "memory");
    uint32_t is = r32(p, 0x10);
    if (is & (1u << 30)) {
        kprintf("[ahci] task file error, IS=0x%x\n", is);
        return -1;
    }
    (void)fis;
    return 0;
}

static int ahci_identify(struct ahci_port *port) {
    uint8_t *bounce = (uint8_t *)PHYS_TO_VIRT(port->bounce_phys);
    if (ahci_command(port, 0, 0, 0, 0xec, port->bounce_phys) != 0)
        return -1;
    uint16_t *id = (uint16_t *)bounce;
    uint64_t sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) | ((uint64_t)id[102] << 32) |
                       ((uint64_t)id[103] << 48);
    if (!sectors)
        sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    port->sectors = sectors;
    kprintf("[ahci] IDENTIFY: %llu sectors (%llu MiB)\n", (unsigned long long)sectors,
            (unsigned long long)(sectors / 2048));
    return 0;
}

static int ahci_read(struct block_device *dev, uint64_t offset, void *buf, size_t len) {
    struct ahci_port *port = dev->priv;
    uint8_t *bounce = (uint8_t *)PHYS_TO_VIRT(port->bounce_phys);
    uint8_t *dst = buf;
    while (len) {
        uint64_t sector = offset / 512;
        size_t in_sector = (size_t)(offset % 512);
        size_t n = 512 - in_sector;
        if (n > len)
            n = len;
        if (ahci_command(port, sector, 1, 0, 0x25, port->bounce_phys) != 0)
            return -1;
        memcpy(dst, bounce + in_sector, n);
        dst += n;
        offset += n;
        len -= n;
    }
    return 0;
}

static int ahci_write(struct block_device *dev, uint64_t offset, const void *buf, size_t len) {
    struct ahci_port *port = dev->priv;
    uint8_t *bounce = (uint8_t *)PHYS_TO_VIRT(port->bounce_phys);
    const uint8_t *src = buf;
    while (len) {
        uint64_t sector = offset / 512;
        size_t in_sector = (size_t)(offset % 512);
        size_t n = 512 - in_sector;
        if (n > len)
            n = len;
        if (ahci_read(dev, sector * 512, bounce, 512) != 0)
            return -1;
        memcpy(bounce + in_sector, src, n);
        if (ahci_command(port, sector, 1, 1, 0x35, port->bounce_phys) != 0)
            return -1;
        src += n;
        offset += n;
        len -= n;
    }
    return 0;
}

struct block_device *ahci_init(void) {
    g_ready = 0;
    struct pci_device *pci = pci_find_class(AHCI_CLASS, AHCI_SUBCLASS, AHCI_PROGIF);
    if (!pci) {
        kprintf("[ahci] no AHCI controller\n");
        return 0;
    }
    uint64_t abar_phys = pci->bar[5] & ~0xfULL;
    if (!abar_phys) {
        kprintf("[ahci] BAR5 is zero\n");
        return 0;
    }
    volatile uint32_t *abar = (volatile uint32_t *)PHYS_TO_VIRT(abar_phys);
    uint32_t pi = r32(abar, 0x0c);
    kprintf("[ahci] ABAR=%p ports=0x%x CAP=0x%x\n", (void *)abar_phys, pi, r32(abar, 0x00));

    /* ICH9 AHCI enable in PCI config space. */
    uint32_t ahci_cfg = pci_read32(pci->bus, pci->slot, pci->func, 0x90);
    pci_write32(pci->bus, pci->slot, pci->func, 0x90, ahci_cfg | (1u << 31));
    w32(abar, 0x04, r32(abar, 0x04) | (1u << 31)); /* AE */

    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i)))
            continue;
        volatile uint32_t *p = abar + (0x100 + i * 0x80) / 4;
        kprintf("[ahci] port %d SIG=0x%x SSTS=0x%x CMD=0x%x\n", i, r32(p, 0x24), r32(p, 0x28),
                r32(p, 0x18));
        if (r32(p, 0x24) != HBA_PORT_SIG_SATA)
            continue;
        if ((r32(p, 0x28) & 0x0f) != 3)
            continue;
        for (int spin = 0; spin < 100000; spin++) {
            if ((r32(p, 0x20) & 0x88) == 0)
                break;
            __asm__ volatile("pause");
        }
        kprintf("[ahci] port %d TFD=0x%x\n", i, r32(p, 0x20));

        uint32_t cmd = r32(p, 0x18);
        cmd &= ~(1u | (1u << 4));
        w32(p, 0x18, cmd);
        if (port_wait(p, 0x18, 0x8000, 0, 500) != 0) {
            kprintf("[ahci] port %d CR stuck: cmd=0x%x\n", i, r32(p, 0x18));
            continue;
        }
        if (port_wait(p, 0x18, 0x4000, 0, 500) != 0) {
            kprintf("[ahci] port %d FR stuck: cmd=0x%x\n", i, r32(p, 0x18));
            continue;
        }

        uint64_t clb = pmm_alloc_pages(1, PMM_ZONE_ANY);
        uint64_t fis = pmm_alloc_pages(1, PMM_ZONE_ANY);
        uint64_t ct = pmm_alloc_pages(1, PMM_ZONE_ANY);
        uint64_t bounce = pmm_alloc_pages(1, PMM_ZONE_ANY);
        if (!clb || !fis || !ct || !bounce)
            return 0;
        memset(PHYS_TO_VIRT(clb), 0, 4096);
        memset(PHYS_TO_VIRT(fis), 0, 4096);
        memset(PHYS_TO_VIRT(ct), 0, 4096);
        memset(PHYS_TO_VIRT(bounce), 0, 4096);

        g_port.regs = p;
        g_port.clb_phys = clb;
        g_port.fis_phys = fis;
        g_port.ct_phys = ct;
        g_port.bounce_phys = bounce;
        w32(p, 0x00, (uint32_t)clb);
        w32(p, 0x04, 0);
        w32(p, 0x08, (uint32_t)fis);
        w32(p, 0x0c, 0);
        w32(p, 0x14, 0);
        w32(p, 0x30, 0xffffffff);
        cmd = r32(p, 0x18);
        cmd &= ~(1u << 1) & ~(1u << 2);
        w32(p, 0x18, cmd | (1u << 4));
        w32(p, 0x18, (r32(p, 0x18) & ~((1u << 1) | (1u << 2))) | 1u);
        kprintf("[ahci] port %d CMD=0x%x CLB=0x%x FB=0x%x\n", i, r32(p, 0x18), r32(p, 0x00),
                r32(p, 0x08));

        if (ahci_identify(&g_port) != 0) {
            kprintf("[ahci] identify failed on port %d\n", i);
            continue;
        }
        g_dev.name = "sda";
        g_dev.size = g_port.sectors * 512;
        g_dev.read = ahci_read;
        g_dev.write = ahci_write;
        g_dev.priv = &g_port;
        g_ready = 1;
        kprintf("[ahci] port %d ready, /dev/sda %llu MiB\n", i,
                (unsigned long long)(g_dev.size >> 20));
        return &g_dev;
    }
    return 0;
}

struct block_device *ahci_device(void) {
    return g_ready ? &g_dev : 0;
}
