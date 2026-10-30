.section .text, "ax", @progbits
.global _start
.type _start, @function
_start:
    xorq %rbp, %rbp
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp), %rdx
    call main
    movl %eax, %edi
    movq $60, %rax
    int $0x80
1:
    jmp 1b
.size _start, . - _start
