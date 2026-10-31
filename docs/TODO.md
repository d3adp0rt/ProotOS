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

Still open:
- certificate verification is `MBEDTLS_SSL_VERIFY_NONE`; no CA store in the
  image yet;
- TLS 1.3 and Ed25519/RSA-PSS certificate chains are not supported by this
  mbedTLS version;
- no TLS session resumption.

Next: embed a CA bundle (`/etc/ssl/certs/ca.pem`), switch to `VERIFY_REQUIRED`,
or move to mbedTLS 3.x.

## ВЕРНУТЬСЯ: remote ppkg

Repos created: `prootos-registry`, `prootos-packages`, `prootos-pkg-hello`.
`ppkg update` now downloads `registry.tar` over HTTPS and extracts it into
`/var/lib/ppkg/`, so the index and package payload really come from the remote
registry. The shell demo runs `ppkg update`, `ppkg list`, `ppkg install hello`,
runs it, then `ppkg remove hello`.

Still open:
- recursive dependency resolution (dependency fields are parsed and displayed);
- source builds (stub: no compiler inside the OS);
- signed registry metadata (needs CA verification first).

## ВЕРНУТЬСЯ: остальное

- Remote ppkg recursive dependencies + source builds.
- TLS certificate verification / CA store / TLS 1.3.
- NVMe, USB xHCI/HID/storage, AC'97 audio.
- COW fork, full signals/ptrace, `syscall/sysret`.
- musl port, more coreutils/TUI.
