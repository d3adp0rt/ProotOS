/*
 * ProotOS — boot protocol shared by the UEFI loader and the kernel.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

#define PROOT_BOOTINFO_MAGIC 0x50524f4f544f5331ULL /* "PROOTOS1" */
#define PROOT_BOOTINFO_VERSION 2u
#define PROOT_CMDLINE_MAX 256u
#define PROOT_LOADER_NAME_MAX 32u

/* Values for bootinfo.framebuffer_format. */
enum {
    PROOT_FB_FORMAT_RGB = 0,
    PROOT_FB_FORMAT_BGR = 1,
    PROOT_FB_FORMAT_BITMASK = 2,
};

/* Mirror of EFI_MEMORY_DESCRIPTOR, stable for the kernel. */
struct proot_memory_descriptor {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} __attribute__((packed));

struct proot_bootinfo {
    uint64_t magic;
    uint32_t version;
    uint32_t boot_flags;

    /* Physical address of an array of proot_memory_descriptor. */
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_desc_size;

    /* Framebuffer: base is a kernel virtual address. */
    uint64_t framebuffer_base;
    /* Physical address of the framebuffer, for user-space mapping (M10). */
    uint64_t framebuffer_phys;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint32_t framebuffer_format;
    uint32_t reserved0;

    /* Kernel image (virtual base/size and physical base). */
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t kernel_phys;

    /* Physical address of the ACPI RSDP, 0 if absent. */
    uint64_t rsdp;

    /* Physical address and size of the initramfs archive. */
    uint64_t initramfs_base;
    uint64_t initramfs_size;

    char cmdline[PROOT_CMDLINE_MAX];
    char loader[PROOT_LOADER_NAME_MAX];
} __attribute__((packed));

#define PROOT_HHDM_BASE 0xffff800000000000ULL
#define PROOT_KERNEL_BASE 0xffffffff80000000ULL
#define PROOT_FB_BASE 0xffffc00000000000ULL
