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

TLS 1.2 works with mbedTLS for one HTTPS request per boot. Known gaps:
- certificate verification is `MBEDTLS_SSL_VERIFY_NONE`;
- a second HTTPS connection in the same boot can fail during X.509 chain
  parsing (`x509_crt_parse_der`), likely load-balanced certificate/chain state;
- TLS 1.3 not enabled.
Fix multi-session/TLS1.3, add CA store, then enable remote ppkg update.
Remote ppkg code path exists (`ppkg update` -> registry bundle over HTTPS) but is
not enabled in the shell demo until multi-session is fixed. ECDSA-only cipher selection and IP caching make the first HTTPS request
reliable; a second request can still receive a different chain (Ed25519/RSA-PSS
or an extension mbedTLS 2.28 cannot parse) and fail X.509 parsing. Next step:
TLS 1.3/Ed25519-capable stack or a persistent TLS session.

## ВЕРНУТЬСЯ: remote ppkg

Repos created: `prootos-registry`, `prootos-packages`, `prootos-pkg-hello`.
`ppkg` still reads a local index (`/var/lib/ppkg/index`); remote fetch needs
TLS/HTTPS. Registry flow: registry -> packages index -> `.pk` URL.

## ВЕРНУТЬСЯ: остальное

- Remote ppkg update + recursive dependencies.
- TLS multi-session/certificate verification.
- NVMe, USB xHCI/HID/storage, AC'97 audio.
- COW fork, full signals/ptrace, `syscall/sysret`.
- musl port, more coreutils/TUI.
