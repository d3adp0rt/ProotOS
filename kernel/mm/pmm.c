/*
 * ProotOS — physical memory manager: DMA32/Normal buddy allocator.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mm.h"

#define META_FREE 1
#define META_ALLOC 2
#define META_CONT 3
#define META_RESERVED 4

struct page_meta {
    uint8_t order;
    uint8_t flags;
    uint8_t zone;
    uint8_t pad;
};

struct free_node {
    struct free_node *next;
    struct free_node *prev;
};

struct zone_info {
    uint64_t start;
    uint64_t end;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
};

static struct page_meta *g_meta;
static uint64_t g_meta_count;
static uint64_t g_meta_phys;
static uint64_t g_meta_size;
static struct free_node *g_free[3][MAX_ORDER + 1];
static struct zone_info g_zones[3];

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static unsigned ceil_log2(uint64_t value) {
    unsigned order = 0;
    while ((1ULL << order) < value)
        order++;
    return order;
}

static struct page_meta *meta_for(uint64_t phys) {
    uint64_t index = phys >> PAGE_SHIFT;
    if (!g_meta || index >= g_meta_count)
        return 0;
    return &g_meta[index];
}

static struct free_node *node_for(uint64_t phys) {
    return (struct free_node *)PHYS_TO_VIRT(phys);
}

static void mark_meta(uint64_t phys, unsigned order, uint8_t flags, uint8_t zone) {
    uint64_t pages = 1ULL << order;
    for (uint64_t i = 0; i < pages; i++) {
        struct page_meta *m = meta_for(phys + i * PAGE_SIZE);
        if (!m)
            return;
        m->flags = flags;
        m->zone = zone;
        m->order = (i == 0) ? (uint8_t)order : 0xff;
    }
}

static void free_list_insert(uint64_t phys, unsigned order, uint8_t zone) {
    struct free_node *node = node_for(phys);
    node->prev = 0;
    node->next = g_free[zone][order];
    if (node->next)
        node->next->prev = node;
    g_free[zone][order] = node;
    mark_meta(phys, order, META_FREE, zone);
    g_zones[zone].free_pages += 1ULL << order;
}

static void free_list_remove(uint64_t phys, unsigned order, uint8_t zone) {
    struct free_node *node = node_for(phys);
    if (node->prev)
        node->prev->next = node->next;
    else
        g_free[zone][order] = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (g_zones[zone].free_pages >= (1ULL << order)) {
        g_zones[zone].free_pages -= 1ULL << order;
    }
}

static void insert_range(uint8_t zone, uint64_t start, uint64_t end) {
    start = align_up(start, PAGE_SIZE);
    end = align_down(end, PAGE_SIZE);
    if (start >= end)
        return;

    g_zones[zone].total_pages += (end - start) >> PAGE_SHIFT;
    while (start < end) {
        unsigned order = MAX_ORDER;
        while (order > 0) {
            uint64_t block = 1ULL << order;
            uint64_t bytes = block * PAGE_SIZE;
            if ((start & (bytes - 1)) == 0 && start + bytes <= end)
                break;
            order--;
        }
        free_list_insert(start, order, zone);
        start += (1ULL << order) * PAGE_SIZE;
    }
}

static void add_range_split(uint8_t zone, uint64_t start, uint64_t end) {
    start = align_up(start, PAGE_SIZE);
    end = align_down(end, PAGE_SIZE);
    if (start >= end)
        return;

    uint64_t meta_start = g_meta_phys;
    uint64_t meta_end = g_meta_phys + g_meta_size;

    if (end <= meta_start || start >= meta_end) {
        insert_range(zone, start, end);
        return;
    }
    if (start < meta_start)
        insert_range(zone, start, meta_start);
    uint64_t after = meta_end > end ? end : meta_end;
    if (end > meta_end)
        insert_range(zone, after, end);
}

static void add_physical_range(uint64_t start, uint64_t end) {
    if (start >= PMM_MAX_PHYS)
        return;
    if (end > PMM_MAX_PHYS)
        end = PMM_MAX_PHYS;

    if (start < 0x100000ULL)
        start = 0x100000ULL;
    if (start >= end)
        return;

    const uint64_t four_gib = 0x100000000ULL;
    if (start < four_gib) {
        add_range_split(PMM_ZONE_DMA32, start, end < four_gib ? end : four_gib);
    }
    if (end > four_gib) {
        add_range_split(PMM_ZONE_NORMAL, start > four_gib ? start : four_gib, end);
    }
}

void pmm_init(const struct proot_bootinfo *bi) {
    memset(g_free, 0, sizeof(g_free));
    memset(g_zones, 0, sizeof(g_zones));
    g_meta = 0;
    g_meta_count = 0;
    g_meta_size = 0;
    g_meta_phys = 0;

    const uint8_t *base = (const uint8_t *)(uintptr_t)(PROOT_HHDM_BASE + bi->memory_map);
    uint64_t max_phys = 0;
    for (uint64_t off = 0; off + bi->memory_map_desc_size <= bi->memory_map_size;
         off += bi->memory_map_desc_size) {
        const struct proot_memory_descriptor *d =
            (const struct proot_memory_descriptor *)(base + off);
        if (!((d->type >= 1 && d->type <= 7) || d->type == 9 || d->type == 10 || d->type == 14)) {
            continue;
        }
        uint64_t end = d->physical_start + d->number_of_pages * PAGE_SIZE;
        if (end > max_phys)
            max_phys = end;
    }
    if (max_phys > PMM_MAX_PHYS)
        max_phys = PMM_MAX_PHYS;
    if (max_phys < 0x1000000ULL)
        max_phys = 0x1000000ULL;

    g_meta_count = align_up(max_phys, PAGE_SIZE) >> PAGE_SHIFT;
    g_meta_size = align_up(g_meta_count * sizeof(struct page_meta), PAGE_SIZE);

    /* Carve the page metadata out of the first suitable conventional range. */
    for (uint64_t off = 0; off + bi->memory_map_desc_size <= bi->memory_map_size;
         off += bi->memory_map_desc_size) {
        const struct proot_memory_descriptor *d =
            (const struct proot_memory_descriptor *)(base + off);
        if (d->type != 7)
            continue;
        uint64_t start = align_up(d->physical_start, PAGE_SIZE);
        uint64_t end = align_down(d->physical_start + d->number_of_pages * PAGE_SIZE, PAGE_SIZE);
        if (start < 0x100000ULL)
            start = 0x100000ULL;
        if (end <= start)
            continue;
        if (end - start >= g_meta_size) {
            g_meta_phys = start;
            break;
        }
    }
    if (!g_meta_phys)
        panic("pmm: no room for page metadata (%llu bytes)", (unsigned long long)g_meta_size);

    g_meta = (struct page_meta *)PHYS_TO_VIRT(g_meta_phys);
    memset(g_meta, 0, g_meta_size);
    for (uint64_t i = 0; i < (g_meta_size >> PAGE_SHIFT); i++) {
        g_meta[(g_meta_phys >> PAGE_SHIFT) + i].flags = META_RESERVED;
        g_meta[(g_meta_phys >> PAGE_SHIFT) + i].order = 0;
    }

    for (uint64_t off = 0; off + bi->memory_map_desc_size <= bi->memory_map_size;
         off += bi->memory_map_desc_size) {
        const struct proot_memory_descriptor *d =
            (const struct proot_memory_descriptor *)(base + off);
        if (d->type != 7)
            continue; /* only conventional RAM is usable */
        add_physical_range(d->physical_start, d->physical_start + d->number_of_pages * PAGE_SIZE);
    }

    g_zones[PMM_ZONE_DMA32].start = 0;
    g_zones[PMM_ZONE_DMA32].end = 0x100000000ULL;
    g_zones[PMM_ZONE_NORMAL].start = 0x100000000ULL;
    g_zones[PMM_ZONE_NORMAL].end = PMM_MAX_PHYS;

    kprintf("[pmm] metadata %llu KiB at %p, max phys %llu MiB\n",
            (unsigned long long)(g_meta_size >> 10), (void *)g_meta_phys,
            (unsigned long long)(max_phys >> 20));
}

