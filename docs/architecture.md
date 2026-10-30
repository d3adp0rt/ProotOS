# Architecture

ProotOS is a monolithic x86_64 kernel booted by its own UEFI loader.

## Boot chain

1. UEFI firmware loads `\EFI\BOOT\BOOTX64.EFI` from the ESP (inside the ISO).
2. The loader reads `\EFI\BOOT\PROOT.CFG`, opens the loaded image's simple file
   system, and reads the kernel ELF and initramfs.
3. GOP is configured (1280x720 requested, fallback to the current mode). If GOP
   is unavailable the kernel uses VGA text mode.
4. ACPI RSDP is taken from the UEFI configuration table.
5. The loader builds 4-level page tables:
   - identity map for the loader and low memory,
   - HHDM at `0xffff800000000000`,
   - kernel at `0xffffffff80000000`,
   - framebuffer at `0xffffc00000000000`.
6. `ExitBootServices`, then the loader reloads GDT/CR3 and jumps to `_start`.
7. The kernel sets up its stack, clears `.bss`, initializes serial, framebuffer
   console and IDT, prints the memory map, and halts (M1).

## Address space

| Region | Virtual range |
|---|---|
| User space | `0x0000000000400000` – `0x00007fffffffffff` (future) |
| HHDM | `0xffff800000000000` – |
| Framebuffer | `0xffffc00000000000` – |
| Kernel | `0xffffffff80000000` – |

The kernel is linked as a non-PIE ELF at `0xffffffff80000000`. The loader
allocates a contiguous physical block, copies PT_LOAD segments into it, and maps
the virtual image onto that block. Physical placement is therefore independent
of the link address.

## Bootinfo

`boot/bootinfo.h` defines the loader/kernel ABI: magic, version, UEFI memory
map, framebuffer geometry, RSDP, initramfs, command line and loader name. All
physical addresses are passed to the kernel; the kernel accesses them through
the HHDM.

## Current status

| Subsystem | Status |
|---|---|
| UEFI loader | M1 |
| Paging | M1 (identity/HHDM/kernel/fb, 4K + 2M) |
| Serial | M1 |
| Framebuffer console | M1 (8x8 font, VGA fallback) |
| IDT/exceptions | M1 (diagnostics, halt) |
| Physical allocator | M2 |
| Kernel heap / vmalloc | M2 |
| VMM (kernel mappings) | M2 |
| ACPI / LAPIC / IOAPIC | M3 |
| PIC/PIT timer, IRQ, RTC | M3 |
| SMP AP bring-up | M3 (4/4 in QEMU) |
| Scheduler / kernel threads | M4 (EEVDF, RT, exclusive) |
| User mode / processes | M5 (ring 3, fork/execve/wait, pipes) |
| VFS/ramfs/FHS | M6a |
| ext2 rw / mount | M6b |
| Drivers | M7 |
| Drivers | M7 |
| libc/userspace | M8 |
| Network/packages | M9 |
| GUI | M10 |
| Self-hosting/Doom | M11/M12 |
| IA32/ARM64 | M13 |
| Real hardware | M14 |

## Conventions

- C23, freestanding, `-Werror`.
- No libc `malloc` in the kernel; explicit allocators only.
- All kernel exported functions must have a ktest or integration test.
- Code and docs in English.
