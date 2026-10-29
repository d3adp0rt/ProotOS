/*
 * ProotOS — kernel entry stub.
 * The UEFI loader jumps here in long mode with the bootinfo pointer in RDI.
 */
.section .text.entry, "ax", @progbits
.global _start
.type _start, @function
_start:
    cli
    cld

    /* Save bootinfo across BSS clearing. */
    movq %rdi, %r15

    /* Clear .bss.  The loader zeroes p_memsz too, but this is cheap. */
    leaq __bss_start(%rip), %rdi
    leaq __bss_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    movq %r15, %rdi
    leaq stack_top(%rip), %rsp
    xorq %rbp, %rbp
    call kmain

1:
    cli
    hlt
    jmp 1b
.size _start, . - _start

.section .bss, "aw", @nobits
.align 16
stack_bottom:
    .skip 65536
stack_top:
