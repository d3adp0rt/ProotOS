# ADR-0006: CMake + Ninja build

- Status: accepted
- Date: 2026-10-29

## Context

The build must produce a PE/COFF UEFI loader, a freestanding ELF kernel, a cpio
archive, a FAT image and an ISO, and must expose `compile_commands.json` for
VS Code/clangd. Ninja is the requested execution engine.

## Decision

Use CMake as the generator and Ninja as the build tool. Per-target custom
commands invoke `clang` with explicit target triples (`x86_64-unknown-windows`
for the loader, `x86_64-unknown-none-elf` for the kernel), `lld-link` for the
EFI application and `ld.lld` for the kernel. `compile_commands.json` is enabled
for host targets; a `compile_flags.txt` covers freestanding sources.

## Consequences

- Cross-target flags are explicit and reproducible.
- Custom commands are less integrated with CMake's dependency scanner, so
  headers are listed as dependencies.
- Host unit tests use normal CMake targets and CTest.
