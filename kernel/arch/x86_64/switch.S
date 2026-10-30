/*
 * ProotOS — kernel thread context switch.
 *
 * void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
 * Saves the callee-saved registers on the old stack, stores RSP, loads the
 * new RSP, restores registers and returns into the new thread.
 */
.section .text, "ax", @progbits
.global context_switch
.type context_switch, @function
context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)
    movq %rsi, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size context_switch, . - context_switch

/*
 * Initial thread entry: the initial stack contains [fn][arg] above the
 * return address, so pop them and call fn(arg).
 */
.section .text, "ax", @progbits
.global thread_start
.type thread_start, @function
thread_start:
    popq %rax
    popq %rdi
    sti
    call *%rax
    call kthread_exit
1:
    cli
    hlt
    jmp 1b
.size thread_start, . - thread_start
