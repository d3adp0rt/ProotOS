/*
 * ProotOS — slab/kmalloc kernel heap (M2).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mm.h"

#define KMALLOC_MAGIC 0x4b4d414cU
#define HDR_SIZE 16
#define HDR_FLAG_FREE 1
#define HDR_FLAG_ALLOC 2
#define HDR_FLAG_LARGE 4

struct alloc_hdr {
    uint32_t magic;
    uint16_t cls;
    uint16_t flags;
    union {
        struct alloc_hdr *next;
        struct {
            uint32_t size;
            uint32_t pages;
        };
    };
};

struct kmem_cache {
    size_t obj_size;
    struct alloc_hdr *free;
    uint64_t allocations;
    uint64_t frees;
};

static const size_t class_sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
#define CLASS_COUNT (sizeof(class_sizes) / sizeof(class_sizes[0]))
static struct kmem_cache g_caches[CLASS_COUNT];

static unsigned slab_order(size_t obj_size) {
    size_t need = obj_size * 8;
    unsigned order = 0;
    while ((PAGE_SIZE << order) < need && order < 5)
        order++;
    return order;
}

static void slab_refill(struct kmem_cache *cache, unsigned cls) {
    unsigned order = slab_order(cache->obj_size);
    uint64_t pages = 1ULL << order;
    uint64_t phys = pmm_alloc_pages(pages, PMM_ZONE_ANY);
    if (!phys)
        panic("kmalloc: out of pages for slab");

    uint8_t *base = (uint8_t *)PHYS_TO_VIRT(phys);
    memset(base, 0, pages * PAGE_SIZE);
    for (uint64_t off = 0; off + cache->obj_size <= pages * PAGE_SIZE; off += cache->obj_size) {
        struct alloc_hdr *hdr = (struct alloc_hdr *)(base + off);
        hdr->magic = KMALLOC_MAGIC;
        hdr->cls = (uint16_t)cls;
        hdr->flags = HDR_FLAG_FREE;
        hdr->next = cache->free;
        cache->free = hdr;
    }
}

void kmalloc_init(void) {
    memset(g_caches, 0, sizeof(g_caches));
    for (size_t i = 0; i < CLASS_COUNT; i++) {
        g_caches[i].obj_size = class_sizes[i];
    }
    kprintf("[kmalloc] %u size classes, header %u bytes\n", (unsigned)CLASS_COUNT,
            (unsigned)HDR_SIZE);
}

void *kmalloc(size_t size) {
    if (size == 0)
        size = 1;
    size_t total = size + HDR_SIZE;

    for (size_t i = 0; i < CLASS_COUNT; i++) {
        if (total > class_sizes[i])
            continue;
        struct kmem_cache *cache = &g_caches[i];
        if (!cache->free)
            slab_refill(cache, (unsigned)i);
        struct alloc_hdr *hdr = cache->free;
        cache->free = hdr->next;
        hdr->magic = KMALLOC_MAGIC;
        hdr->cls = (uint16_t)i;
        hdr->flags = HDR_FLAG_ALLOC;
        hdr->size = (uint32_t)size;
        hdr->pages = 0;
        cache->allocations++;
        return (void *)((uint8_t *)hdr + HDR_SIZE);
    }

    uint64_t pages = (total + PAGE_SIZE - 1) >> PAGE_SHIFT;
    uint64_t phys = pmm_alloc_pages(pages, PMM_ZONE_ANY);
    if (!phys)
        return 0;
    struct alloc_hdr *hdr = (struct alloc_hdr *)PHYS_TO_VIRT(phys);
    hdr->magic = KMALLOC_MAGIC;
    hdr->cls = 0xffff;
    hdr->flags = HDR_FLAG_ALLOC | HDR_FLAG_LARGE;
    hdr->size = (uint32_t)size;
    hdr->pages = (uint32_t)pages;
    return (void *)((uint8_t *)hdr + HDR_SIZE);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr)
        return;
    struct alloc_hdr *hdr = (struct alloc_hdr *)((uint8_t *)ptr - HDR_SIZE);
    if (hdr->magic != KMALLOC_MAGIC)
        return;

    if (hdr->flags & HDR_FLAG_LARGE) {
        uint64_t pages = hdr->pages;
        uint64_t phys = VIRT_TO_PHYS(hdr);
        hdr->magic = 0;
        pmm_free_pages(phys, pages);
        return;
    }

    uint16_t cls = hdr->cls;
    if (cls >= CLASS_COUNT)
        return;
    struct kmem_cache *cache = &g_caches[cls];
    hdr->flags = HDR_FLAG_FREE;
    hdr->next = cache->free;
    cache->free = hdr;
    cache->frees++;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    struct alloc_hdr *hdr = (struct alloc_hdr *)((uint8_t *)ptr - HDR_SIZE);
    if (hdr->magic != KMALLOC_MAGIC)
        return 0;
    size_t old = hdr->size;
    if (size <= old)
        return ptr;
    void *new_ptr = kmalloc(size);
    if (!new_ptr)
        return 0;
    memcpy(new_ptr, ptr, old);
    kfree(ptr);
    return new_ptr;
}

void kmalloc_dump(void) {
    for (size_t i = 0; i < CLASS_COUNT; i++) {
        uint64_t free_objects = 0;
        for (struct alloc_hdr *h = g_caches[i].free; h; h = h->next)
            free_objects++;
        kprintf("[kmalloc] class %4llu: free=%llu alloc=%llu\n", (unsigned long long)class_sizes[i],
                (unsigned long long)free_objects, (unsigned long long)g_caches[i].allocations);
    }
}
