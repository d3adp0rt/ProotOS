/*
 * ProotOS — processes and user address spaces (M5).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"
#include "sched.h"

#define USER_LIMIT 0x0000800000000000ULL
#define USER_STACK_TOP 0x00007ffffff00000ULL
#define USER_STACK_PAGES 16
#define PROC_MAX_FDS 16

struct pipe;
struct file {
    int type;
    struct pipe *pipe;
    int write_end;
    int refs;
};

enum process_state {
    PROC_RUNNING = 1,
    PROC_ZOMBIE = 2,
};

struct process {
    uint32_t pid;
    uint32_t ppid;
    uint64_t pml4_phys;
    uint64_t entry;
    uint64_t user_stack_top;
    uint64_t brk;
    int state;
    int exit_code;
    struct process *parent;
    struct process *children;
    struct process *sibling;
    struct thread *thread;
    struct wait_queue child_wq;
    struct file *fds[PROC_MAX_FDS];
    int traced;
    struct process *next_all;
};

void process_init(void);
struct process *process_spawn_init(void);
struct process *process_current(void);
uint32_t process_fork(struct trap_frame *tf);
int process_wait(int pid, uint64_t status_uptr);
int process_exec(struct process *p, const char *name);
void process_enter_user(struct process *p) __attribute__((noreturn));
void process_segfault(void) __attribute__((noreturn));
int process_pipe(int out_fds[2]);
long process_read(int fd, void *buf, size_t len);
long process_write(int fd, const void *buf, size_t len);
void process_close_fd(int fd);
void process_fd_copy(struct process *parent, struct process *child);
struct process *process_find(uint32_t pid);
long process_ptrace(int request, int pid, uint64_t addr, uint64_t data);
void process_exit(int code) __attribute__((noreturn));

int user_copy_from(struct process *p, void *dst, uint64_t uptr, size_t len);
int user_copy_to(struct process *p, uint64_t uptr, const void *src, size_t len);
