/*
 * ProotOS — legacy virtio-blk driver (M7a fallback).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "block.h"

struct block_device *virtio_blk_init(void);
struct block_device *virtio_blk_device(void);
