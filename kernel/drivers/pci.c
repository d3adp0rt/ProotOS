/*
 * ProotOS — legacy PCI config-space enumeration.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "pci.h"

#define PCI_CONFIG_ADDR 0xcf8
#define PCI_CONFIG_DATA 0xcfc

static struct pci_device g_devices[64];
static int g_device_count;

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xfc);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xfc);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

static uint64_t pci_bar(uint8_t bus, uint8_t slot, uint8_t func, int index) {
    uint32_t bar = pci_read32(bus, slot, func, (uint8_t)(0x10 + index * 4));
    if (bar & 1)
        return bar; /* I/O BAR, keep the low flag bit */
    uint64_t addr = bar & ~0xfULL;
    if ((bar & 0x6) == 0x4 && index < 5) {
        uint32_t upper = pci_read32(bus, slot, func, (uint8_t)(0x10 + (index + 1) * 4));
        addr |= (uint64_t)upper << 32;
    }
    return addr;
}

void pci_init(void) {
    g_device_count = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0);
                uint16_t vendor = (uint16_t)(id & 0xffff);
                if (vendor == 0xffff)
                    continue;
                if (g_device_count >= (int)(sizeof(g_devices) / sizeof(g_devices[0])))
                    return;
                struct pci_device *d = &g_devices[g_device_count++];
                d->bus = (uint8_t)bus;
                d->slot = (uint8_t)slot;
                d->func = (uint8_t)func;
                d->vendor = vendor;
                d->device = (uint16_t)(id >> 16);
                uint32_t class_reg = pci_read32(d->bus, d->slot, d->func, 0x08);
                d->class_code = (uint8_t)(class_reg >> 24);
                d->subclass = (uint8_t)(class_reg >> 16);
                d->prog_if = (uint8_t)(class_reg >> 8);
                for (int i = 0; i < 6; i++)
                    d->bar[i] = pci_bar(d->bus, d->slot, d->func, i);

                uint32_t cmd = pci_read32(d->bus, d->slot, d->func, 0x04);
                cmd |= (1u << 1) | (1u << 2); /* memory space + bus master */
                pci_write32(d->bus, d->slot, d->func, 0x04, cmd);
            }
        }
    }
    kprintf("[pci] %d devices on legacy config space\n", g_device_count);
    for (int i = 0; i < g_device_count; i++) {
        struct pci_device *d = &g_devices[i];
        if (d->class_code == 0x01)
            kprintf("[pci] %02x:%02x.%u %04x:%04x class=%02x:%02x progif=%02x bar5=%p\n", d->bus,
                    d->slot, d->func, d->vendor, d->device, d->class_code, d->subclass, d->prog_if,
                    (void *)d->bar[5]);
    }
}

struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    for (int i = 0; i < g_device_count; i++) {
        struct pci_device *d = &g_devices[i];
        if (d->class_code == class_code && d->subclass == subclass &&
            (prog_if == 0xff || d->prog_if == prog_if))
            return d;
    }
    return 0;
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < g_device_count; i++) {
        struct pci_device *d = &g_devices[i];
        if (d->vendor == vendor && d->device == device)
            return d;
    }
    return 0;
}
