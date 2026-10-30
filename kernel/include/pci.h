/*
 * ProotOS — PCI configuration space (M7).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint64_t bar[6];
};

void pci_init(void);
struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
struct pci_device *pci_find_device(uint16_t vendor, uint16_t device);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
