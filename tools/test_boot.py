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
            "--timeout", "150",
            "--fresh-vars",
            "--expect", "M8b complete",
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
        "[ahci] IDENTIFY: 16384 sectors",
        "[net] DHCP offer",
        "[net] DHCP lease",
        "[net] ICMP echo reply from 10.0.2.2",
        "[net] DNS example.com ->",
        "ping 10.0.2.2: reply",
        "wget: 577 bytes (https)",
        "[https] CA store loaded",
        "ppkg: registry updated",
        "ppkg: installed greet",
        "ppkg: installed hello",
        "greet: hello from a dependency!",
        "ppkg: removed hello",
        "ppkg: removed greet",
        "[mouse] PS/2 aux enabled",
        "gui: framebuffer",
        "gui: framebuffer check PASS",
        "Lua 5.4 on ProotOS",
        "fib: 1 1 2 3 5 8 13 21 34 55 89 144",
        "sum 1..100 = 5050",
        "tcc: hello from compiled C",
        "tcc: fib(12) = 144",
        "tcc: sqrt(2) = 1.41421",
        "MicroPython on ProotOS",
        "import mathutil: fact(6) = 720",
        "fib: [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]",
        "sum 1..100 = 5050",
        "gui: terminal shell PASS",
        "gui: desktop drawn",
        "ProotOS shell (M8)",
        "M8 libc + shell + /bin/echo works",
        "Filesystem /:",
        "M8b complete: libc, shell, ls/rm/cp/mv/ps/df.",
    ]
    missing = [line for line in required if line not in out]
    if missing:
        print("\nBOOT TEST FAILED, missing:", missing, file=sys.stderr)
        return 1
    print("\nBOOT TEST PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
