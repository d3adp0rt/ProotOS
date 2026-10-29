/*
 * ProotOS — bootloader utility code: freestanding string/format helpers,
 * serial console, UEFI allocation and config parsing.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loader.h"
#include <stdarg.h>

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
EFI_HANDLE gImageHandle;

static int g_uefi_console = 0;

/* ------------------------------------------------------------------ */
/* memory/string                                                       */
/* ------------------------------------------------------------------ */

void *memset(void *dst, int value, size_t n) {
    unsigned char *p = dst;
    while (n--)
        *p++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
    }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n)
        dst[i++] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : 0;
}

/* ------------------------------------------------------------------ */
/* serial                                                              */
/* ------------------------------------------------------------------ */

#define COM1 0x3f8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

void serial_putc(char c) {
    for (int spin = 0; spin < 100000; spin++) {
        if (inb(COM1 + 5) & 0x20)
            break;
    }
    outb(COM1, (uint8_t)c);
}

/* ------------------------------------------------------------------ */
/* console                                                             */
/* ------------------------------------------------------------------ */

static CHAR16 utf16_buf[512];

static void uefi_write(const char *s) {
    if (!g_uefi_console || !gST || !gST->ConOut)
        return;
    size_t i = 0;
    while (*s && i < (sizeof(utf16_buf) / sizeof(utf16_buf[0])) - 1) {
        utf16_buf[i++] = (CHAR16)(unsigned char)*s++;
    }
    utf16_buf[i] = 0;
    gST->ConOut->OutputString(gST->ConOut, utf16_buf);
}

void console_use_uefi(int enabled) {
    g_uefi_console = enabled;
}

void console_putc(char c) {
    serial_putc(c);
    if (g_uefi_console) {
        char tmp[2] = {c, 0};
        uefi_write(tmp);
    }
}

void console_puts(const char *s) {
    const char *p = s;
    while (*p)
        serial_putc(*p++);
    uefi_write(s);
}

/* ------------------------------------------------------------------ */
/* formatting                                                          */
/* ------------------------------------------------------------------ */

static void utoa(unsigned long long value, unsigned base, int upper, char *out, size_t out_size) {
    static const char lower[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower;
    char tmp[32];
    size_t n = 0;

    if (value == 0) {
        if (out_size > 1) {
            out[0] = '0';
            out[1] = 0;
        }
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = digits[value % base];
        value /= base;
    }
    size_t i = 0;
    while (n && i + 1 < out_size)
        out[i++] = tmp[--n];
    out[i] = 0;
}

static void format_string(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t oi = 0;
#define PUT(ch)                                                                                    \
    do {                                                                                           \
        if (oi + 1 < cap)                                                                          \
            out[oi++] = (ch);                                                                      \
    } while (0)

    while (*fmt && oi + 1 < cap) {
        if (*fmt != '%') {
            PUT(*fmt++);
            continue;
        }
        fmt++;
        int zero = 0, width = 0, left = 0;
        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0') {
            zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        int is_long = 0;
        if (*fmt == 'l') {
            is_long++;
            fmt++;
            if (*fmt == 'l') {
                is_long++;
                fmt++;
            }
        }
        char tmp[64];
        const char *str = 0;

        switch (*fmt) {
        case 'c': {
            tmp[0] = (char)va_arg(ap, int);
            tmp[1] = 0;
            str = tmp;
            break;
        }
        case 's':
            str = va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            break;
        case 'd':
        case 'i': {
            long long v = is_long ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            unsigned long long mag =
                v < 0 ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
            utoa(mag, 10, 0, tmp + 1, sizeof(tmp) - 1);
            if (v < 0) {
                tmp[0] = '-';
                str = tmp;
            } else {
                memmove(tmp, tmp + 1, strlen(tmp + 1) + 1);
                str = tmp;
            }
            break;
        }
        case 'u': {
            unsigned long long v = is_long ? va_arg(ap, unsigned long long)
                                           : (unsigned long long)va_arg(ap, unsigned int);
            utoa(v, 10, 0, tmp, sizeof(tmp));
            str = tmp;
            break;
        }
        case 'x':
        case 'X': {
            unsigned long long v = is_long ? va_arg(ap, unsigned long long)
                                           : (unsigned long long)va_arg(ap, unsigned int);
            utoa(v, 16, *fmt == 'X', tmp, sizeof(tmp));
            str = tmp;
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)va_arg(ap, void *);
            utoa(v, 16, 0, tmp, sizeof(tmp));
            static char pbuf[66];
            pbuf[0] = '0';
            pbuf[1] = 'x';
            strcpy(pbuf + 2, tmp);
            str = pbuf;
            break;
        }
        case '%':
            tmp[0] = '%';
            tmp[1] = 0;
            str = tmp;
            break;
        default:
            tmp[0] = '%';
            tmp[1] = 0;
            str = tmp;
            if (*fmt)
                fmt--;
            break;
        }
        if (*fmt)
            fmt++;

        size_t len = strlen(str);
        size_t pad = (width > (int)len) ? (size_t)width - len : 0;
        if (!left) {
            for (size_t i = 0; i < pad; i++)
                PUT(zero ? '0' : ' ');
        }
        for (size_t i = 0; i < len; i++)
            PUT(str[i]);
        if (left) {
            for (size_t i = 0; i < pad; i++)
                PUT(' ');
        }
    }
    out[oi] = 0;
#undef PUT
}

