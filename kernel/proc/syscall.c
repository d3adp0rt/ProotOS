/*
 * ProotOS — syscall dispatch (M5, int 0x80 ABI).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "proc.h"
#include "sched.h"
#include "timer.h"

#define SYS_WRITE 1
#define SYS_GETPID 2
#define SYS_YIELD 3
#define SYS_SLEEP_MS 4
#define SYS_FORK 57
#define SYS_EXIT 60
#define SYS_WAIT4 61

static long sys_write(struct process *p, int fd, uint64_t uptr, size_t len) {
    if (fd != 1 && fd != 2)
        return -9;
    char buf[128];
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(buf) - 1)
            chunk = sizeof(buf) - 1;
        if (user_copy_from(p, buf, uptr + done, chunk) != 0)
            return -14;
        buf[chunk] = 0;
        console_write(buf);
        done += chunk;
    }
    return (long)done;
}

void syscall_dispatch(struct trap_frame *tf) {
    struct process *p = process_current();
    if (!p) {
        tf->rax = (uint64_t)-1;
        return;
    }

    long ret = -38; /* -ENOSYS */
    switch (tf->rax) {
    case SYS_WRITE:
        ret = sys_write(p, (int)tf->rdi, tf->rsi, (size_t)tf->rdx);
        break;
    case SYS_GETPID:
        ret = p->pid;
        break;
    case SYS_YIELD:
        kthread_yield();
        ret = 0;
        break;
    case SYS_SLEEP_MS:
        kthread_sleep_ms(tf->rdi);
        ret = 0;
        break;
    case SYS_FORK:
        ret = (long)process_fork(tf);
        break;
    case SYS_WAIT4:
        ret = process_wait((int)tf->rdi, tf->rsi);
        break;
    case SYS_EXIT:
        process_exit((int)tf->rdi);
        break;
    default:
        break;
    }
    tf->rax = (uint64_t)ret;
}
