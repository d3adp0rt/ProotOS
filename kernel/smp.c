/*
 * ProotOS — SMP AP bring-up via INIT-SIPI-SIPI and the 0x8000 trampoline.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "smp.h"
#include "mm.h"
#include "apic.h"
#include "timer.h"
#include "sched.h"

#define AP_TRAMPOLINE_PHYS 0x8000ULL
#define AP_PARAMS_PHYS 0x7000ULL
#define AP_STACK_SIZE 16384

extern const uint8_t ap_trampoline_start[];
extern const uint8_t ap_trampoline_end[];

struct ap_arg {
    uint32_t index;
    uint32_t apic_id;
};

static struct acpi_info g_acpi;
static struct ap_arg g_args[ACPI_MAX_CPUS];
static volatile uint32_t g_online = 1;

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void ap_main(struct ap_arg *arg) __attribute__((noreturn));
static void ap_main(struct ap_arg *arg) {
    lapic_init(&g_acpi);
    __atomic_add_fetch(&g_online, 1, __ATOMIC_SEQ_CST);
    kprintf("[smp] AP%u online (apic %u, id %u)\n", arg->index, arg->apic_id, lapic_id());
    sched_init_ap();
}

uint32_t smp_online_cpus(void) {
    return g_online;
}

void smp_init(const struct acpi_info *acpi) {
    g_acpi = *acpi;
    if (acpi->cpu_count <= 1) {
        kprintf("[smp] single CPU\n");
        return;
    }

    uint64_t blob_size = (uint64_t)(ap_trampoline_end - ap_trampoline_start);
    if (blob_size > 0x1000)
        panic("smp: trampoline too large");
    memcpy(PHYS_TO_VIRT(AP_TRAMPOLINE_PHYS), ap_trampoline_start, blob_size);
    kprintf("[smp] trampoline %llu bytes at 0x8000, %u CPUs\n", (unsigned long long)blob_size,
            acpi->cpu_count);

    volatile uint64_t *params = (volatile uint64_t *)PHYS_TO_VIRT(AP_PARAMS_PHYS);
    uint64_t cr3 = read_cr3();

    for (uint32_t i = 1; i < acpi->cpu_count; i++) {
        void *stack = kmalloc(AP_STACK_SIZE);
        if (!stack) {
            kprintf("[smp] no stack for AP%u\n", i);
            break;
        }
        g_args[i].index = i;
        g_args[i].apic_id = acpi->apic_ids[i];

        params[0] = cr3;
        params[1] = (uint64_t)(uintptr_t)ap_main;
        params[2] = (uint64_t)(uintptr_t)stack + AP_STACK_SIZE;
        params[3] = (uint64_t)(uintptr_t)&g_args[i];

        kprintf("[smp] waking AP%u (apic %u)\n", i, acpi->apic_ids[i]);
        lapic_send_ipi(acpi->apic_ids[i], 0x00004500); /* INIT, assert */
        timer_sleep_ms(10);
        lapic_send_ipi(acpi->apic_ids[i], 0x00004608); /* SIPI, vector 0x08 */
        timer_sleep_ms(1);
        lapic_send_ipi(acpi->apic_ids[i], 0x00004608);

        for (int spin = 0; spin < 100; spin++) {
            if (g_online > i)
                break;
            timer_sleep_ms(10);
        }
        if (g_online <= i) {
            kprintf("[smp] AP%u did not come online\n", i);
        }
    }

    for (int spin = 0; spin < 200 && g_online < acpi->cpu_count; spin++) {
        timer_sleep_ms(10);
    }
    kprintf("[smp] online CPUs: %u/%u\n", g_online, acpi->cpu_count);
}
