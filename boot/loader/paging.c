/*
 * ProotOS — bootloader page-table construction.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loader.h"

#define PAGE_SIZE 4096ULL
#define PTE_PRESENT 1ULL
#define PTE_WRITE 2ULL
#define PTE_PS 0x80ULL
#define PTE_NX (1ULL << 63)

static uint64_t alloc_pt(void) {
    uint64_t p = uefi_alloc_pages(PAGE_SIZE, 0xffffffffULL);
    memset((void *)(uintptr_t)p, 0, PAGE_SIZE);
    return p;
}

static uint64_t *get_pd(uint64_t *pdpt, uint64_t index) {
    if (!(pdpt[index] & PTE_PRESENT)) {
        uint64_t pd = alloc_pt();
        pdpt[index] = pd | PTE_PRESENT | PTE_WRITE;
    }
    return (uint64_t *)(uintptr_t)(pdpt[index] & ~0xfffULL);
}

static void map_2m(uint64_t *pdpt, uint64_t vaddr, uint64_t paddr, uint64_t size, uint64_t flags) {
    for (uint64_t off = 0; off < size; off += 0x200000ULL) {
        uint64_t v = vaddr + off;
        uint64_t p = paddr + off;
        uint64_t *pd = get_pd(pdpt, (v >> 30) & 0x1ff);
        pd[(v >> 21) & 0x1ff] = (p & ~0x1fffffULL) | flags | PTE_PS;
    }
}

static void map_4k(uint64_t *pdpt, uint64_t vaddr, uint64_t paddr, uint64_t size, uint64_t flags) {
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        uint64_t v = vaddr + off;
        uint64_t p = paddr + off;
        uint64_t *pd = get_pd(pdpt, (v >> 30) & 0x1ff);
        uint64_t pdi = (v >> 21) & 0x1ff;
        if (!(pd[pdi] & PTE_PRESENT)) {
            uint64_t pt = alloc_pt();
            pd[pdi] = pt | PTE_PRESENT | PTE_WRITE;
        } else if (pd[pdi] & PTE_PS) {
            fail("4K map conflicts with 2M mapping at %p", (void *)v);
        }
        uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pdi] & ~0xfffULL);
        pt[(v >> 12) & 0x1ff] = (p & ~0xfffULL) | flags;
    }
}

struct paging_result paging_init(uint64_t identity_limit, const struct loaded_elf *kernel,
                                 uint64_t fb_phys, uint64_t fb_size) {
    struct paging_result res = {0, 0};

    uint64_t pml4_phys = alloc_pt();
    uint64_t pdpt_id = alloc_pt();
    uint64_t pdpt_hhdm = alloc_pt();
    uint64_t pdpt_fb = alloc_pt();
    uint64_t pdpt_kern = alloc_pt();
    uint64_t *pml4 = (uint64_t *)(uintptr_t)pml4_phys;

    pml4[0] = pdpt_id | PTE_PRESENT | PTE_WRITE;
    pml4[256] = pdpt_hhdm | PTE_PRESENT | PTE_WRITE;
    pml4[384] = pdpt_fb | PTE_PRESENT | PTE_WRITE;
    pml4[511] = pdpt_kern | PTE_PRESENT | PTE_WRITE;

    if (identity_limit < 0x100000000ULL)
        identity_limit = 0x100000000ULL;
    /* One PDPT covers 512 GiB; larger physical ranges need another level. */
    if (identity_limit > 0x8000000000ULL)
        identity_limit = 0x8000000000ULL;
    identity_limit = (identity_limit + 0x1fffffULL) & ~0x1fffffULL;

    uint64_t *id = (uint64_t *)(uintptr_t)pdpt_id;
    uint64_t *hh = (uint64_t *)(uintptr_t)pdpt_hhdm;

    /* Identity map: the loader keeps executing here until the final jump. */
    map_2m(id, 0, 0, identity_limit, PTE_PRESENT | PTE_WRITE);
    /* HHDM: direct map of physical memory at PROOT_HHDM_BASE. */
    map_2m(hh, PROOT_HHDM_BASE, 0, identity_limit, PTE_PRESENT | PTE_WRITE | PTE_NX);

    /* Kernel image at its linked virtual address. */
    map_4k((uint64_t *)(uintptr_t)pdpt_kern, kernel->virt_base, kernel->phys_base,
           kernel->virt_size, PTE_PRESENT | PTE_WRITE);

    /* Framebuffer at a stable virtual address. */
    if (fb_phys && fb_size) {
        uint64_t base = fb_phys & ~0xfffULL;
        uint64_t off = fb_phys - base;
        uint64_t size = (fb_size + off + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        map_4k((uint64_t *)(uintptr_t)pdpt_fb, PROOT_FB_BASE, base, size,
               PTE_PRESENT | PTE_WRITE | PTE_NX);
        res.fb_virt = PROOT_FB_BASE + off;
    }

    res.pml4_phys = pml4_phys;
    console_printf("[loader] paging: PML4=%p identity=%llu GiB fb=%p\n", (void *)pml4_phys,
                   (unsigned long long)(identity_limit >> 30), (void *)res.fb_virt);
    return res;
}
