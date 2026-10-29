/*
 * ProotOS — UEFI loader internal interfaces.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "efi.h"
#include "../bootinfo.h"

extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_HANDLE gImageHandle;

/* libc-ish helpers (freestanding). */
void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);

/* Console and serial. */
void serial_init(void);
void serial_putc(char c);
void console_putc(char c);
void console_puts(const char *s);
void console_use_uefi(int enabled);
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fail(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* UEFI allocation helpers (all below 4 GiB). */
void *uefi_alloc_pool(size_t size);
uint64_t uefi_alloc_pages(size_t bytes, uint64_t max_address);
void uefi_free_pages(uint64_t addr, size_t bytes);

/* File system access. */
struct file_blob {
    void *data;
    uint64_t size;
    uint64_t physical;
};
int fs_read_file(EFI_HANDLE image_handle, const char *path, struct file_blob *out);

/* ELF loading. */
struct loaded_elf {
    uint64_t virt_base;
    uint64_t virt_size;
    uint64_t phys_base;
    uint64_t entry;
};
int elf_load(const struct file_blob *blob, struct loaded_elf *out);

/* Paging. */
struct paging_result {
    uint64_t pml4_phys;
    uint64_t fb_virt;
};
struct paging_result paging_init(uint64_t identity_limit, const struct loaded_elf *kernel,
                                 uint64_t fb_phys, uint64_t fb_size);

/* Final jump. */
void jump_to_kernel(uint64_t entry, uint64_t bootinfo_virt, uint64_t pml4_phys)
    __attribute__((noreturn));

/* config */
struct boot_config {
    char kernel[128];
    char initramfs[128];
    char cmdline[PROOT_CMDLINE_MAX];
    int video_width;
    int video_height;
};
void config_defaults(struct boot_config *cfg);
void config_parse(struct boot_config *cfg, const char *data, uint64_t size);
