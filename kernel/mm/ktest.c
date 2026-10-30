/*
 * ProotOS — M2 memory self-tests.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mm.h"

static void check(int condition, const char *message) {
    if (!condition)
        panic("ktest: %s", message);
}

static void test_pmm(void) {
    struct pmm_stats before = pmm_get_stats();

    uint64_t p1 = pmm_alloc_pages(1, PMM_ZONE_ANY);
    check(p1 != 0 && (p1 & (PAGE_SIZE - 1)) == 0, "pmm single page");
    memset(PHYS_TO_VIRT(p1), 0xa5, PAGE_SIZE);
    check(*(volatile uint8_t *)PHYS_TO_VIRT(p1) == 0xa5, "pmm page write");
    pmm_free_pages(p1, 1);

    uint64_t p3 = pmm_alloc_pages(3, PMM_ZONE_ANY);
    check(p3 != 0 && (p3 & (PAGE_SIZE - 1)) == 0, "pmm order-2 page");
    memset(PHYS_TO_VIRT(p3), 0x5a, 4 * PAGE_SIZE);
    pmm_free_pages(p3, 3);

    uint64_t p64 = pmm_alloc_pages(64, PMM_ZONE_ANY);
    check(p64 != 0, "pmm 64 pages");
    memset(PHYS_TO_VIRT(p64), 0x11, 64 * PAGE_SIZE);
    pmm_free_pages(p64, 64);

    struct pmm_stats after = pmm_get_stats();
    check(before.free_pages == after.free_pages, "pmm free-page accounting");
    kprintf("[ktest] pmm: ok (%llu MiB free)\n",
            (unsigned long long)(after.free_pages * PAGE_SIZE >> 20));
}

static void test_kmalloc(void) {
    static const size_t sizes[] = {1, 7, 16, 31, 64, 100, 255, 1000, 4000, 9000};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint8_t *p = kmalloc(sizes[i]);
        check(p != 0, "kmalloc returned null");
        check(((uintptr_t)p & 0xf) == 0, "kmalloc alignment");
        memset(p, (int)(0x30 + i), sizes[i]);
        check(p[0] == (uint8_t)(0x30 + i) && p[sizes[i] - 1] == (uint8_t)(0x30 + i),
              "kmalloc write");
        kfree(p);
    }

    uint8_t *z = kzalloc(512);
    check(z != 0, "kzalloc returned null");
    for (size_t i = 0; i < 512; i++)
        check(z[i] == 0, "kzalloc not zero");
    kfree(z);

    uint8_t *r = kmalloc(64);
    check(r != 0, "krealloc source");
    for (int i = 0; i < 64; i++)
        r[i] = (uint8_t)i;
    r = krealloc(r, 2000);
    check(r != 0, "krealloc target");
    for (int i = 0; i < 64; i++)
        check(r[i] == (uint8_t)i, "krealloc contents");
    kfree(r);

    void *objects[512];
    for (int i = 0; i < 512; i++) {
        objects[i] = kmalloc(64);
        check(objects[i] != 0, "kmalloc bulk");
    }
    for (int i = 0; i < 512; i++)
        kfree(objects[i]);

    kprintf("[ktest] kmalloc: ok\n");
}

static void test_vmalloc(void) {
    uint8_t *p = vmalloc(8192);
    check(p != 0, "vmalloc returned null");
    for (int i = 0; i < 8192; i += 512)
        p[i] = (uint8_t)(i >> 9);
    for (int i = 0; i < 8192; i += 512)
        check(p[i] == (uint8_t)(i >> 9), "vmalloc write");
    check(vmm_is_mapped((uintptr_t)p & ~(PAGE_SIZE - 1)), "vmalloc not mapped");
    vfree(p);
    kprintf("[ktest] vmalloc: ok\n");
}

static void test_vmm(void) {
    uint64_t pa = pmm_alloc_pages(1, PMM_ZONE_ANY);
    uint64_t pa2 = pmm_alloc_pages(1, PMM_ZONE_ANY);
    check(pa && pa2, "vmm test pages");

    check(vmm_map(VMM_TEST_BASE, pa, VMM_PRESENT | VMM_WRITE | VMM_NX) == 0, "vmm_map");
    check(vmm_is_mapped(VMM_TEST_BASE), "vmm mapped");
    check(vmm_get_phys(VMM_TEST_BASE) == pa, "vmm phys");
    *(volatile uint64_t *)VMM_TEST_BASE = 0x1122334455667788ULL;
    check(*(volatile uint64_t *)VMM_TEST_BASE == 0x1122334455667788ULL, "vmm write");

    check(vmm_map(VMM_TEST_BASE + 0x2000, pa2, VMM_PRESENT | VMM_WRITE | VMM_NX) == 0,
          "vmm guard map");
    check(vmm_unmap(VMM_TEST_BASE + 0x2000) == 0, "vmm guard unmap");
    check(vmm_is_mapped(VMM_TEST_BASE), "vmm guard first");
    check(!vmm_is_mapped(VMM_TEST_BASE + 0x2000), "vmm guard second");
    check(vmm_unmap(VMM_TEST_BASE) == 0, "vmm unmap");
    check(!vmm_is_mapped(VMM_TEST_BASE), "vmm unmapped");

    pmm_free_pages(pa, 1);
    pmm_free_pages(pa2, 1);
    kprintf("[ktest] vmm + guard pages: ok\n");
}

void mm_selftest(void) {
    kprintf("[ktest] starting M2 memory tests\n");
    test_pmm();
    test_kmalloc();
    test_vmalloc();
    test_vmm();
    pmm_dump();
    kmalloc_dump();
    kprintf("[ktest] M2 memory tests: PASS\n");
}