void console_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    format_string(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_puts(buf);
}

void fail(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    format_string(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_puts("\n[loader] FATAL: ");
    console_puts(buf);
    console_puts("\n[loader] halted.\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

void *uefi_alloc_pool(size_t size) {
    VOID *p = 0;
    EFI_STATUS st = gBS->AllocatePool(EfiLoaderData, size, &p);
    if (EFI_ERROR(st) || !p)
        fail("AllocatePool(%zu) failed: %llx", size, (unsigned long long)st);
    return p;
}

uint64_t uefi_alloc_pages(size_t bytes, uint64_t max_address) {
    UINTN pages = (bytes + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS addr = max_address;
    EFI_STATUS st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &addr);
    if (EFI_ERROR(st)) {
        addr = 0;
        st = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
    }
    if (EFI_ERROR(st) || addr == 0)
        fail("AllocatePages(%zu) failed: %llx", bytes, (unsigned long long)st);
    memset((void *)(uintptr_t)addr, 0, pages * 4096);
    return addr;
}

void uefi_free_pages(uint64_t addr, size_t bytes) {
    gBS->FreePages(addr, (bytes + 4095) / 4096);
}

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */

void config_defaults(struct boot_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->kernel, "\\EFI\\BOOT\\KERNEL.ELF");
    strcpy(cfg->initramfs, "\\EFI\\BOOT\\INITRAMFS.CPIO");
    strcpy(cfg->cmdline, "console=serial,fb");
    cfg->video_width = 1280;
    cfg->video_height = 720;
}

static int parse_int(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

void config_parse(struct boot_config *cfg, const char *data, uint64_t size) {
    uint64_t i = 0;
    while (i < size) {
        char line[256];
        size_t n = 0;
        while (i < size && data[i] != '\n' && n + 1 < sizeof(line))
            line[n++] = data[i++];
        if (i < size && data[i] == '\n')
            i++;
        line[n] = 0;

        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r')
            p++;
        if (*p == 0 || *p == '#' || *p == ';')
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = p;
        char *val = eq + 1;
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        while (*val == ' ' || *val == '\t')
            val++;
        end = val + strlen(val);
        while (end > val && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
            *--end = 0;
        if (*val == '"' && end > val + 1 && end[-1] == '"') {
            val++;
            end[-1] = 0;
        }

        if (!strcmp(key, "kernel"))
            strncpy(cfg->kernel, val, sizeof(cfg->kernel) - 1);
        else if (!strcmp(key, "initramfs"))
            strncpy(cfg->initramfs, val, sizeof(cfg->initramfs) - 1);
        else if (!strcmp(key, "cmdline"))
            strncpy(cfg->cmdline, val, sizeof(cfg->cmdline) - 1);
        else if (!strcmp(key, "width"))
            cfg->video_width = parse_int(val);
        else if (!strcmp(key, "height"))
            cfg->video_height = parse_int(val);
    }
}
