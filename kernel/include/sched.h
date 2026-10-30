/*
 * ProotOS — kernel thread scheduler interfaces (M4).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"
#include "spinlock.h"

#define SCHED_MAX_CPUS 64
#define THREAD_STACK_SIZE 16384

enum thread_state {
    THREAD_READY = 0,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_DEAD,
};

enum sched_class {
    SCHED_IDLE = 0,
    SCHED_NORMAL,
    SCHED_RT,
    SCHED_EXCLUSIVE,
};

struct wait_queue;
struct thread;

struct wait_queue {
    struct thread *head;
    spinlock_t lock;
};
struct thread {
    uint64_t rsp;
    uint8_t *stack;
    size_t stack_size;
    uint32_t tid;
    uint32_t cpu;
    uint8_t state;
    uint8_t cls;
    int8_t nice;
    uint8_t rt_priority;
    uint64_t vruntime;
    uint64_t slice_used;
    uint64_t wake_tick;
    struct thread *next;
    struct wait_queue join_wq;
    const char *name;
    void (*fn)(void *);
    void *arg;
    int exit_code;
};

void sched_start(void);
void sched_init_ap(void) __attribute__((noreturn));
void sched_tick(void);
int sched_need_resched(void);
void sched_maybe_resched(int need_resched);
void schedule(void);

struct thread *kthread_create(void (*fn)(void *), void *arg, const char *name, int cls, int nice,
                              int rt_priority);
void kthread_exit(void) __attribute__((noreturn));
int kthread_join(struct thread *thread);
void kthread_yield(void);
void kthread_sleep_ms(uint64_t ms);

void wait_queue_wait(struct wait_queue *wq);
void wait_queue_wake_all(struct wait_queue *wq);

void m4_selftest_thread(void *arg);
