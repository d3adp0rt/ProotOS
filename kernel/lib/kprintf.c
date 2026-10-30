/*
 * ProotOS — kernel formatted output and panic.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include <stdarg.h>


static void utoa(unsigned long long value, unsigned base, int upper, char *out, size_t cap) {
    static const char lower[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower;
    char tmp[32];
    size_t n = 0;
    if (value == 0) {
        if (cap > 1) {
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
    while (n && i + 1 < cap)
        out[i++] = tmp[--n];
    out[i] = 0;
}

static void format(char *out, size_t cap, const char *fmt, va_list ap) {
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
        int zero = 0, width = 0, left = 0, prec = -1;
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
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }
        int is_long = 0;
        if (*fmt == 'z') {
            is_long = 2;
            fmt++;
        } else if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            }
        }
        char tmp[64];
        const char *str = tmp;

        switch (*fmt) {
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            tmp[1] = 0;
            break;
        case 's':
            str = va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            break;
        case 'd':
        case 'i': {
            long long v = (is_long == 2) ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            unsigned long long mag =
                v < 0 ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
            utoa(mag, 10, 0, tmp + 1, sizeof(tmp) - 1);
            if (v < 0) {
                tmp[0] = '-';
            } else {
                memmove(tmp, tmp + 1, strlen(tmp + 1) + 1);
            }
            break;
        }
        case 'u': {
            unsigned long long v = (is_long == 2) ? va_arg(ap, unsigned long long)
                                                  : (unsigned long long)va_arg(ap, unsigned int);
            utoa(v, 10, 0, tmp, sizeof(tmp));
            break;
        }
        case 'x':
        case 'X': {
            unsigned long long v = (is_long == 2) ? va_arg(ap, unsigned long long)
                                                  : (unsigned long long)va_arg(ap, unsigned int);
            utoa(v, 16, *fmt == 'X', tmp, sizeof(tmp));
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)va_arg(ap, void *);
            static char pbuf[66];
            pbuf[0] = '0';
            pbuf[1] = 'x';
            utoa(v, 16, 0, pbuf + 2, sizeof(pbuf) - 2);
            str = pbuf;
            break;
        }
        case '%':
            tmp[0] = '%';
            tmp[1] = 0;
            break;
        default:
            tmp[0] = '%';
            tmp[1] = 0;
            if (*fmt)
                fmt--;
            break;
        }
        if (*fmt)
            fmt++;

        size_t len = strlen(str);
        if (prec >= 0 && len > (size_t)prec)
            len = (size_t)prec;
        size_t pad = (width > (int)len) ? (size_t)width - len : 0;
        if (!left)
            for (size_t i = 0; i < pad; i++)
                PUT(zero ? '0' : ' ');
        for (size_t i = 0; i < len; i++)
            PUT(str[i]);
        if (left)
            for (size_t i = 0; i < pad; i++)
                PUT(' ');
    }
    out[oi] = 0;
#undef PUT
}

static void kvprintf(const char *fmt, va_list ap) {
    char buf[1024];
    format(buf, sizeof(buf), fmt, ap);
    console_write(buf);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

int kvsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    format(out, cap, fmt, ap);
    return (int)strlen(out);
}

int ksnprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    format(out, cap, fmt, ap);
    va_end(ap);
    return (int)strlen(out);
}

void panic(const char *fmt, ...) {
    console_write("\n*** KERNEL PANIC ***\n");
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    console_write("\nSystem halted.\n");
    cpu_halt();
}
