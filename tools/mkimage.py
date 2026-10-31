#!/usr/bin/env python3
"""Build a UEFI-bootable ISO: FAT ESP image + El Torito entry."""

import os
import shutil
import subprocess
import sys
import tempfile

FAT_SIZE_MB = 32


def run(cmd):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main() -> int:
    if len(sys.argv) != 7:
        print(
            f"usage: {sys.argv[0]} <bootx64.efi> <kernel.elf> <initramfs.cpio> "
            "<proot.cfg> <output.iso> <workdir>",
            file=sys.stderr,
        )
        return 2

    bootx64, kernel, initramfs, config, output_iso, workdir = sys.argv[1:]
    os.makedirs(workdir, exist_ok=True)
    efi_img = os.path.join(workdir, "efi.img")

    with open(efi_img, "wb") as f:
        f.truncate(FAT_SIZE_MB * 1024 * 1024)

    run(["mformat", "-i", efi_img, "-v", "PROOTOS", "::"])
    run(["mmd", "-i", efi_img, "::/EFI"])
    run(["mmd", "-i", efi_img, "::/EFI/BOOT"])
    run(["mcopy", "-i", efi_img, bootx64, "::/EFI/BOOT/BOOTX64.EFI"])
    run(["mcopy", "-i", efi_img, kernel, "::/EFI/BOOT/KERNEL.ELF"])
    run(["mcopy", "-i", efi_img, initramfs, "::/EFI/BOOT/INITRAMFS.CPIO"])
    run(["mcopy", "-i", efi_img, config, "::/EFI/BOOT/PROOT.CFG"])

    iso_root = os.path.join(workdir, "iso_root")
    if os.path.isdir(iso_root):
        shutil.rmtree(iso_root)
    os.makedirs(iso_root)
    shutil.copy2(efi_img, os.path.join(iso_root, "efi.img"))
    # Also expose the ESP tree in the ISO filesystem for tools that expect it.
    iso_boot = os.path.join(iso_root, "EFI", "BOOT")
    os.makedirs(iso_boot, exist_ok=True)
    shutil.copy2(bootx64, os.path.join(iso_boot, "BOOTX64.EFI"))
    shutil.copy2(kernel, os.path.join(iso_boot, "KERNEL.ELF"))
    shutil.copy2(initramfs, os.path.join(iso_boot, "INITRAMFS.CPIO"))
    shutil.copy2(config, os.path.join(iso_boot, "PROOT.CFG"))

    os.makedirs(os.path.dirname(os.path.abspath(output_iso)), exist_ok=True)
    run(
        [
            "xorriso",
            "-as",
            "mkisofs",
            "-R",
            "-J",
            "-V",
            "PROOTOS",
            "-o",
            output_iso,
            "-eltorito-alt-boot",
            "-e",
            "efi.img",
            "-no-emul-boot",
            iso_root,
        ]
    )
    print(f"ISO: {output_iso}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
