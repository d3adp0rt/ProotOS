# Changelog

All notable changes to ProotOS are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **M7c network/device discovery**: RTL8139 PCI NIC (MAC, reset, RX/TX rings,
  TX completion, boot-time TX test), AC'97 and xHCI discovery, NVMe probe.
  QEMU attaches rtl8139, AC'97 and qemu-xhci.
- **M7b PS/2 + power**: scancode set 1 keyboard driver, IRQ1, shift/caps,
  `/dev/kbd`, scancode self-test, ACPI S5 poweroff and reboot syscalls;
  user-mode `shutdown` at the end of the demo.
- **M7a PCI + virtio-blk**: legacy PCI enumeration, virtio-blk I/O BAR driver
  with a 256-entry split virtqueue, 8 MiB disk image, block-device plumbing and
  ext2 mounted from `/dev/vda` at `/mnt`; AHCI/ATA PIO drivers are present as
  fallbacks.
- **M6b ext2**: block-device layer, 8 MiB ramdisk, ext2 mount, block/inode
  bitmaps, direct and single-indirect block mapping, file read/write, directory
  lookup/create/unlink, mount/umount, `/mnt` rw demo.
- **M6a VFS/ramfs**: ramfs root unpacked from cpio initramfs, classic FHS
  directories, `/proc/meminfo`, `/proc/uptime`, `/dev/console`, `/dev/null`,
  path lookup, create/mkdir/unlink, read/write, fd-backed open/close/lseek,
  and a user-mode FS demo reading `/etc/motd` and creating/reading `/tmp/test`.
- **M5b process features**: `execve` from initramfs, pipes with blocking
  read/write and fd inheritance across fork, `close`, user-mode exception
  termination, `kill(SIGKILL)` self, and a `ptrace` skeleton
  (TRACEME/ATTACH/PEEKDATA/POKEDATA/CONT).
- **M5 user mode and processes**: GDT/TSS, ring-3 iretq entry, per-process
  PML4 with shared kernel mappings, ELF64 user loader, user stack, `int 0x80`
  syscall ABI (`write`, `getpid`, `yield`, `sleep_ms`, `fork`, `wait4`, `exit`),
  deep-copy fork, zombie/wait4, initramfs `/init` execution, and a user-mode
  hello/fork/wait demo.
- **M4 scheduler**: per-CPU EEVDF runqueues with nice weights, RT FIFO/RR and
  `SCHED_EXCLUSIVE`, kernel threads, 16 KiB stacks, context switch, idle thread,
  timer preemption, sleep/wakeup, wait queues, `kthread_join`, and M4 self-tests
  (sleep accuracy, lost-wakeup-free wait queue, nice ratios, exclusive
  starvation).
- **M3 interrupts, timers and SMP**: ACPI RSDP/XSDT/MADT/HPET parsing, Local
  APIC and IOAPIC setup, PIC remap, 1 kHz PIT timer with IRQ dispatch, CMOS RTC,
  spinlocks, INIT-SIPI-SIPI AP bring-up through a 16→32→64-bit trampoline at
  `0x8000`, 4/4 CPUs online in QEMU.
- **M2 memory management**: DMA32/Normal buddy allocator with page metadata,
  zone statistics, slab/kmalloc/kfree/krealloc/kzalloc, vmalloc/vfree, kernel
  page-table manager with 4K map/unmap/query, guard-page support and M2
  self-tests (`[ktest] M2 memory tests: PASS`).
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
