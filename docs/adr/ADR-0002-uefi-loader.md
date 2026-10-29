# ADR-0002: Own UEFI bootloader

- Status: accepted
- Date: 2026-10-29

## Context

The target is UEFI (OVMF and real hardware), with an ISO and an initramfs. GRUB
and Limine are available but add a dependency and hide the transition into long
mode.

## Decision

Write a minimal UEFI application in C compiled for `x86_64-unknown-windows` and
linked with `lld-link` as `BOOTX64.EFI`. It loads the kernel ELF and initramfs,
sets GOP, collects ACPI/memory map, builds page tables, exits boot services and
jumps to the higher-half kernel.

## Consequences

- Full control over the boot ABI and page tables.
- We own the UEFI bugs and must test on OVMF and real firmware.
- No BIOS/legacy support.
