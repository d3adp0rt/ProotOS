/*
 * ProotOS — RTL8139 PCI NIC driver: MAC, init, TX, polled RX.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "rtl8139.h"
#include "pci.h"
#include "mm.h"

#define RTL_VENDOR 0x10ec
#define RTL_DEVICE 0x8139
#define RX_WRAP 65536u
#define RX_SIZE (RX_WRAP + 16 + 1536)
#define TX_SIZE 2048

static uint16_t g_io;
static uint8_t g_mac[6];
static uint8_t *g_rx;
static uint8_t *g_tx;
static uint64_t g_rx_phys;
static uint64_t g_tx_phys;
static int g_tx_index;
static int g_ready;
static uint16_t g_capr;
static uint8_t *g_rx_ring;

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outw(uint16_t p, uint16_t v) {
    __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(p));
}
static inline uint16_t inw(uint16_t p) {
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p));
}
static inline uint32_t inl(uint16_t p) {
    uint32_t v;
    __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}

int rtl8139_init(void) {
    g_ready = 0;
    struct pci_device *pci = pci_find_device(RTL_VENDOR, RTL_DEVICE);
    if (!pci) {
        kprintf("[rtl8139] not present\n");
        return -1;
    }
    if (!(pci->bar[0] & 1)) {
        kprintf("[rtl8139] BAR0 is not I/O\n");
        return -1;
    }
    g_io = (uint16_t)(pci->bar[0] & ~3ULL);

    for (int i = 0; i < 6; i++)
        g_mac[i] = inb(g_io + i);
    kprintf("[rtl8139] I/O=0x%x MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", g_io, g_mac[0], g_mac[1],
            g_mac[2], g_mac[3], g_mac[4], g_mac[5]);

    outb(g_io + 0x37, 0x10); /* software reset */
    for (int i = 0; i < 100000; i++)
        if (!(inb(g_io + 0x37) & 0x10))
            break;

    g_rx_phys = pmm_alloc_pages((RX_SIZE + 4095) / 4096, PMM_ZONE_ANY);
    g_tx_phys = pmm_alloc_pages((4 * TX_SIZE + 4095) / 4096, PMM_ZONE_ANY);
    if (!g_rx_phys || !g_tx_phys)
        return -1;
    g_rx = PHYS_TO_VIRT(g_rx_phys);
    g_rx_ring = g_rx;
    g_tx = PHYS_TO_VIRT(g_tx_phys);
    memset(g_rx, 0, RX_SIZE);
    memset(g_tx, 0, 4 * TX_SIZE);

    outl(g_io + 0x30, (uint32_t)g_rx_phys);   /* RBSTART */
    /* RCR: accept all, wrap, 64K ring, unlimited DMA burst. */
    outl(g_io + 0x44, 0x0000000f | (1 << 7) | (3 << 11) | (7 << 8));
    outl(g_io + 0x40, 0x03000700);            /* TCR */
    outl(g_io + 0x3c, 0);                     /* IMR */
    outl(g_io + 0x3e, 0xffff);                /* clear ISR */
    for (int i = 0; i < 4; i++)
        outl(g_io + 0x20 + i * 4, (uint32_t)(g_tx_phys + i * TX_SIZE));
    g_tx_index = 0;
    outb(g_io + 0x37, 0x0c); /* TE | RE */
    g_ready = 1;
    kprintf("[rtl8139] RX/TX enabled\n");
    return 0;
}

static uint8_t rx_byte(uint32_t off) {
    return g_rx_ring[off % RX_WRAP];
}

int rtl8139_poll(void *frame, size_t cap, size_t *out_len) {
    if (!g_ready || !frame) return 0;
    uint16_t cbr = inw(g_io + 0x3a);
    uint32_t capoff = g_capr;
    if (cbr == (uint16_t)capoff) return 0;
    uint8_t *hdr = g_rx_ring + (capoff % RX_WRAP);
    uint16_t status, len;
    /* Header can straddle the wrap point. */
    if ((capoff % RX_WRAP) <= RX_WRAP - 4) {
        status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        len = (uint16_t)(hdr[2] | (hdr[3] << 8));
    } else {
        status = (uint16_t)(rx_byte(capoff) | (rx_byte(capoff + 1) << 8));
        len = (uint16_t)(rx_byte(capoff + 2) | (rx_byte(capoff + 3) << 8));
    }
    if (len < 4 || len > 1600) {
        /* Desync: reset the ring and let the stack recover. */
        g_capr = 0;
        outw(g_io + 0x38, 0);
        outl(g_io + 0x3e, 0xffff);
        return -1;
    }
    uint16_t payload = (uint16_t)(len - 4);
    if (payload > cap) payload = (uint16_t)cap;
    uint32_t off = capoff + 4;
    for (uint16_t i = 0; i < payload; i++)
        ((uint8_t *)frame)[i] = rx_byte(off + i);
    if (out_len) *out_len = payload;
    g_capr = (uint16_t)((capoff + 4 + len + 3) & ~3u);
    g_capr = (uint16_t)(g_capr % RX_WRAP);
    outw(g_io + 0x38, (uint16_t)((g_capr - 16) & 0xffff));
    (void)status;
    return 1;
}

int rtl8139_send(const void *frame, size_t len) {
    if (!g_ready || len > TX_SIZE)
        return -1;
    int i = g_tx_index;
    memcpy(g_tx + i * TX_SIZE, frame, len);
    outl(g_io + 0x10 + i * 4, (uint32_t)len); /* TSD: start transmit */
    for (int spin = 0; spin < 500000; spin++) {
        uint32_t tsd = inl(g_io + 0x10 + i * 4);
        if (tsd & 0x8000) { /* TOK: transmit OK */
            g_tx_index = (i + 1) & 3;
            return 0;
        }
        if (!(tsd & 0x2000)) {
            g_tx_index = (i + 1) & 3;
            return -1;
        }
        __asm__ volatile("pause");
    }
    kprintf("[rtl8139] TX timeout TSD=0x%x\n", inl(g_io + 0x10 + i * 4));
    return -1;
}

const uint8_t *rtl8139_mac(void) {
    return g_mac;
}
