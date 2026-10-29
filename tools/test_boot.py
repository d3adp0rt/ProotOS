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
        "M1 complete",
        "IDT installed",
    ]
    missing = [line for line in required if line not in out]
    if missing:
        print("\nBOOT TEST FAILED, missing:", missing, file=sys.stderr)
        return 1
    print("\nBOOT TEST PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
