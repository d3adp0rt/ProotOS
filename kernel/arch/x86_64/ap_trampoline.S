/*
 * ProotOS — Application Processor startup trampoline.
 *
 * Linked at physical 0x8000.  The BSP copies this blob there and sends
 * INIT-SIPI-SIPI with vector 0x08.  In real mode the AP finds its parameters
 * at 0x7000, switches through protected mode into long mode and calls the
 * kernel entry with the per-AP argument in RDI.
 */
.section .text, "ax", @progbits

.code16
.global _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x6f00, %sp

    lgdtl gdt_desc

    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    ljmpl $0x18, $pm_entry

.code32
pm_entry:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movl 0x7000, %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $0x20, %eax          /* CR4.PAE */
    movl %eax, %cr4

    movl $0xc0000080, %ecx
    rdmsr
    orl $0x900, %eax         /* EFER.LME | EFER.NXE */
    wrmsr

    movl %cr0, %eax
    orl $0x80000001, %eax    /* CR0.PG | CR0.PE */
    movl %eax, %cr0

    ljmpl $0x08, $lm_entry

.code64
lm_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movq 0x7008, %rax        /* kernel entry */
    movq 0x7010, %rsp        /* per-AP stack top */
    movq 0x7018, %rdi        /* per-AP argument */
    call *%rax

1:
    cli
    hlt
    jmp 1b

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff  /* 0x08: 64-bit code */
    .quad 0x00af92000000ffff  /* 0x10: 64-bit data */
    .quad 0x00cf9a000000ffff  /* 0x18: 32-bit code */
    .quad 0x00cf92000000ffff  /* 0x20: 32-bit data */
gdt_end:

gdt_desc:
    .word gdt_end - gdt - 1
    .long gdt
