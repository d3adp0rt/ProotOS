/*
 * ProotOS — M4 scheduler self-tests.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sched.h"
#include "timer.h"
#include "proc.h"

static volatile int g_stop_workers;
static volatile uint64_t g_counters[4];
static volatile uint64_t g_sleep_delta;
static volatile int g_waiter_ran;
static volatile int g_waker_ran;
static volatile int g_normal_stop;
static volatile uint64_t g_normal_counter;
static volatile uint64_t g_excl_before;
static volatile uint64_t g_excl_after;

static struct wait_queue g_wq;

struct worker_arg {
    int index;
};

static void check(int condition, const char *message) {
    if (!condition)
        panic("ktest: %s", message);
}

static void sleeper(void *arg) {
    (void)arg;
    uint64_t start = timer_ticks();
    kthread_sleep_ms(100);
    g_sleep_delta = timer_ticks() - start;
    kthread_exit();
}

static void waiter(void *arg) {
    (void)arg;
    wait_queue_wait(&g_wq);
    g_waiter_ran = 1;
    kthread_exit();
}

static void waker(void *arg) {
    (void)arg;
    kthread_sleep_ms(50);
    wait_queue_wake_all(&g_wq);
    g_waker_ran = 1;
    kthread_exit();
}

static void cpu_worker(void *arg) {
    struct worker_arg *w = arg;
    while (!g_stop_workers) {
        __atomic_add_fetch(&g_counters[w->index], 1, __ATOMIC_RELAXED);
    }
    kthread_exit();
}

static void normal_worker(void *arg) {
    (void)arg;
    while (!g_normal_stop) {
        __atomic_add_fetch(&g_normal_counter, 1, __ATOMIC_RELAXED);
    }
    kthread_exit();
}

static void exclusive_worker(void *arg) {
    (void)arg;
    g_excl_before = g_normal_counter;
    uint64_t end = timer_ticks() + 100;
    while (timer_ticks() < end) {
        __asm__ volatile("pause");
    }
    g_excl_after = g_normal_counter;
    kthread_exit();
}

void m4_selftest_thread(void *arg) {
    (void)arg;
    kprintf("[ktest] starting M4 scheduler tests\n");

    /* Sleep/wakeup accuracy. */
    struct thread *s = kthread_create(sleeper, 0, "sleeper", SCHED_NORMAL, 0, 0);
    check(s != 0, "create sleeper");
    check(kthread_join(s) == 0, "join sleeper");
    check(g_sleep_delta >= 99 && g_sleep_delta <= 130, "sleep 100ms accuracy");
    kprintf("[ktest] sleep: 100 ms -> %llu ticks\n", (unsigned long long)g_sleep_delta);

    /* Wait queue without lost wakeups. */
    struct thread *w = kthread_create(waiter, 0, "waiter", SCHED_NORMAL, 0, 0);
    struct thread *k = kthread_create(waker, 0, "waker", SCHED_NORMAL, 0, 0);
    check(w && k, "create waiter/waker");
    check(kthread_join(w) == 0, "join waiter");
    check(kthread_join(k) == 0, "join waker");
    check(g_waiter_ran && g_waker_ran, "wait queue wakeup");
    kprintf("[ktest] wait queue: ok\n");

    /* EEVDF nice ratios. */
    static struct worker_arg wargs[4] = {{0}, {1}, {2}, {3}};
    static const int nices[4] = {-10, 0, 10, 19};
    struct thread *workers[4];
    g_stop_workers = 0;
    for (int i = 0; i < 4; i++) {
        g_counters[i] = 0;
        workers[i] = kthread_create(cpu_worker, &wargs[i], "worker", SCHED_NORMAL, nices[i], 0);
        check(workers[i] != 0, "create cpu worker");
    }
    kthread_sleep_ms(1000);
    g_stop_workers = 1;
    for (int i = 0; i < 4; i++)
        check(kthread_join(workers[i]) == 0, "join cpu worker");

    uint64_t total = g_counters[0] + g_counters[1] + g_counters[2] + g_counters[3];
    check(total > 0, "workers made progress");
    check(g_counters[0] > g_counters[1], "nice -10 > nice 0");
    check(g_counters[1] > g_counters[2], "nice 0 > nice 10");
    check(g_counters[2] > g_counters[3], "nice 10 > nice 19");
    kprintf("[ktest] nice ratios: -10:%llu%% 0:%llu%% 10:%llu%% 19:%llu%%\n",
            (unsigned long long)(g_counters[0] * 100 / total),
            (unsigned long long)(g_counters[1] * 100 / total),
            (unsigned long long)(g_counters[2] * 100 / total),
            (unsigned long long)(g_counters[3] * 100 / total));

    /* SCHED_EXCLUSIVE must starve normal threads while it runs. */
    g_normal_stop = 0;
    g_normal_counter = 0;
    struct thread *normal = kthread_create(normal_worker, 0, "normal", SCHED_NORMAL, 0, 0);
    check(normal != 0, "create normal worker");
    kthread_sleep_ms(200);
    check(g_normal_counter > 0, "normal worker progress");
    struct thread *excl = kthread_create(exclusive_worker, 0, "exclusive", SCHED_EXCLUSIVE, 0, 0);
    check(excl != 0, "create exclusive");
    check(kthread_join(excl) == 0, "join exclusive");
    g_normal_stop = 1;
    check(kthread_join(normal) == 0, "join normal");
    check(g_excl_before == g_excl_after, "exclusive starvation");
    kprintf("[ktest] exclusive: normal counter frozen %llu -> %llu\n",
            (unsigned long long)g_excl_before, (unsigned long long)g_excl_after);

    kprintf("[ktest] M4 scheduler tests: PASS\n");
    kprintf("M4 complete: EEVDF, RT/exclusive classes, sleep, wait queues, join and preemption.\n");
    process_spawn_init();
    kthread_exit();
}
