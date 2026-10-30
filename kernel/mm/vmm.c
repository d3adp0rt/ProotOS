/*
 * ProotOS — virtual memory manager (M2).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mm.h"

struct vmalloc_hdr {
    uint32_t magic;
    uint32_t pages;
    uint64_t phys;
    uint64_t size;
    uint64_t reserved;
};

#define VMALLOC_MAGIC 0x564d414c4c4f4331ULL

static uint64_t *g_pml4;
static uint64_t g_vmalloc_next;

static uint64_t *table_from_entry(uint64_t *entry, uint64_t flags) {
    if (!(*entry & PTE_PRESENT)) {
        uint64_t phys = pmm_alloc_pages(1, PMM_ZONE_ANY);
        if (!phys)
            return 0;
        uint64_t *table = (uint64_t *)PHYS_TO_VIRT(phys);
        memset(table, 0, PAGE_SIZE);
        *entry = phys | PTE_PRESENT | PTE_WRITE | (flags & PTE_USER);
    }
    return (uint64_t *)PHYS_TO_VIRT(*entry & ~0xfffULL);
}

void vmm_init(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    g_pml4 = (uint64_t *)PHYS_TO_VIRT(cr3 & ~0xfffULL);
    g_vmalloc_next = VMM_VMALLOC_BASE + 0x100000ULL;
    kprintf("[vmm] PML4 at %p, vmalloc from %p\n", (void *)(cr3 & ~0xfffULL),
            (void *)g_vmalloc_next);
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!g_pml4 || (virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
        return -1;

    uint64_t *pdpt = table_from_entry(&g_pml4[(virt >> 39) & 0x1ff], flags);
    if (!pdpt)
        return -1;
    uint64_t *pd = table_from_entry(&pdpt[(virt >> 30) & 0x1ff], flags);
    if (!pd)
        return -1;
    uint64_t pdi = (virt >> 21) & 0x1ff;
    if (pd[pdi] & PTE_PS)
        return -1;
    uint64_t *pt = table_from_entry(&pd[pdi], flags);
    if (!pt)
        return -1;

    uint64_t *pte = &pt[(virt >> 12) & 0x1ff];
    if (*pte & PTE_PRESENT)
        return -1;
    *pte = (phys & ~0xfffULL) | (flags & ~PTE_PRESENT) | PTE_PRESENT;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_unmap(uint64_t virt) {
    if (!g_pml4 || (virt & (PAGE_SIZE - 1)))
        return -1;

    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(g_pml4[(virt >> 39) & 0x1ff] & ~0xfffULL);
    if (!(g_pml4[(virt >> 39) & 0x1ff] & PTE_PRESENT))
        return -1;
    if (!(pdpt[(virt >> 30) & 0x1ff] & PTE_PRESENT))
        return -1;
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[(virt >> 30) & 0x1ff] & ~0xfffULL);
    uint64_t pdi = (virt >> 21) & 0x1ff;
    if (!(pd[pdi] & PTE_PRESENT) || (pd[pdi] & PTE_PS))
        return -1;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[pdi] & ~0xfffULL);
    uint64_t *pte = &pt[(virt >> 12) & 0x1ff];
    if (!(*pte & PTE_PRESENT))
        return -1;
    *pte = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_is_mapped(uint64_t virt) {
    if (!g_pml4)
        return 0;
    uint64_t e = g_pml4[(virt >> 39) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return 0;
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(e & ~0xfffULL);
    e = pdpt[(virt >> 30) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return 0;
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(e & ~0xfffULL);
    e = pd[(virt >> 21) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return 0;
    if (e & PTE_PS)
        return 1;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(e & ~0xfffULL);
    return (pt[(virt >> 12) & 0x1ff] & PTE_PRESENT) ? 1 : 0;
}

uint64_t vmm_get_phys(uint64_t virt) {
    if (!vmm_is_mapped(virt))
        return 0;
    uint64_t e = g_pml4[(virt >> 39) & 0x1ff];
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(e & ~0xfffULL);
    e = pdpt[(virt >> 30) & 0x1ff];
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(e & ~0xfffULL);
    e = pd[(virt >> 21) & 0x1ff];
    if (e & PTE_PS)
        return (e & 0x000fffffffe00000ULL) | (virt & 0x1fffffULL);
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(e & ~0xfffULL);
    e = pt[(virt >> 12) & 0x1ff];
    return (e & 0x000ffffffffff000ULL) | (virt & 0xfff);
}

void *vmalloc(size_t size) {
    if (size == 0)
        size = 1;
    uint64_t total = size + sizeof(struct vmalloc_hdr);
    uint64_t pages = (total + PAGE_SIZE - 1) >> PAGE_SHIFT;
    uint64_t bytes = pages * PAGE_SIZE;
    if (g_vmalloc_next + bytes > VMM_VMALLOC_END)
        return 0;

    uint64_t phys = pmm_alloc_pages(pages, PMM_ZONE_ANY);
    if (!phys)
        return 0;

    uint64_t base = g_vmalloc_next;
    for (uint64_t i = 0; i < pages; i++) {
        if (vmm_map(base + i * PAGE_SIZE, phys + i * PAGE_SIZE, PTE_PRESENT | PTE_WRITE | PTE_NX) !=
            0) {
            for (uint64_t j = 0; j < i; j++)
                vmm_unmap(base + j * PAGE_SIZE);
            pmm_free_pages(phys, pages);
            return 0;
        }
    }
    g_vmalloc_next += bytes;

    memset(PHYS_TO_VIRT(phys), 0, bytes);
    struct vmalloc_hdr *hdr = (struct vmalloc_hdr *)(uintptr_t)base;
    hdr->magic = (uint32_t)VMALLOC_MAGIC;
    hdr->pages = (uint32_t)pages;
    hdr->phys = phys;
    hdr->size = size;
    return (void *)(uintptr_t)(base + sizeof(struct vmalloc_hdr));
}

void vfree(void *ptr) {
    if (!ptr)
        return;
    struct vmalloc_hdr *hdr = (struct vmalloc_hdr *)((uintptr_t)ptr - sizeof(struct vmalloc_hdr));
    if (hdr->magic != (uint32_t)VMALLOC_MAGIC)
        return;
    uint64_t base = (uintptr_t)hdr;
    uint64_t pages = hdr->pages;
    uint64_t phys = hdr->phys;
    hdr->magic = 0;
    for (uint64_t i = 0; i < pages; i++)
        vmm_unmap(base + i * PAGE_SIZE);
    pmm_free_pages(phys, pages);
}
