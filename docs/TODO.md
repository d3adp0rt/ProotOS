# TODO / ВЕРНУТЬСЯ

## DHCP — done

The kernel runs a BOOTP/DHCP client during bring-up:

- broadcast DISCOVER with `flags=0x8000`, client-id and hostname options;
- parses OFFER (subnet, router, DNS options);
- sends REQUEST with option 50 (requested IP) and 54 (server id), waits ACK;
- applies the lease to `g_ip`, `g_gw`, `g_dns`; falls back to the static
  `10.0.2.15/24` config when no server answers.

QEMU SLIRP result:

```
[net] DHCP offer 10.0.2.15 from 10.0.2.2
[net] DHCP lease 10.0.2.15 gw 10.0.2.2 dns 10.0.2.3
```

Still open: lease renewal/T1/T2 timers, DHCPRELEASE on shutdown, and a
user-space `ifconfig`/`udhcpc` interface.

## ВЕРНУТЬСЯ: TLS / HTTPS

TLS 1.2 works with mbedTLS 2.28 and supports multiple HTTPS connections per
boot (demo does two `wget` requests plus a registry fetch). Multi-session was
fixed by:

- increasing the RTL8139 RX ring from 8K to 64K and handling wrap at the header;
- validating TCP sequence numbers (duplicates/out-of-order segments are now
  dropped instead of corrupting the TLS byte stream);
- honoring `Content-Length` so a short read is reported as an error.

Certificate verification is enabled:

- the image ships a Mozilla CA bundle at
  `/etc/ssl/certs/ca-certificates.crt` (unpacked from the initramfs);
- the kernel parses it once, uses `MBEDTLS_SSL_VERIFY_REQUIRED` and checks
  hostnames;
- `self-signed.badssl.com` is rejected with
  "certificate is not correctly signed by the trusted CA";
- if the bundle is missing the client logs a warning and falls back to
  `VERIFY_NONE` so the demo still boots.

Still open:
- TLS 1.3 and Ed25519/RSA-PSS certificate chains are not supported by this
  mbedTLS version;
- no OCSP/CRL checking and no automatic CA bundle refresh (the bundle is a
  build-time snapshot; refresh by replacing the file in `initramfs/`);
- no TLS session resumption.

## ВЕРНУТЬСЯ: remote ppkg

Repos created: `prootos-registry`, `prootos-packages`, `prootos-pkg-hello`.
`ppkg update` downloads `registry.tar` over HTTPS and extracts it into
`/var/lib/ppkg/`, so the index and package payload really come from the remote
registry. The shell demo runs `ppkg update`, `ppkg list`, `ppkg install hello`
(which pulls the `greet` dependency first), runs both programs, then removes
them.

`ppkg install` now resolves dependencies recursively from the index
(comma-separated `deps` field, depth and cycle limits). `ppkg remove` removes
only the named package; dependency-aware removal is still open.

Still open:
- dependency-aware removal and version constraints;
- source builds (stub: no compiler inside the OS);
- signed registry metadata (TLS verification is now in place; package
  signatures are still missing).

## M10: GUI — complete

Delivered:

- GOP framebuffer mapped into user space (`fb_info`, `fb_map`, `fb_unmap`);
- PS/2 IntelliMouse: buttons, motion, wheel; `/dev/mouse` events;
- window manager: overlapping windows, z-order, focus, drag, corner resize,
  close/taskbar restore, minimum sizes;
- clipping and a screen backbuffer; damage rectangles for terminal output,
  drag, resize, paging and typing; cursor save/restore without ghosts;
- taskbar launcher: window buttons, `+Terminal`, `+Editor`;
- terminals: multiple windows, per-window shell over pipes, scrollback,
  resize-aware wrapping, line selection, Ctrl+C/Ctrl+V, PageUp/PageDown,
  mouse wheel;
- file manager: readdir navigation, `..`, keyboard navigation (arrows, Enter,
  Backspace), text preview, Edit handoff;
- text editor: load/save (Ctrl+S), undo (Ctrl+Z), cursor movement, scrolling,
  paste, dirty marker;
- kernel console is muted on the framebuffer while the GUI owns the screen;
- extended keyboard handling: arrows, PgUp/PgDn, Ctrl combos.

Known post-M10 work:

- per-window window backbuffers (currently one screen backbuffer);
- terminal search and persistent clipboard across apps;
- C++ toolchain/runtime;
- MicroPython filesystem write APIs;
- signed package metadata, NVMe/USB/audio drivers;
- move GUI into `/init` as the default session instead of a demo command.


## M11: languages and self-hosting

Lua 5.4 done:

- vendored `third_party/lua`, core plus base/table/string/math/utf8/coroutine/
  debug libraries (no io/os/package);
- `/bin/lua script.lua` and a simple stdin REPL;
- sample script at `/usr/share/lua/hello.lua` (fib, tables, gmatch,
  string.format, pcall/error);
- supporting libc work: free-list allocator with realloc/calloc, FILE-style
  stdio, floating-point vsnprintf/strtod, setjmp/longjmp, ctype/locale,
  and a compact libm (sqrt/exp/log/sin/cos/pow/...);
- kernel FPU context switching via FXSAVE/FXRSTOR, and user/libc builds now
  use SSE; crt0 aligns the stack for the SysV ABI.

TinyCC done (JIT):

- vendored TCC 0.9.27, single-translation-unit build with `CONFIG_TCCBOOT`,
  `CONFIG_TCC_STATIC` and a PROOTOS JIT mmap path;
- `/bin/tcc -nostdlib -run file.c` compiles, relocates and runs C in-process;
- the JIT resolves libc symbols (printf/malloc/strings/math) through TCC's
  static symbol table;
- demo `/usr/share/tcc/hello.c` (recursion, loops, doubles/sqrt);
- kernel syscalls `mmap`/`munmap`/`mprotect` (90–92) for executable JIT pages;
- libc additions for compilers: variadic `open`, `sscanf`, `qsort`, `fdopen`,
  `fputc`, `strtof/strtold`, POSIX `struct stat`, `<sys/*>`, `<inttypes.h>`,
  `<dlfcn.h>`, `<signal.h>` shims, guarded heap free list.

The image now installs `/usr/include` (libc + TCC wrapper headers), libc
sources at `/usr/src/libc` and objects at `/usr/lib/prootos`.

Executable linking done:

- TCC's static EXE path now uses absolute `R_X86_64_64` GOT slots, applies
  `.got` relocations (`final_sections_reloc`) and calls `relocate_plt()` when
  there is no dynamic section; the patches are scoped to `TCC_OUTPUT_EXE` so
  `-run` keeps its JUMP_SLOT path;
- `tcc -static -nostdlib -o /exe.elf exe.c /usr/lib/prootos/*.o ...` links
  against the prebuilt libc objects (or `/usr/src/libc` sources) and the
  resulting static ELF runs via the normal loader;
- demo prints `tcc: standalone executable works` and squares 1..5.

Also done:

- terminal ANSI support in the framebuffer console (`CSI H/J/K`);
- `/bin/edit`: nano-like full-screen editor (arrows, PgUp/PgDn, Tab,
  Enter/Backspace, Ctrl+S save, Ctrl+Q quit, status line);
- TCC startup no longer hangs: help/version text is printed with `fputs`
  instead of `printf(help)` (long format strings exposed a formatter stall);
- MicroPython REPL input translates ProotOS key codes to VT100 sequences and
  Enter to `\r`, so line editing/history work.

Still open: C++ toolchain/runtime, MicroPython `open()`/write support.

MicroPython done:

- vendored MicroPython 1.24.1 `py/`, `shared/` and the minimal port;
- built by CMake through a small clang/ld wrapper; linked with the ProotOS
  libc, GC heap 256 KB, floats enabled, BASIC ROM level;
- `/bin/micropython script.py` executes a file; no-argument mode starts the
  friendly REPL (stdin, or `/dev/kbd` when launched from the console);
- external imports work: `mp_import_stat`/`mp_lexer_new_from_file` read
  `.py` files through the VFS; sys.path includes `/usr/share/python` and
  `/lib` (`import mathutil` demo);
- `open()` and write support are still stubbed;
- demo `/usr/share/python/hello.py`: recursion, comprehensions, dicts,
  `sorted`, `%` formatting;
- libc additions for Python: alloca, float classification (`isnan/isinf/
  isfinite/signbit/copysign/nearbyint/nan`), errno constants, fd flags.

Still open:

- TCC executable linking (PLT/GOT + libtcc1 helpers); JIT is the supported
  path;
- C++ compiler/runtime;
- MicroPython VFS/import support and a larger heap;
- `ppkg source` builds using the in-OS toolchain.

## ВЕРНУТЬСЯ: остальное

- Remote ppkg version constraints + source builds.
- TLS certificate verification / CA store / TLS 1.3.
- NVMe, USB xHCI/HID/storage, AC'97 audio.
- COW fork, full signals/ptrace, `syscall/sysret`.
- musl port, more coreutils/TUI.
