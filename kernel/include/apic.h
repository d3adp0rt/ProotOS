/*
 * ProotOS — Local APIC and IOAPIC (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"
#include "acpi.h"

void lapic_init(const struct acpi_info *acpi);
uint32_t lapic_id(void);
void lapic_eoi(void);
void lapic_send_ipi(uint32_t dest_apic_id, uint32_t icr_low);
void ioapic_init(const struct acpi_info *acpi);
void ioapic_mask_all(void);
void ioapic_unmask(uint32_t gsi, uint8_t vector, uint32_t dest_apic_id);
