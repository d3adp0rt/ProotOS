# Changelog

All notable changes to ProotOS are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Package repositories**: public `prootos-registry`, `prootos-packages`
  and `prootos-pkg-hello` on GitHub; local `ppkg` index and sample package.
- **ppkg core**: `/bin/ppkg list|install`, tar-based `.pk` packages, local
  index at `/var/lib/ppkg/index`, VFS exec fallback so installed programs can
  run, and a `hello` sample package.
- **Shell improvements**: command history (8 entries) with Up/Down arrows,
  `history` and `clear` built-ins, extended PS/2 arrow scancodes, and a
  framebuffer cell erase fix.
- **Terminal erase fix**: framebuffer cells are cleared before glyph draw, so
  backspace/space really erase characters.
- **wget HTTPS diagnostics**: 3xx redirects to `https://` now report that TLS
  is required instead of a bare failure.
- **M9c ping/wget**: user `/bin/ping` (ICMP syscall) and `/bin/wget`
  (DNS + minimal TCP client + HTTP/1.0 GET). Fixed IP checksum double-write in
  TCP and enabled interrupts during blocking syscalls.
- **M9b DNS**: UDP DNS query to 10.0.2.3 with compressed-name answer parsing;
  `example.com` resolves at boot. Fixed RTL8139 CAPR writeback (CAPR-16) so RX
  continues after the first packet.
- **M7d AHCI**: fixed command FIS placement (command table, not PxFB) and
  PRDT layout (DBC at offset 12); IDENTIFY, READ/WRITE DMA EXT, ext2 on
  `/dev/sda`; QEMU runner uses AHCI instead of virtio-blk.
- **M9a network core**: RTL8139 RX ring (CBR/CAPR, 16-bit register I/O),
  ARP resolution with SLIRP gateway fallback, IPv4/ICMP echo request/reply,
  boot-time ping to 10.0.2.2.
- **M8b coreutils**: `ls` (readdir syscall), `rm`, `cp`, `mv`, `ps` (process
  list syscall), `df` (statfs), VFS readdir/statfs callbacks for ramfs and
  ext2, and mount/umount syscalls.
- **M8 libc and shell**: freestanding user libc (syscalls, string, printf,
  argv-aware execve), `/bin/sh` with line editor and built-ins, `/bin/echo`,
  `/bin/cat`, initramfs `/bin` staging, and a boot demo exercising fork,
  execve and argv.
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
