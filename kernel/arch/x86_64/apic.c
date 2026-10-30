/*
 * ProotOS — Local APIC / IOAPIC driver.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "apic.h"
#include "mm.h"

#define LAPIC_ID 0x020
#define LAPIC_VERSION 0x030
#define LAPIC_TPR 0x080
#define LAPIC_EOI 0x0b0
#define LAPIC_SIVR 0x0f0
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370

static volatile uint32_t *g_lapic;
static volatile uint32_t *g_ioapic;
static uint32_t g_ioapic_gsi;
static uint32_t g_ioapic_max = 24;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static uint32_t lapic_read(uint32_t reg) {
    return g_lapic[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t value) {
    g_lapic[reg / 4] = value;
}

void lapic_init(const struct acpi_info *acpi) {
    uint64_t base_msr = rdmsr(0x1b); /* IA32_APIC_BASE */
    uint64_t base = base_msr & 0xfffff000ULL;
    if (!base)
        base = acpi->lapic_base;
    if (!base)
        panic("lapic: no local APIC base");

    base_msr |= (1ULL << 11); /* global enable */
    wrmsr(0x1b, base_msr);

    g_lapic = (volatile uint32_t *)PHYS_TO_VIRT(base);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SIVR, 0x1ff); /* spurious vector 0xff, APIC enabled */
    lapic_write(LAPIC_LVT_TIMER, 0x10000);
    lapic_write(LAPIC_LVT_LINT0, 0x10000);
    lapic_write(LAPIC_LVT_LINT1, 0x10000);
    lapic_write(LAPIC_LVT_ERROR, 0x10000);

    kprintf("[lapic] base=%p id=%u version=0x%x\n", (void *)base, lapic_read(LAPIC_ID) >> 24,
            lapic_read(LAPIC_VERSION) & 0xff);
}

uint32_t lapic_id(void) {
    return g_lapic ? (lapic_read(LAPIC_ID) >> 24) : 0;
}

void lapic_eoi(void) {
    if (g_lapic)
        lapic_write(LAPIC_EOI, 0);
}

void lapic_send_ipi(uint32_t dest_apic_id, uint32_t icr_low) {
    if (!g_lapic)
        return;
    lapic_write(LAPIC_ICR_HIGH, dest_apic_id << 24);
    lapic_write(LAPIC_ICR_LOW, icr_low);
    for (int i = 0; i < 100000; i++) {
        if (!(lapic_read(LAPIC_ICR_LOW) & (1 << 12)))
            break;
        __asm__ volatile("pause");
    }
}

static uint32_t ioapic_read(uint32_t reg) {
    g_ioapic[0] = reg;
    return g_ioapic[4];
}

static void ioapic_write(uint32_t reg, uint32_t value) {
    g_ioapic[0] = reg;
    g_ioapic[4] = value;
}

void ioapic_init(const struct acpi_info *acpi) {
    if (!acpi->ioapic_base) {
        kprintf("[ioapic] none\n");
        return;
    }
    g_ioapic = (volatile uint32_t *)PHYS_TO_VIRT(acpi->ioapic_base);
    g_ioapic_gsi = acpi->ioapic_gsi;
    uint32_t version = ioapic_read(1);
    g_ioapic_max = ((version >> 16) & 0xff) + 1;
    kprintf("[ioapic] base=%p version=0x%x entries=%u gsi_base=%u\n", (void *)acpi->ioapic_base,
            version & 0xff, g_ioapic_max, g_ioapic_gsi);
    ioapic_mask_all();
}

void ioapic_mask_all(void) {
    if (!g_ioapic)
        return;
    for (uint32_t i = 0; i < g_ioapic_max; i++) {
        ioapic_write(0x10 + i * 2, 0x10000); /* mask */
        ioapic_write(0x11 + i * 2, 0);
    }
}

void ioapic_unmask(uint32_t gsi, uint8_t vector, uint32_t dest_apic_id) {
    if (!g_ioapic || gsi < g_ioapic_gsi)
        return;
    uint32_t index = gsi - g_ioapic_gsi;
    if (index >= g_ioapic_max)
        return;
    uint32_t low = vector | (0 << 8) /* fixed */ | (0 << 11) /* physical */ | (0 << 13) /* edge */ |
                   (0 << 15) /* high */;
    ioapic_write(0x10 + index * 2, low | 0x10000);
    ioapic_write(0x11 + index * 2, dest_apic_id << 24);
    ioapic_write(0x10 + index * 2, low);
}
