# ADR-0004: ext2 as the root filesystem

- Status: accepted
- Date: 2026-10-29

## Context

The root filesystem must support read/write, permissions, timestamps, symlinks
and hard links, and be implementable in a reasonable amount of time. A
journaling or CoW filesystem is significantly more complex.

## Decision

Use ext2 read/write as the root filesystem. Use ordered writes, `fsync`,
writeback and an in-tree `fsck`. initramfs is cpio `newc`. Additional
filesystems: ramfs, tmpfs, devtmpfs, procfs, sysfs, FAT32, ISO9660; later
exFAT/NTFS read-only and 9p/virtiofs.

## Consequences

- No journal: crash consistency depends on ordered writes and fsck.
- Simpler on-disk format and easier recovery tooling.
- ext2 is the native format; users can still mount removable FAT32.
