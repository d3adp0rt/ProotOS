/*
 * ProotOS — EEVDF-based kernel thread scheduler (M4).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sched.h"
#include "mm.h"
#include "apic.h"
#include "timer.h"
#include "gdt.h"

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void thread_start(void);
extern void idt_load(void);

struct cpu {
    struct thread *current;
    struct thread *idle;
    struct thread *run_normal;
    struct thread *run_rt;
    struct thread *run_excl;
    struct thread *sleep_list;
    uint64_t min_vruntime;
    volatile uint32_t need_resched;
    uint32_t index;
    spinlock_t lock;
};

static struct cpu g_cpus[SCHED_MAX_CPUS];
static struct thread g_boot_thread;
static uint32_t g_next_tid = 1;

static const uint32_t nice_weight[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916, 9548, 7620, 6100, 4904,
    3906,  3121,  2501,  1991,  1586,  1277,  1024,  820,   655,   526,   423,  335,  272,  215,
    172,   137,   110,   87,    70,    56,    45,    36,    29,    23,    18,   15,
};

static uint32_t weight_for(int nice) {
    if (nice < -20)
        nice = -20;
    if (nice > 19)
        nice = 19;
    return nice_weight[nice + 20];
}

static struct cpu *cpu_self(void) {
    uint32_t id = lapic_id() & (SCHED_MAX_CPUS - 1);
    return &g_cpus[id];
}

static void enqueue_locked(struct cpu *cpu, struct thread *t) {
    t->next = 0;
    t->state = THREAD_READY;
    if (t->vruntime < cpu->min_vruntime)
        t->vruntime = cpu->min_vruntime;

    if (t->cls == SCHED_EXCLUSIVE) {
        struct thread **pp = &cpu->run_excl;
        while (*pp)
            pp = &(*pp)->next;
        *pp = t;
        return;
    }
    if (t->cls == SCHED_RT) {
        struct thread **pp = &cpu->run_rt;
        while (*pp && (*pp)->rt_priority >= t->rt_priority)
            pp = &(*pp)->next;
        t->next = *pp;
        *pp = t;
        return;
    }

    struct thread **pp = &cpu->run_normal;
    while (*pp && (*pp)->vruntime <= t->vruntime)
        pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
}

static struct thread *pop_head(struct thread **head) {
    struct thread *t = *head;
    if (t) {
        *head = t->next;
        t->next = 0;
        t->state = THREAD_RUNNING;
        t->slice_used = 0;
    }
    return t;
}

static struct thread *pick_next_locked(struct cpu *cpu) {
    struct thread *t = pop_head(&cpu->run_excl);
    if (t)
        return t;
    t = pop_head(&cpu->run_rt);
    if (t)
        return t;
    t = pop_head(&cpu->run_normal);
    if (t) {
        if (t->vruntime > cpu->min_vruntime)
            cpu->min_vruntime = t->vruntime;
        return t;
    }
    cpu->idle->state = THREAD_RUNNING;
    return cpu->idle;
}

static void sched_enqueue_thread(struct thread *t) {
    if (!t)
        return;
    struct cpu *cpu = &g_cpus[t->cpu & (SCHED_MAX_CPUS - 1)];
    uint64_t flags = irq_save();
    spin_lock(&cpu->lock);
    enqueue_locked(cpu, t);
    cpu->need_resched = 1;
    spin_unlock(&cpu->lock);
    irq_restore(flags);
}

static struct thread *thread_alloc(void (*fn)(void *), void *arg, const char *name, int cls,
                                   int nice, int rt_priority) {
    struct thread *t = kzalloc(sizeof(*t));
    if (!t)
        return 0;
    t->stack = kzalloc(THREAD_STACK_SIZE);
    if (!t->stack) {
        kfree(t);
        return 0;
    }
    t->stack_size = THREAD_STACK_SIZE;
    t->tid = __atomic_add_fetch(&g_next_tid, 1, __ATOMIC_RELAXED);
    t->cpu = cpu_self()->index;
    t->pml4_phys = vmm_current_pml4();
    t->proc = 0;
    t->state = THREAD_READY;
    t->cls = (uint8_t)cls;
    t->nice = (int8_t)nice;
    t->rt_priority = (uint8_t)rt_priority;
    t->fn = fn;
    t->arg = arg;
    t->name = name;
    t->vruntime = cpu_self()->min_vruntime;
    t->join_wq.head = 0;
    t->join_wq.lock = (spinlock_t)SPINLOCK_INIT;

    uint64_t *sp = (uint64_t *)(t->stack + t->stack_size);
    *--sp = (uint64_t)(uintptr_t)arg;
    *--sp = (uint64_t)(uintptr_t)fn;
    *--sp = (uint64_t)(uintptr_t)thread_start;
    *--sp = 0; /* rbp */
    *--sp = 0; /* rbx */
    *--sp = 0; /* r12 */
    *--sp = 0; /* r13 */
    *--sp = 0; /* r14 */
    *--sp = 0; /* r15 */
    t->rsp = (uint64_t)(uintptr_t)sp;
    return t;
}

