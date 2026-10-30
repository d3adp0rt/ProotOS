/*
 * ProotOS — minimal user-mode init program (M5b).
 * write=1, fork=57, execve=59, exit=60, wait4=61 via int 0x80.
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
    movq $59, %rax
    leaq path(%rip), %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    int $0x80

    /* execve failed */
    movq $60, %rax
    movq $1, %rdi
    int $0x80

1:
    jmp 1b
.size _start, . - _start

.section .rodata, "a", @progbits
hello:
    .ascii "Hello from ProotOS user mode!\n"
hello_end:
parent_msg:
    .ascii "parent: wait4 returned after execve\n"
parent_end:
path:
    .asciz "hello"
