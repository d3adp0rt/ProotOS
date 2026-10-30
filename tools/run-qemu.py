#!/usr/bin/env python3
"""Run ProotOS in QEMU with OVMF (UEFI)."""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_ovmf():
    candidates = [
        ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"),
        ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
        ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ]
    for code, var in candidates:
        if os.path.exists(code) and os.path.exists(var):
            return code, var
    raise SystemExit("OVMF firmware not found (install edk2-ovmf)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=str(ROOT / "build/images/prootos.iso"))
    ap.add_argument("--disk", default=str(ROOT / "build/images/data.img"))
    ap.add_argument("--memory", default="1G")
    ap.add_argument("--smp", default="4")
    ap.add_argument("--headless", action="store_true")
    ap.add_argument("--gui", action="store_true")
    ap.add_argument("--gdb", action="store_true")
    ap.add_argument("--timeout", type=float, default=0)
    ap.add_argument("--fresh-vars", action="store_true")
    ap.add_argument("--keep-disk", action="store_true")
    ap.add_argument("--pcap", default="")
    args = ap.parse_args()

    iso = Path(args.iso)
    if not iso.exists():
        raise SystemExit(f"ISO not found: {iso}")

    code, var_src = find_ovmf()
    work = ROOT / "build/images"
    work.mkdir(parents=True, exist_ok=True)
    vars_file = work / "OVMF_VARS.fd"
    if args.fresh_vars or not vars_file.exists():
        shutil.copy2(var_src, vars_file)

    cmd = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-cpu", "max",
        "-m", args.memory,
        "-smp", args.smp,
        "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,file={vars_file}",
        "-cdrom", str(iso),
        "-boot", "order=d",
        "-vga", "std",
        "-netdev", "user,id=net0",
        "-device", "rtl8139,netdev=net0",
        "-audiodev", "none,id=snd0",
        "-device", "AC97,audiodev=snd0",
        "-device", "qemu-xhci",
        "-monitor", "none",
        "-no-reboot",
        "-serial", "stdio",
    ]
    if args.pcap:
        cmd += ["-object", f"filter-dump,id=f1,netdev=net0,file={args.pcap}"]
    disk = Path(args.disk)
    if disk.exists():
        run_disk = work / "data-run.img"
        if not args.keep_disk or not run_disk.exists():
            shutil.copy2(disk, run_disk)
        cmd += ["-drive", f"file={run_disk},format=raw,if=ide,index=0"]
    if args.gdb:
        cmd += ["-s", "-S"]
    if args.headless or not args.gui:
        cmd += ["-display", "none"]

    print("+", " ".join(cmd), file=sys.stderr)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        out, _ = proc.communicate(timeout=args.timeout if args.timeout > 0 else None)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
    sys.stdout.write(out)
    return proc.returncode or 0


if __name__ == "__main__":
    raise SystemExit(main())