uint64_t pmm_alloc_pages(uint64_t count, enum pmm_zone zone) {
    if (count == 0 || count > MAX_ORDER_PAGES)
        return 0;
    unsigned want = ceil_log2(count);

    int first = (zone == PMM_ZONE_ANY) ? PMM_ZONE_NORMAL : (int)zone;
    int second = (zone == PMM_ZONE_ANY) ? PMM_ZONE_DMA32 : -1;

    for (int attempt = 0; attempt < 2; attempt++) {
        int z = (attempt == 0) ? first : second;
        if (z <= 0 || z > 2)
            continue;

        unsigned order = want;
        while (order <= MAX_ORDER && !g_free[z][order])
            order++;
        if (order > MAX_ORDER)
            continue;

        struct free_node *node = g_free[z][order];
        uint64_t addr = (uint64_t)((uintptr_t)node - PROOT_HHDM_BASE);
        free_list_remove(addr, order, (uint8_t)z);

        while (order > want) {
            order--;
            uint64_t buddy = addr + ((1ULL << order) * PAGE_SIZE);
            free_list_insert(buddy, order, (uint8_t)z);
        }

        mark_meta(addr, want, META_ALLOC, (uint8_t)z);
        g_zones[z].allocated_pages += 1ULL << want;
        return addr;
    }
    return 0;
}

