/*
 * ProotOS — AHCI SATA block driver (M7a).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "block.h"

struct block_device *ahci_init(void);
struct block_device *ahci_device(void);
