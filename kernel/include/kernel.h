/*
 * ProotOS — kernel common definitions.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "../../boot/bootinfo.h"

#define KERNEL_NAME "ProotOS"
#define KERNEL_VERSION "0.0.1"
#define KERNEL_ARCH "x86_64"

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

void console_init(const struct proot_bootinfo *bi);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ksnprintf(char *out, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);

void idt_init(void);
void idt_load(void);

struct trap_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rsi, rdi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

void exception_dispatch(struct trap_frame *tf) __attribute__((noreturn));

static inline __attribute__((noreturn)) void cpu_halt(void) {
    for (;;)
        __asm__ volatile("cli; hlt");
}
