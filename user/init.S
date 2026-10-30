/*
 * ProotOS — minimal user-mode init program (M5).
 * Syscalls: write=1, getpid=2, fork=57, exit=60, wait4=61 via int 0x80.
 */
.section .text, "ax", @progbits
.global _start
.type _start, @function
_start:
    movq $1, %rax
    movq $1, %rdi
    leaq hello(%rip), %rsi
    movq $hello_end - hello, %rdx
    int $0x80

    movq $57, %rax
    int $0x80
    testq %rax, %rax
    jz child

    /* parent: wait for the child, then print and exit */
    movq $61, %rax
    movq $-1, %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    xorq %r10, %r10
    int $0x80

    movq $1, %rax
    movq $1, %rdi
    leaq parent_msg(%rip), %rsi
    movq $parent_end - parent_msg, %rdx
    int $0x80

    movq $60, %rax
    xorq %rdi, %rdi
    int $0x80

child:
    movq $2, %rax
    int $0x80
    movq $1, %rax
    movq $1, %rdi
    leaq child_msg(%rip), %rsi
    movq $child_end - child_msg, %rdx
    int $0x80

    movq $60, %rax
    xorq %rdi, %rdi
    int $0x80

1:
    jmp 1b
.size _start, . - _start

.section .rodata, "a", @progbits
hello:
    .ascii "Hello from ProotOS user mode!\n"
hello_end:
child_msg:
    .ascii "child: fork returned 0\n"
child_end:
parent_msg:
    .ascii "parent: wait4 returned\n"
parent_end:
