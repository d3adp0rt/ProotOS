/*
 * ProotOS — legacy ATA PIO block driver (M7a).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "block.h"

struct block_device *ata_init(void);
struct block_device *ata_device(void);
