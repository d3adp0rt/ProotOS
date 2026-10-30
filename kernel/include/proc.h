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
};

void process_init(void);
struct process *process_spawn_init(void);
struct process *process_current(void);
uint32_t process_fork(struct trap_frame *tf);
int process_wait(int pid, uint64_t status_uptr);
void process_exit(int code) __attribute__((noreturn));

int user_copy_from(struct process *p, void *dst, uint64_t uptr, size_t len);
int user_copy_to(struct process *p, uint64_t uptr, const void *src, size_t len);
