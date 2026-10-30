/*
 * Blob wrapper: embeds ap_trampoline.bin into the kernel image.
 */
.section .rodata, "a", @progbits
.global ap_trampoline_start
.global ap_trampoline_end
ap_trampoline_start:
.incbin "ap_trampoline.bin"
ap_trampoline_end:
