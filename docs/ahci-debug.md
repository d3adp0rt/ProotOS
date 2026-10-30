# AHCI debug notes

Status: command issue still not accepted by the guest driver; virtio-blk remains
the working system disk.

## Observed

Q35 / ICH9 and a separate `-device ahci` both show:

- port 0: `SIG=0x101`, `SSTS=0x113`, `TFD=0x50`;
- after engine start: `PxCMD=0xc011` (ST|FRE|FR|CR);
- write `PxCI=1`; guest polls `PxCI` and reports timeout with `IS=0`.

QEMU trace (`-trace enable=ahci_*`) shows the command is in fact processed:

```
write PxCI 1
ahci_populate_sglist
ahci_dma_prepare_buf prepared=512
ahci_pio_transfer reading 512 bytes
read PxIS 0x3
read PxTFD 0x50
read PxCMD 0xc017
...
read PxCI 0
```

So the HBA executes the command and fills the bounce page, while the guest's
poll sees stale/early `PxCI=1`, `PxIS=0`.

## Hypotheses

1. Polling `PxCI` is not the right completion signal for this controller;
   `PxIS.DHRS`/`PSS` should be polled/cleared.
2. MMIO mapping of ABAR through the HHDM is cacheable; PxIS/PxCI updates may be
   stale until a cache flush.
3. The command completes after the current busy-wait window; QEMU PIO/BH
   scheduling may need a longer, timer-based delay.
4. PxCI may need to be cleared (write 0) after consuming PxIS.

## Next experiments

- Poll `PxIS & (DHRS|PSS)` and clear it; accept completion even if PxCI stays 1.
- Add `wbinvd`/`clflush` around PxIS/PxCI reads, or map ABAR uncached.
- Use a PIT-based 10 ms delay before polling.
- Compare the identify bounce buffer (word 0/1/60/61/100-103) after the command,
  even if PxCI still reads 1.
