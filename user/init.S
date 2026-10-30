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

    /* VFS: read /etc/motd */
    movq $6, %rax
    leaq motd_path(%rip), %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    int $0x80
    movq %rax, %r12
    movq $0, %rax
    movq %r12, %rdi
    leaq buf(%rip), %rsi
    movq $255, %rdx
    int $0x80
    movq %rax, %r13
    movq $1, %rax
    movq $1, %rdi
    leaq buf(%rip), %rsi
    movq %r13, %rdx
    int $0x80
    movq $3, %rax
    movq %r12, %rdi
    int $0x80

    /* VFS: create/write/read /tmp/test */
    movq $6, %rax
    leaq test_path(%rip), %rdi
    movq $0x42, %rsi
    movq $0644, %rdx
    int $0x80
    movq %rax, %r12
    movq $1, %rax
    movq %r12, %rdi
    leaq vfs_msg(%rip), %rsi
    movq $vfs_end - vfs_msg, %rdx
    int $0x80
    movq $10, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    int $0x80
    movq $0, %rax
    movq %r12, %rdi
    leaq buf(%rip), %rsi
    movq $255, %rdx
    int $0x80
    movq %rax, %r13
    movq $1, %rax
    movq $1, %rdi
    leaq buf(%rip), %rsi
    movq %r13, %rdx
    int $0x80
    movq $3, %rax
    movq %r12, %rdi
    int $0x80
    movq $9, %rax
    leaq test_path(%rip), %rdi
    int $0x80

    movq $1, %rax
    movq $1, %rdi
    leaq fs_done(%rip), %rsi
    movq $fs_done_end - fs_done, %rdx
    int $0x80

    /* ext2 on /mnt: create/write/read */
    movq $6, %rax
    leaq ext2_path(%rip), %rdi
    movq $0x42, %rsi
    movq $0644, %rdx
    int $0x80
    movq %rax, %r12
    movq $1, %rax
    movq %r12, %rdi
    leaq ext2_msg(%rip), %rsi
    movq $ext2_end - ext2_msg, %rdx
    int $0x80
    movq $10, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    int $0x80
    movq $0, %rax
    movq %r12, %rdi
    leaq buf(%rip), %rsi
    movq $255, %rdx
    int $0x80
    movq %rax, %r13
    movq $1, %rax
    movq $1, %rdi
    leaq buf(%rip), %rsi
    movq %r13, %rdx
    int $0x80
    movq $3, %rax
    movq %r12, %rdi
    int $0x80
    movq $9, %rax
    leaq ext2_path(%rip), %rdi
    int $0x80

    movq $1, %rax
    movq $1, %rdi
    leaq ext2_done(%rip), %rsi
    movq $ext2_done_end - ext2_done, %rdx
    int $0x80

    movq $59, %rax
    leaq shell_path(%rip), %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    int $0x80

    movq $169, %rax
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
motd_path:
    .asciz "/etc/motd"
test_path:
    .asciz "/tmp/test"
vfs_msg:
    .ascii "vfs write/read works\n"
vfs_end:
fs_done:
    .ascii "fs test done\n"
fs_done_end:
ext2_path:
    .asciz "/mnt/test"
ext2_msg:
    .ascii "ext2 write/read works\n"
ext2_end:
ext2_done:
    .ascii "ext2 test done\n"
ext2_done_end:
shell_path:
    .asciz "/bin/sh"

.section .bss, "aw", @nobits
.align 16
buf:
    .skip 256
