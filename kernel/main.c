/*
 * ProotOS — kernel entry (M1).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "mm.h"
#include "acpi.h"
#include "apic.h"
#include "timer.h"
#include "smp.h"
#include "sched.h"
#include "gdt.h"
#include "initramfs.h"
#include "proc.h"
#include "vfs.h"
#include "pci.h"
#include "ahci.h"
#include "ata.h"
#include "virtio_blk.h"

static const char *memory_type_name(uint32_t type) {
    switch (type) {
    case 0:
        return "reserved";
    case 1:
        return "loader-code";
    case 2:
        return "loader-data";
    case 3:
        return "boot-code";
    case 4:
        return "boot-data";
    case 5:
        return "rt-code";
    case 6:
        return "rt-data";
    case 7:
        return "conventional";
    case 8:
        return "unusable";
    case 9:
        return "acpi-reclaim";
    case 10:
        return "acpi-nvs";
    case 11:
        return "mmio";
    case 12:
        return "mmio-port";
    case 13:
        return "pal";
    case 14:
        return "persistent";
    default:
        return "unknown";
    }
}

static void print_size(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL) {
        kprintf("%llu MiB", (unsigned long long)(bytes >> 20));
    } else if (bytes >= 1024ULL) {
        kprintf("%llu KiB", (unsigned long long)(bytes >> 10));
    } else {
        kprintf("%llu B", (unsigned long long)bytes);
    }
}

static void print_memory_map(const struct proot_bootinfo *bi) {
    const uint8_t *base = (const uint8_t *)(uintptr_t)(PROOT_HHDM_BASE + bi->memory_map);
    uint64_t total_ram = 0;
    uint64_t count = 0;

    for (uint64_t off = 0; off + bi->memory_map_desc_size <= bi->memory_map_size;
         off += bi->memory_map_desc_size) {
        const struct proot_memory_descriptor *d =
            (const struct proot_memory_descriptor *)(base + off);
        if (d->type == 7)
            total_ram += d->number_of_pages * 4096ULL;
        count++;
    }

    kprintf("[mem] %llu descriptors, %llu MiB conventional RAM\n", (unsigned long long)count,
            (unsigned long long)(total_ram >> 20));

    int shown = 0;
    for (uint64_t off = 0; off + bi->memory_map_desc_size <= bi->memory_map_size;
         off += bi->memory_map_desc_size) {
        const struct proot_memory_descriptor *d =
            (const struct proot_memory_descriptor *)(base + off);
        if (shown >= 10) {
            kprintf("[mem] ...\n");
            break;
        }
        kprintf("[mem] %-13s %p + ", memory_type_name(d->type), (void *)d->physical_start);
        print_size(d->number_of_pages * 4096ULL);
        kprintf("\n");
        shown++;
    }
}

void kmain(struct proot_bootinfo *bi) {
    serial_init();
    serial_write("\n[prootos] kernel entered\n");

    if (!bi || bi->magic != PROOT_BOOTINFO_MAGIC) {
        serial_write("[prootos] invalid bootinfo\n");
        cpu_halt();
    }

    console_init(bi);
    console_clear();

    kprintf("  ____            _    ___  ____\n");
    kprintf(" |  _ \\ _ __ ___ | |_ / _ \\/ ___|\n");
    kprintf(" | |_) | '__/ _ \\| __| | | \\___ \\\n");
    kprintf(" |  __/| | | (_) | |_| |_| |___) |\n");
    kprintf(" |_|   |_|  \\___/ \\__|\\___/|____/\n");
    kprintf("  %s %s (%s) - M2 memory management\n\n", KERNEL_NAME, KERNEL_VERSION, KERNEL_ARCH);

    kprintf("[boot] loader: %s\n", bi->loader);
    kprintf("[boot] cmdline: %s\n", bi->cmdline);
    kprintf("[boot] kernel: %p..%p (phys %p)\n", (void *)bi->kernel_base,
            (void *)(bi->kernel_base + bi->kernel_size), (void *)bi->kernel_phys);

    if (bi->framebuffer_base) {
        kprintf("[fb] %ux%u %u bpp pitch=%u at %p\n", bi->framebuffer_width, bi->framebuffer_height,
                bi->framebuffer_bpp, bi->framebuffer_pitch, (void *)bi->framebuffer_base);
    } else {
        kprintf("[fb] no GOP framebuffer, using VGA text fallback\n");
    }

    if (bi->rsdp) {
        const char *sig = (const char *)(uintptr_t)(PROOT_HHDM_BASE + bi->rsdp);
        char sigbuf[9];
        memcpy(sigbuf, sig, 8);
        sigbuf[8] = 0;
        kprintf("[acpi] RSDP at %p signature='%s'\n", (void *)bi->rsdp, sigbuf);
    } else {
        kprintf("[acpi] no RSDP\n");
    }

    if (bi->initramfs_base && bi->initramfs_size) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)(PROOT_HHDM_BASE + bi->initramfs_base);
        int cpio = (bi->initramfs_size >= 6 && memcmp(p, "070701", 6) == 0);
        kprintf("[initramfs] %llu bytes at %p (%s)\n", (unsigned long long)bi->initramfs_size,
                (void *)bi->initramfs_base, cpio ? "cpio newc" : "unknown format");
    } else {
        kprintf("[initramfs] none\n");
    }

    print_memory_map(bi);

    initramfs_init(bi);
    idt_init();
    gdt_init();
    kprintf("[cpu] IDT installed, exceptions armed\n");

    pmm_init(bi);
    vmm_init();
    kmalloc_init();
    pci_init();
    virtio_blk_init();
    if (!virtio_blk_device()) {
        ata_init();
        if (!ata_device()) ahci_init();
    }
    vfs_init();
    pmm_dump();
    mm_selftest();

    struct acpi_info acpi;
    if (acpi_init(bi->rsdp, &acpi) == 0) {
        lapic_init(&acpi);
        ioapic_init(&acpi);
        ioapic_unmask(acpi.irq_gsi[0], 32, lapic_id());
    } else {
        kprintf("[acpi] MADT unavailable, falling back to PIC/PIT only\n");
    }

    timer_init();
    rtc_print();

    __asm__ volatile("sti");
    kprintf("[time] waiting for 1000 timer ticks...\n");
    timer_sleep_ms(1000);
    kprintf("[time] uptime ticks=%llu (~1 second)\n", (unsigned long long)timer_ticks());
    timer_sleep_ms(10);
    kprintf("[time] uptime ticks=%llu (+10 ms)\n", (unsigned long long)timer_ticks());

    smp_init(&acpi);
    process_init();

    kprintf("[sched] starting M4/M5 tests...\n");
    struct thread *test = kthread_create(m4_selftest_thread, 0, "m4-test", SCHED_NORMAL, 0, 0);
    if (!test) panic("cannot create M4 test thread");
    sched_start();

    cpu_halt();
}
