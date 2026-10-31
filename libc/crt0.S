.section .text, "ax", @progbits
.global _start
.type _start, @function
_start:
    xorq %rbp, %rbp
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp), %rdx
    andq $-16, %rsp
    call main
    movl %eax, %edi
    call exit
1:
    jmp 1b
.size _start, . - _start
