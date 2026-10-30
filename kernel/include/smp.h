/*
 * ProotOS — SMP AP bring-up (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"
#include "acpi.h"

void smp_init(const struct acpi_info *acpi);
uint32_t smp_online_cpus(void);
