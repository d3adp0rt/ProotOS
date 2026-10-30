/*
 * ProotOS — user hello program loaded via execve (M5b).
 */
.section .text, "ax", @progbits
.global _start
.type _start, @function
_start:
    movq $1, %rax
    movq $1, %rdi
    leaq msg(%rip), %rsi
    movq $msg_end - msg, %rdx
    int $0x80

    movq $60, %rax
    xorq %rdi, %rdi
    int $0x80

1:
    jmp 1b
.size _start, . - _start

.section .rodata, "a", @progbits
msg:
    .ascii "hello from execve!\n"
msg_end:
