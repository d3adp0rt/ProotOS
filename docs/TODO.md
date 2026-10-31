# TODO / ВЕРНУТЬСЯ

## ВЕРНУТЬСЯ: DHCP

Статус: отложено. Сеть сейчас работает на статической конфигурации:

- IP: `10.0.2.15/24`
- gateway: `10.0.2.2`
- DNS: `10.0.2.3`
- SLIRP gateway MAC fallback: `52:55:0a:00:02:02`

Что уже проверено:

- `DHCPDISCOVER` уходит на `255.255.255.255:67`;
- pcap подтверждает отправку;
- `DHCPOFFER` от QEMU SLIRP не приходит;
- padding до 300 байт, UDP checksum, client-id, source `0.0.0.0`/`10.0.2.15`
  не помогли.

Когда возвращаться: перед M9c или при переходе на реальную сеть.

Что проверить при возврате:

1. QEMU `-netdev user,dhcpstart=...` и BOOTP-режим.
2. DHCP OFFER от SLIRP с эталонным клиентом (udhcpc/dhclient в Linux-госте).
3. `ciaddr`/`flags`/`option 50/54/61`, длина BOOTP >= 300.
4. После ACK — обновление `g_ip/g_gw/g_dns` и повторный ARP.

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

## M10: GUI

M10a done:

- bootinfo now carries the physical framebuffer base;
- syscalls `fb_info` (85) and `fb_map` (86) map the GOP framebuffer into the
  calling process at `0x30000000`;
- PS/2 aux mouse driver on IRQ12, queue and `/dev/mouse` events
  (`dx`, `dy`, buttons);
- `/bin/gui`: userspace compositor demo with a desktop, two windows, 8x8 text,
  shadow/border drawing, cursor save/restore (no ghosting) and ESC exit;
- verified with QEMU `mouse_move` and framebuffer screenshots.

M10b done:

- overlapping windows with explicit z-order and raise-on-click;
- title-bar dragging, close buttons and a taskbar that restores windows;
- active-window highlighting and focus hand-off after closing;
- click handling uses the mouse button bits from `/dev/mouse`;
- verified with QEMU `mouse_move`/`mouse_button` and framebuffer screenshots.

Fixed along the way: `wait4` had a lost-wakeup race (the child could exit
between the zombie scan and `wait_queue_wait`), which randomly froze the boot
demo at init; the scan and block now happen under the child wait-queue lock.

Next M10c:

- text console widget backed by a pipe, so the shell runs inside a window;
- keyboard focus for the active window and text input routing;
- launcher/taskbar actions to start apps;
- damage tracking instead of full-screen redraws;
- window resize and per-window backbuffers.

## ВЕРНУТЬСЯ: остальное

- Remote ppkg version constraints + source builds.
- TLS certificate verification / CA store / TLS 1.3.
- NVMe, USB xHCI/HID/storage, AC'97 audio.
- COW fork, full signals/ptrace, `syscall/sysret`.
- musl port, more coreutils/TUI.
