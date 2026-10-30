/*
 * ProotOS — ACPI RSDP/XSDT/MADT/HPET parsing.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "acpi.h"
#include "mm.h"

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt hdr;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_hpet {
    struct acpi_sdt hdr;
    uint8_t hardware_revision;
    uint8_t comparator_count;
    uint8_t counter_size;
    uint8_t reserved;
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t base_address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

static int checksum_ok(const void *data, size_t length) {
    const uint8_t *p = data;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++)
        sum += p[i];
    return sum == 0;
}

static int sig4(const struct acpi_sdt *sdt, const char *sig) {
    return memcmp(sdt->signature, sig, 4) == 0;
}

int acpi_init(uint64_t rsdp_phys, struct acpi_info *out) {
    memset(out, 0, sizeof(*out));
    out->rsdp = rsdp_phys;
    for (int i = 0; i < 16; i++)
        out->irq_gsi[i] = (uint8_t)i;
    if (!rsdp_phys)
        return -1;

    const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)PHYS_TO_VIRT(rsdp_phys);
    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
        kprintf("[acpi] bad RSDP signature\n");
        return -1;
    }
    if (!checksum_ok(rsdp, 20)) {
        kprintf("[acpi] warning: RSDP checksum mismatch\n");
    }

    uint64_t root_phys = 0;
    int use_xsdt = 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        root_phys = rsdp->xsdt_address;
        use_xsdt = 1;
    } else {
        root_phys = rsdp->rsdt_address;
    }
    if (!root_phys) {
        kprintf("[acpi] no RSDT/XSDT\n");
        return -1;
    }

    const struct acpi_sdt *root = (const struct acpi_sdt *)PHYS_TO_VIRT(root_phys);
    if (!(sig4(root, "XSDT") || sig4(root, "RSDT"))) {
        kprintf("[acpi] bad root table signature\n");
        return -1;
    }
    if (!checksum_ok(root, root->length)) {
        kprintf("[acpi] warning: %c%c%c%c checksum mismatch\n", root->signature[0],
                root->signature[1], root->signature[2], root->signature[3]);
    }

    uint64_t entry_count = (root->length - sizeof(struct acpi_sdt)) / (use_xsdt ? 8 : 4);
    for (uint64_t i = 0; i < entry_count; i++) {
        uint64_t table_phys;
        if (use_xsdt) {
            table_phys = ((const uint64_t *)(root + 1))[i];
        } else {
            table_phys = ((const uint32_t *)(root + 1))[i];
        }
        if (!table_phys)
            continue;
        const struct acpi_sdt *table = (const struct acpi_sdt *)PHYS_TO_VIRT(table_phys);
        if (!checksum_ok(table, table->length))
            continue;

        if (sig4(table, "APIC")) {
            const struct acpi_madt *madt = (const struct acpi_madt *)table;
            out->lapic_base = madt->lapic_address;
            const uint8_t *p = (const uint8_t *)table + sizeof(struct acpi_madt);
            const uint8_t *end = (const uint8_t *)table + table->length;
            while (p + 2 <= end) {
                uint8_t type = p[0];
                uint8_t len = p[1];
                if (len < 2 || p + len > end)
                    break;
                if (type == 0 && len >= 8) {
                    uint8_t apic_id = p[3];
                    uint32_t flags = *(const uint32_t *)(p + 4);
                    if ((flags & 1) && out->cpu_count < ACPI_MAX_CPUS) {
                        out->apic_ids[out->cpu_count++] = apic_id;
                    }
                } else if (type == 1 && len >= 12) {
                    out->ioapic_base = *(const uint32_t *)(p + 4);
                    out->ioapic_gsi = *(const uint32_t *)(p + 8);
                } else if (type == 2 && len >= 10) {
                    uint8_t source = p[3];
                    uint32_t gsi = *(const uint32_t *)(p + 4);
                    if (source < 16)
                        out->irq_gsi[source] = (uint8_t)gsi;
                } else if (type == 5 && len >= 12) {
                    out->lapic_base = *(const uint64_t *)(p + 4);
                }
                p += len;
            }
        } else if (sig4(table, "HPET")) {
            const struct acpi_hpet *hpet = (const struct acpi_hpet *)table;
            out->hpet_base = hpet->base_address;
        }
    }

    if (!out->cpu_count || !out->lapic_base) {
        kprintf("[acpi] MADT incomplete: cpus=%u lapic=%p\n", out->cpu_count,
                (void *)out->lapic_base);
        return -1;
    }
    out->valid = 1;
    kprintf("[acpi] MADT: cpus=%u lapic=%p ioapic=%p gsi=%u\n", out->cpu_count,
            (void *)out->lapic_base, (void *)out->ioapic_base, out->ioapic_gsi);
    for (uint32_t i = 0; i < out->cpu_count; i++) {
        kprintf("[acpi] cpu%u apic-id=%u\n", i, out->apic_ids[i]);
    }
    kprintf("[acpi] IRQ0 -> GSI%u\n", out->irq_gsi[0]);
    if (out->hpet_base) {
        kprintf("[acpi] HPET at %p\n", (void *)out->hpet_base);
    }
    return 0;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

void acpi_poweroff(void) {
    kprintf("[acpi] S5 poweroff\n");
    outw(0x604, 0x2000);  /* QEMU q35 ACPI PM1a_CNT */
    outw(0xb004, 0x2000); /* older QEMU */
    for (;;) __asm__ volatile("cli; hlt");
}

void acpi_reboot(void) {
    kprintf("[acpi] reboot\n");
    outb(0x64, 0xfe);
    for (;;) __asm__ volatile("cli; hlt");
}