struct thread *kthread_create(void (*fn)(void *), void *arg, const char *name, int cls, int nice,
                              int rt_priority) {
    struct thread *t = thread_alloc(fn, arg, name, cls, nice, rt_priority);
    if (!t)
        return 0;
    sched_enqueue_thread(t);
    return t;
}

static void idle_main(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

void schedule(void) {
    uint64_t flags = irq_save();
    struct cpu *cpu = cpu_self();
    spin_lock(&cpu->lock);

    struct thread *prev = cpu->current;
    if (!prev) {
        struct thread *next = pick_next_locked(cpu);
        cpu->current = next;
        cpu->need_resched = 0;
        spin_unlock(&cpu->lock);
        irq_restore(flags);
        return;
    }

    if (prev != cpu->idle && prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        enqueue_locked(cpu, prev);
    }

    struct thread *next = pick_next_locked(cpu);
    cpu->current = next;
    cpu->need_resched = 0;
    spin_unlock(&cpu->lock);

    if (next != prev) {
        vmm_switch(next->pml4_phys);
        tss_set_rsp0((uint64_t)(uintptr_t)(next->stack + next->stack_size));
        context_switch(&prev->rsp, next->rsp);
    }
    irq_restore(flags);
}

struct thread *sched_current(void) {
    return cpu_self()->current;
}

void sched_tick(void) {
    struct cpu *cpu = cpu_self();
    uint64_t now = timer_ticks();
    int need = 0;

    spin_lock(&cpu->lock);
    struct thread **pp = &cpu->sleep_list;
    while (*pp) {
        struct thread *t = *pp;
        if (t->wake_tick <= now) {
            *pp = t->next;
            t->next = 0;
            enqueue_locked(cpu, t);
            need = 1;
        } else {
            pp = &t->next;
        }
    }

    struct thread *cur = cpu->current;
    if (cur && cur != cpu->idle) {
        if (cur->cls == SCHED_NORMAL) {
            cur->vruntime += (1024ULL * 1000ULL) / weight_for(cur->nice);
        } else if (cur->cls == SCHED_RT) {
            cur->slice_used++;
        }
    }

    if (cur && cur->cls == SCHED_EXCLUSIVE) {
        cpu->need_resched = 0;
        spin_unlock(&cpu->lock);
        return;
    }

    if (!cur || cur == cpu->idle) {
        if (cpu->run_excl || cpu->run_rt || cpu->run_normal)
            need = 1;
    } else {
        if (cpu->run_excl) {
            need = 1;
        } else if (cpu->run_rt && cur->cls != SCHED_RT) {
            need = 1;
        } else if (cur->cls == SCHED_NORMAL && cpu->run_normal) {
            struct thread *next = cpu->run_normal;
            if (next->vruntime + 4000 <= cur->vruntime)
                need = 1;
        } else if (cur->cls == SCHED_RT && cpu->run_rt) {
            struct thread *next = cpu->run_rt;
            if (next->rt_priority > cur->rt_priority || cur->slice_used >= 10)
                need = 1;
        }
    }

    cpu->need_resched = (uint32_t)need;
    spin_unlock(&cpu->lock);
}

int sched_need_resched(void) {
    return cpu_self()->need_resched != 0;
}

void sched_maybe_resched(int need_resched) {
    if (need_resched)
        schedule();
}

void sched_start(void) {
    struct cpu *cpu = cpu_self();
    cpu->idle = thread_alloc(idle_main, 0, "idle", SCHED_IDLE, 0, 0);
    if (!cpu->idle)
        panic("sched: cannot allocate idle thread");

    cpu->current = &g_boot_thread;
    g_boot_thread.cpu = cpu->index;
    g_boot_thread.state = THREAD_RUNNING;

    uint64_t flags = irq_save();
    spin_lock(&cpu->lock);
    struct thread *next = pick_next_locked(cpu);
    cpu->current = next;
    cpu->need_resched = 0;
    spin_unlock(&cpu->lock);
    if (next != &g_boot_thread)
        context_switch(&g_boot_thread.rsp, next->rsp);
    irq_restore(flags);
}

void sched_init_ap(void) {
    struct cpu *cpu = cpu_self();
    cpu->idle = 0;
    cpu->current = 0;
    idt_load();
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

void kthread_yield(void) {
    schedule();
}

void kthread_sleep_ms(uint64_t ms) {
    if (ms == 0) {
        kthread_yield();
        return;
    }
    uint64_t flags = irq_save();
    struct cpu *cpu = cpu_self();
    spin_lock(&cpu->lock);
    struct thread *cur = cpu->current;
    cur->state = THREAD_SLEEPING;
    cur->wake_tick = timer_ticks() + ms;
    cur->next = cpu->sleep_list;
    cpu->sleep_list = cur;
    spin_unlock(&cpu->lock);
    schedule();
    irq_restore(flags);
}

void wait_queue_wait(struct wait_queue *wq) {
    if (!wq)
        return;
    uint64_t flags = irq_save();
    struct cpu *cpu = cpu_self();
    spin_lock(&wq->lock);
    struct thread *cur = cpu->current;
    cur->state = THREAD_BLOCKED;
    cur->next = wq->head;
    wq->head = cur;
    spin_unlock(&wq->lock);
    schedule();
    irq_restore(flags);
}

void wait_queue_wake_all(struct wait_queue *wq) {
    if (!wq)
        return;
    uint64_t flags = irq_save();
    spin_lock(&wq->lock);
    struct thread *t = wq->head;
    wq->head = 0;
    spin_unlock(&wq->lock);
    while (t) {
        struct thread *next = t->next;
        t->next = 0;
        sched_enqueue_thread(t);
        t = next;
    }
    irq_restore(flags);
}

int kthread_join(struct thread *thread) {
    if (!thread)
        return -1;
    for (;;) {
        uint64_t flags = irq_save();
        spin_lock(&thread->join_wq.lock);
        if (thread->state == THREAD_DEAD) {
            int code = thread->exit_code;
            spin_unlock(&thread->join_wq.lock);
            irq_restore(flags);
            return code;
        }
        struct cpu *cpu = cpu_self();
        struct thread *cur = cpu->current;
        cur->state = THREAD_BLOCKED;
        cur->next = thread->join_wq.head;
        thread->join_wq.head = cur;
        spin_unlock(&thread->join_wq.lock);
        schedule();
        irq_restore(flags);
    }
}

void kthread_exit(void) {
    uint64_t flags = irq_save();
    struct cpu *cpu = cpu_self();
    struct thread *cur = cpu->current;
    if (!cur)
        cpu_halt();

    cur->state = THREAD_DEAD;
    cur->exit_code = 0;

    spin_lock(&cur->join_wq.lock);
    struct thread *waiters = cur->join_wq.head;
    cur->join_wq.head = 0;
    spin_unlock(&cur->join_wq.lock);

    while (waiters) {
        struct thread *next = waiters->next;
        waiters->next = 0;
        sched_enqueue_thread(waiters);
        waiters = next;
    }

    schedule();
    irq_restore(flags);
    cpu_halt();
}
