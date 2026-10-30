/*
 * ProotOS — ACPI table parsing (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

#define ACPI_MAX_CPUS 64

struct acpi_info {
    int valid;
    uint64_t rsdp;
    uint64_t lapic_base;
    uint64_t ioapic_base;
    uint32_t ioapic_gsi;
    uint32_t cpu_count;
    uint64_t hpet_base;
    uint32_t hpet_period_fs;
    uint8_t irq_gsi[16];
    uint8_t apic_ids[ACPI_MAX_CPUS];
};

int acpi_init(uint64_t rsdp_phys, struct acpi_info *out);
