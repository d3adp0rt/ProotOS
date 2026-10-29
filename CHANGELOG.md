# Changelog

All notable changes to ProotOS are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Repository bootstrap: GPL-3.0 license, CMake + Ninja build, docs and ADRs.
- UEFI bootloader (`BOOTX64.EFI`): config parsing, GOP setup, ELF64 loading,
  cpio initramfs loading, ACPI RSDP discovery, memory map, 4-level page tables
  with identity map, HHDM, higher-half kernel mapping and framebuffer mapping.
- Kernel entry (`_start`), freestanding string/printf, serial console, GOP
  framebuffer console with VGA text fallback, IDT and exception diagnostics.
- Boot smoke test driven over QEMU serial.
- ISO image builder (FAT ESP + El Torito) and QEMU runner.

### Changed
- Nothing yet.

### Fixed
- Nothing yet.

## [0.0.1] - 2026-10-29

Initial bootstrap.
