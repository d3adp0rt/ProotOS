/*
 * ProotOS — ACPI power management (M7b).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

void acpi_poweroff(void) __attribute__((noreturn));
void acpi_reboot(void) __attribute__((noreturn));
