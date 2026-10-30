/*
 * ProotOS — syscall dispatch (M5, int 0x80 ABI).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "proc.h"
#include "sched.h"
#include "timer.h"

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_GETPID 2
#define SYS_CLOSE 3
#define SYS_SLEEP_MS 4
#define SYS_YIELD 5
#define SYS_PIPE 22
#define SYS_FORK 57
#define SYS_EXECVE 59
#define SYS_EXIT 60
#define SYS_WAIT4 61
#define SYS_KILL 62
#define SYS_PTRACE 101

static long sys_write(struct process *p, int fd, uint64_t uptr, size_t len) {
    char buf[128];
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);
        if (user_copy_from(p, buf, uptr + done, chunk) != 0)
            return -14;
        if (fd == 1 || fd == 2) {
            char text[129];
            memcpy(text, buf, chunk);
            text[chunk] = 0;
            console_write(text);
            done += chunk;
        } else {
            long n = process_write(fd, buf, chunk);
            if (n < 0)
                return n;
            done += (size_t)n;
            if ((size_t)n < chunk)
                break;
        }
    }
    return (long)done;
}

static long sys_read(struct process *p, int fd, uint64_t uptr, size_t len) {
    char buf[128];
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);
        long n = process_read(fd, buf, chunk);
        if (n < 0)
            return n;
        if (n == 0)
            break;
        if (user_copy_to(p, uptr + done, buf, (size_t)n) != 0)
            return -14;
        done += (size_t)n;
        if ((size_t)n < chunk)
            break;
    }
    return (long)done;
}

static long sys_pipe(struct process *p, uint64_t uptr) {
    int fds[2];
    if (process_pipe(fds) != 0)
        return -1;
    if (user_copy_to(p, uptr, fds, sizeof(fds)) != 0)
        return -14;
    return 0;
}

static long sys_execve(struct process *p, uint64_t upath) {
    char path[64];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    if (process_exec(p, path) != 0)
        return -2;
    process_enter_user(p);
}

void syscall_dispatch(struct trap_frame *tf) {
    struct process *p = process_current();
    if (!p) {
        tf->rax = (uint64_t)-1;
        return;
    }

    long ret = -38; /* -ENOSYS */
    switch (tf->rax) {
    case SYS_READ:
        ret = sys_read(p, (int)tf->rdi, tf->rsi, (size_t)tf->rdx);
        break;
    case SYS_WRITE:
        ret = sys_write(p, (int)tf->rdi, tf->rsi, (size_t)tf->rdx);
        break;
    case SYS_GETPID:
        ret = p->pid;
        break;
    case SYS_CLOSE:
        process_close_fd((int)tf->rdi);
        ret = 0;
        break;
    case SYS_YIELD:
        kthread_yield();
        ret = 0;
        break;
    case SYS_SLEEP_MS:
        kthread_sleep_ms(tf->rdi);
        ret = 0;
        break;
    case SYS_PIPE:
        ret = sys_pipe(p, tf->rdi);
        break;
    case SYS_FORK:
        ret = (long)process_fork(tf);
        break;
    case SYS_EXECVE:
        ret = sys_execve(p, tf->rdi);
        break;
    case SYS_WAIT4:
        ret = process_wait((int)tf->rdi, tf->rsi);
        break;
    case SYS_KILL:
        if ((int)tf->rdi == (int)p->pid && (int)tf->rsi == 9)
            process_exit(137);
        ret = 0;
        break;
    case SYS_PTRACE:
        ret = process_ptrace((int)tf->rdi, (int)tf->rsi, tf->rdx, tf->r10);
        break;
    case SYS_EXIT:
        process_exit((int)tf->rdi);
        break;
    default:
        break;
    }
    tf->rax = (uint64_t)ret;
}
