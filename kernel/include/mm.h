/*
 * ProotOS — memory management interfaces (M2).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12
#define MAX_ORDER 10
#define MAX_ORDER_PAGES (1ULL << MAX_ORDER)
#define PMM_MAX_PHYS (64ULL * 1024 * 1024 * 1024)

#define PHYS_TO_VIRT(p) ((void *)(uintptr_t)(PROOT_HHDM_BASE + (uint64_t)(p)))
#define VIRT_TO_PHYS(v) ((uint64_t)(uintptr_t)(v) - PROOT_HHDM_BASE)

#define VMM_VMALLOC_BASE 0xffffa00000000000ULL
#define VMM_VMALLOC_END 0xffffb00000000000ULL
#define VMM_TEST_BASE 0xffffa00000010000ULL

#define PTE_PRESENT 1ULL
#define PTE_WRITE 2ULL
#define PTE_USER 4ULL
#define PTE_PS 0x80ULL
#define PTE_NX (1ULL << 63)

#define VMM_PRESENT PTE_PRESENT
#define VMM_WRITE PTE_WRITE
#define VMM_USER PTE_USER
#define VMM_NX PTE_NX

enum pmm_zone {
    PMM_ZONE_ANY = 0,
    PMM_ZONE_DMA32 = 1,
    PMM_ZONE_NORMAL = 2,
};

struct pmm_stats {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t reserved_pages;
};

void pmm_init(const struct proot_bootinfo *bi);
uint64_t pmm_alloc_pages(uint64_t count, enum pmm_zone zone);
void pmm_free_pages(uint64_t phys, uint64_t count);
struct pmm_stats pmm_get_stats(void);
void pmm_dump(void);

void kmalloc_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
void kmalloc_dump(void);

void vmm_init(void);
int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_unmap(uint64_t virt);
int vmm_is_mapped(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
void *vmalloc(size_t size);
void vfree(void *ptr);

void mm_selftest(void);
