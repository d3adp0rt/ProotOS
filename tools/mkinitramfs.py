#!/usr/bin/env python3
"""Build a cpio newc initramfs from a directory."""

import os
import stat
import sys

ALIGN = 4


def pad4(buf: bytearray) -> None:
    while len(buf) % ALIGN:
        buf.append(0)


def write_entry(out: bytearray, name: str, mode: int, data: bytes) -> None:
    name_bytes = name.encode() + b"\0"
    header = (
        "070701"
        + f"{0:08x}"          # ino
        + f"{mode:08x}"       # mode
        + f"{0:08x}"          # uid
        + f"{0:08x}"          # gid
        + f"{1:08x}"          # nlink
        + f"{0:08x}"          # mtime
        + f"{len(data):08x}"  # filesize
        + f"{0:08x}"          # devmajor
        + f"{0:08x}"          # devminor
        + f"{0:08x}"          # rdevmajor
        + f"{0:08x}"          # rdevminor
        + f"{len(name_bytes):08x}"
        + f"{0:08x}"          # check
    ).encode()
    out.extend(header)
    out.extend(name_bytes)
    pad4(out)
    out.extend(data)
    pad4(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <root-dir> <output.cpio>", file=sys.stderr)
        return 2
    root, output = sys.argv[1], sys.argv[2]
    if not os.path.isdir(root):
        print(f"not a directory: {root}", file=sys.stderr)
        return 1

    out = bytearray()
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(dirnames + filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            entries.append(rel)
    entries.sort()

    for rel in entries:
        full = os.path.join(root, rel)
        st = os.lstat(full)
        if stat.S_ISDIR(st.st_mode):
            write_entry(out, rel, 0o040755, b"")
        elif stat.S_ISREG(st.st_mode):
            mode = 0o100755 if st.st_mode & 0o111 else 0o100644
            with open(full, "rb") as f:
                write_entry(out, rel, mode, f.read())
        else:
            continue

    write_entry(out, "TRAILER!!!", 0, b"")
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    with open(output, "wb") as f:
        f.write(out)
    print(f"initramfs: {output} ({len(out)} bytes, {len(entries)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
