#!/usr/bin/env python3
"""Smoke-test the boot chain in QEMU and assert expected serial output."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / "build/images/prootos.iso"


def main() -> int:
    if not ISO.exists():
        print(f"missing ISO: {ISO}", file=sys.stderr)
        return 1
    proc = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/run-qemu.py"),
            "--iso", str(ISO),
            "--headless",
            "--timeout", "45",
            "--fresh-vars",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    out = proc.stdout
    print(out)
    required = [
        "ProotOS loader",
        "[prootos] kernel entered",
        "[ktest] M2 memory tests: PASS",
        "[smp] online CPUs: 4/4",
        "[ktest] M4 scheduler tests: PASS",
        "Hello from ProotOS user mode!",
        "hello from execve!",
        "parent: wait4 returned after execve",
        "ProotOS initramfs",
        "vfs write/read works",
        "fs test done",
        "ext2 write/read works",
        "ext2 test done",
        "[kbd] PS/2 scancode self-test: PASS",
        "[rtl8139] test TX: PASS",
        "M7c complete",
    ]
    missing = [line for line in required if line not in out]
    if missing:
        print("\nBOOT TEST FAILED, missing:", missing, file=sys.stderr)
        return 1
    print("\nBOOT TEST PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
