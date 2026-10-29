# Boot protocol

## Loader

`boot/loader/main.c` is a UEFI application built as PE/COFF:

```
clang -target x86_64-unknown-windows ...
lld-link /subsystem:efi_application /entry:efi_main
```

It uses only the UEFI services it needs: Simple Text Output, Boot Services
(allocation, memory map, ExitBootServices), Loaded Image, Simple File System,
Graphics Output and the configuration table.

## Config (`\EFI\BOOT\PROOT.CFG`)

INI-like, `key=value`, `#`/`;` comments:

```ini
kernel=\EFI\BOOT\KERNEL.ELF
initramfs=\EFI\BOOT\INITRAMFS.CPIO
width=1280
height=720
cmdline=console=serial,fb root=initramfs
```

Missing config falls back to defaults.

## ELF loading

The loader validates ELF64 little-endian `ET_EXEC` x86_64, computes the
`PT_LOAD` virtual range, allocates a contiguous physical block below 4 GiB,
copies segments (zeroing `p_memsz > p_filesz`), and maps the virtual range to
that block. This avoids depending on `p_paddr` and firmware placement.

## Page tables

- 4-level paging, 2 MiB pages for identity and HHDM, 4 KiB pages for kernel and
  framebuffer.
- Identity map covers all RAM (at least 4 GiB) for the loader until the final
  jump.
- HHDM is NX; framebuffer is NX.
- CR4.LA57 is cleared and EFER.NXE is set before loading CR3.

## Handoff

`jump_to_kernel(entry, bootinfo_virt, pml4_phys)`:

1. `cli`
2. clear CR4.LA57, set EFER.NXE
3. load CR3
4. load a flat 64-bit GDT
5. call the kernel entry with the SysV ABI, passing `bootinfo_virt`
   (`PROOT_HHDM_BASE + physical`).

The kernel entry sets its own 64 KiB stack and clears `.bss`.

## Initramfs

cpio `newc` (magic `070701`), built by `tools/mkinitramfs.py` from
`initramfs/`. The kernel receives its physical address and size and accesses it
through the HHDM.
