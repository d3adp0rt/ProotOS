/*
 * ProotOS — remaining PCI device discovery.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "misc.h"
#include "pci.h"

void misc_init(void) {
    struct pci_device *ac97 = pci_find_class(0x04, 0x01, 0xff);
    if (ac97)
        kprintf("[ac97] %04x:%04x bar0=%p\n", ac97->vendor, ac97->device, (void *)ac97->bar[0]);
    else
        kprintf("[ac97] not present\n");

    struct pci_device *xhci = pci_find_class(0x0c, 0x03, 0x30);
    if (xhci)
        kprintf("[xhci] %04x:%04x bar0=%p (driver deferred)\n", xhci->vendor, xhci->device,
                (void *)xhci->bar[0]);
    else
        kprintf("[xhci] not present\n");

    struct pci_device *nvme = pci_find_class(0x01, 0x08, 0xff);
    if (nvme)
        kprintf("[nvme] %04x:%04x bar0=%p (driver deferred)\n", nvme->vendor, nvme->device,
                (void *)nvme->bar[0]);
    else
        kprintf("[nvme] not present\n");
}
