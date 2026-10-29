# Testing

No CI. The local gate is `ninja -C build test-all`.

## Layers

| Layer | What | Command |
|---|---|---|
| Host unit tests | pure data structures, parsers, allocators | `ctest --test-dir build` |
| Kernel self-tests | allocator, paging, locks, VFS, syscalls (`test=1`) | `ninja test-boot` |
| Boot integration | QEMU + OVMF + serial assertions | `python3 tools/test_boot.py` |
| Fuzzing | ext2, cpio, ELF, INI, syscall arguments | later |
| Soak | SMP, allocation, I/O, network | later |

## Boot test

`tools/test_boot.py` builds nothing; it runs the existing ISO with a fresh OVMF
variable store, captures serial output for up to 45 seconds, and asserts:

- `ProotOS loader`
- `[prootos] kernel entered`
- `IDT installed`
- `M1 complete`

The test is expected to kill QEMU after the kernel halts; that is not a failure.

## Coverage policy

- Every exported kernel function is exercised by a ktest or an integration test.
- Core libraries: >= 80% line coverage in host tests.
- Parsers and filesystems: >= 90% line coverage.
- New code without tests fails review.

## Manual QEMU

```sh
python3 tools/run-qemu.py --gui
python3 tools/run-qemu.py --headless --timeout 20
python3 tools/run-qemu.py --headless --gdb
```

With `--gdb`, QEMU waits for a debugger on `localhost:1234`:

```sh
gdb build/images/kernel.elf -ex 'target remote :1234'
```
