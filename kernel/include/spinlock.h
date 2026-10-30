/*
 * ProotOS — spinlocks and interrupt-safe locking (M3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_lock(spinlock_t *lock) {
    while (1) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&lock->locked, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED)) {
            return;
        }
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
            __asm__ volatile("pause");
        }
    }
}

static inline void spin_unlock(spinlock_t *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9))
        __asm__ volatile("sti" : : : "memory");
    else
        __asm__ volatile("cli" : : : "memory");
}

static inline uint64_t spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spin_unlock(lock);
    irq_restore(flags);
}
