/*
 * ProotOS — virtual memory manager (M2/M5).
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

static uint64_t *g_current_pml4;
static uint64_t g_current_pml4_phys;
static uint64_t *g_kernel_pml4;
static uint64_t g_kernel_pml4_phys;
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
    return (uint64_t *)PHYS_TO_VIRT(*entry & 0x000ffffffffff000ULL);
}

void vmm_init(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    g_current_pml4_phys = cr3 & 0x000ffffffffff000ULL;
    g_kernel_pml4_phys = g_current_pml4_phys;
    g_current_pml4 = (uint64_t *)PHYS_TO_VIRT(g_current_pml4_phys);
    g_kernel_pml4 = g_current_pml4;
    g_vmalloc_next = VMM_VMALLOC_BASE + 0x100000ULL;
    kprintf("[vmm] PML4 at %p, vmalloc from %p\n", (void *)g_current_pml4_phys,
            (void *)g_vmalloc_next);
}

uint64_t vmm_current_pml4(void) {
    return g_current_pml4_phys;
}

uint64_t vmm_kernel_pml4(void) {
    return g_kernel_pml4_phys;
}

void vmm_switch(uint64_t pml4_phys) {
    if (!pml4_phys)
        pml4_phys = g_kernel_pml4_phys;
    if (pml4_phys == g_current_pml4_phys)
        return;
    g_current_pml4_phys = pml4_phys;
    g_current_pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_create_pml4(void) {
    uint64_t phys = pmm_alloc_pages(1, PMM_ZONE_ANY);
    if (!phys)
        return 0;
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(phys);
    memset(pml4, 0, PAGE_SIZE);
    /* Kernel mappings are shared; the lower half belongs to the process. */
    for (int i = 256; i < 512; i++)
        pml4[i] = g_kernel_pml4[i];
    return phys;
}

int vmm_map_pml4(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4_phys || (virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
        return -1;
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    uint64_t *pdpt = table_from_entry(&pml4[(virt >> 39) & 0x1ff], flags);
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
    *pte = (phys & 0x000ffffffffff000ULL) | (flags & ~PTE_PRESENT) | PTE_PRESENT;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_unmap_pml4(uint64_t pml4_phys, uint64_t virt) {
    if (!pml4_phys || (virt & (PAGE_SIZE - 1)))
        return -1;
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);
    uint64_t e = pml4[(virt >> 39) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return -1;
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pdpt[(virt >> 30) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return -1;
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pd[(virt >> 21) & 0x1ff];
    if (!(e & PTE_PRESENT) || (e & PTE_PS))
        return -1;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    uint64_t *pte = &pt[(virt >> 12) & 0x1ff];
    if (!(*pte & PTE_PRESENT))
        return -1;
    *pte = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_is_mapped_pml4(uint64_t pml4_phys, uint64_t virt) {
    if (!pml4_phys)
        return 0;
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);
    uint64_t e = pml4[(virt >> 39) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return 0;
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pdpt[(virt >> 30) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return 0;
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pd[(virt >> 21) & 0x1ff];
    if (!(e & PTE_PRESENT))
        return 0;
    if (e & PTE_PS)
        return 1;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    return (pt[(virt >> 12) & 0x1ff] & PTE_PRESENT) ? 1 : 0;
}

uint64_t vmm_get_phys_pml4(uint64_t pml4_phys, uint64_t virt) {
    if (!vmm_is_mapped_pml4(pml4_phys, virt))
        return 0;
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);
    uint64_t e = pml4[(virt >> 39) & 0x1ff];
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pdpt[(virt >> 30) & 0x1ff];
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pd[(virt >> 21) & 0x1ff];
    if (e & PTE_PS)
        return (e & 0x000fffffffe00000ULL) | (virt & 0x1fffffULL);
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(e & 0x000ffffffffff000ULL);
    e = pt[(virt >> 12) & 0x1ff];
    return (e & 0x000ffffffffff000ULL) | (virt & 0xfff);
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    return vmm_map_pml4(g_current_pml4_phys, virt, phys, flags);
}

int vmm_unmap(uint64_t virt) {
    return vmm_unmap_pml4(g_current_pml4_phys, virt);
}

int vmm_is_mapped(uint64_t virt) {
    return vmm_is_mapped_pml4(g_current_pml4_phys, virt);
}

uint64_t vmm_get_phys(uint64_t virt) {
    return vmm_get_phys_pml4(g_current_pml4_phys, virt);
}

int vmm_copy_user_space(uint64_t src_pml4, uint64_t dst_pml4) {
    uint64_t *src = (uint64_t *)PHYS_TO_VIRT(src_pml4);
    for (int i = 0; i < 256; i++) {
        if (!(src[i] & PTE_PRESENT))
            continue;
        uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(src[i] & 0x000ffffffffff000ULL);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PTE_PRESENT))
                continue;
            if (pdpt[j] & PTE_PS)
                return -1;
            uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[j] & 0x000ffffffffff000ULL);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PTE_PRESENT))
                    continue;
                if (pd[k] & PTE_PS)
                    return -1;
                uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[k] & 0x000ffffffffff000ULL);
                for (int l = 0; l < 512; l++) {
                    uint64_t pte = pt[l];
                    if (!(pte & PTE_PRESENT))
                        continue;
                    uint64_t vaddr = ((uint64_t)i << 39) | ((uint64_t)j << 30) |
                                     ((uint64_t)k << 21) | ((uint64_t)l << 12);
                    uint64_t new_phys = pmm_alloc_pages(1, PMM_ZONE_ANY);
                    if (!new_phys)
                        return -1;
                    memcpy(PHYS_TO_VIRT(new_phys), PHYS_TO_VIRT(pte & 0x000ffffffffff000ULL),
                           PAGE_SIZE);
                    if (vmm_map_pml4(dst_pml4, vaddr, new_phys,
                                     pte & (PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NX)) != 0)
                        return -1;
                }
            }
        }
    }
    return 0;
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