void pmm_free_pages(uint64_t phys, uint64_t count) {
    (void)count;
    if (phys & (PAGE_SIZE - 1))
        return;
    struct page_meta *head = meta_for(phys);
    if (!head || head->flags != META_ALLOC)
        return;

    unsigned order = head->order;
    uint8_t zone = head->zone;
    if (zone < 1 || zone > 2 || order > MAX_ORDER)
        return;

    uint64_t pages = 1ULL << order;
    if (g_zones[zone].allocated_pages >= pages)
        g_zones[zone].allocated_pages -= pages;

    while (order < MAX_ORDER) {
        uint64_t buddy = phys ^ ((1ULL << order) * PAGE_SIZE);
        struct page_meta *bm = meta_for(buddy);
        if (!bm || bm->flags != META_FREE || bm->order != order || bm->zone != zone)
            break;
        if (buddy < g_zones[zone].start || buddy + (1ULL << order) * PAGE_SIZE > g_zones[zone].end)
            break;
        free_list_remove(buddy, order, zone);
        if (buddy < phys)
            phys = buddy;
        order++;
    }
    free_list_insert(phys, order, zone);
}

struct pmm_stats pmm_get_stats(void) {
    struct pmm_stats s = {0};
    for (int z = 1; z <= 2; z++) {
        s.total_pages += g_zones[z].total_pages;
        s.free_pages += g_zones[z].free_pages;
        s.allocated_pages += g_zones[z].allocated_pages;
    }
    s.reserved_pages = s.total_pages - s.free_pages - s.allocated_pages;
    return s;
}

void pmm_dump(void) {
    struct pmm_stats s = pmm_get_stats();
    kprintf("[pmm] total=%llu MiB free=%llu MiB allocated=%llu MiB reserved=%llu MiB\n",
            (unsigned long long)(s.total_pages * PAGE_SIZE >> 20),
            (unsigned long long)(s.free_pages * PAGE_SIZE >> 20),
            (unsigned long long)(s.allocated_pages * PAGE_SIZE >> 20),
            (unsigned long long)(s.reserved_pages * PAGE_SIZE >> 20));
    for (int z = 1; z <= 2; z++) {
        kprintf("[pmm] zone %s: %llu MiB free, largest order ",
                z == PMM_ZONE_DMA32 ? "dma32" : "normal",
                (unsigned long long)(g_zones[z].free_pages * PAGE_SIZE >> 20));
        int largest = -1;
        for (int o = MAX_ORDER; o >= 0; o--) {
            if (g_free[z][o]) {
                largest = o;
                break;
            }
        }
        if (largest < 0)
            kprintf("none\n");
        else
            kprintf("%d (%llu KiB)\n", largest,
                    (unsigned long long)((1ULL << largest) * PAGE_SIZE >> 10));
    }
}
