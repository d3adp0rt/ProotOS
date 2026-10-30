#include "libc.h"

long syscall0(long n) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}
long syscall1(long n, long a) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a) : "memory");
    return ret;
}
long syscall2(long n, long a, long b) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b) : "memory");
    return ret;
}
long syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return ret;
}
long syscall4(long n, long a, long b, long c, long d) {
    register long r10 __asm__("r10") = d;
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "memory");
    return ret;
}

long write(int fd, const void *buf, size_t len) {
    return syscall3(1, fd, (long)buf, (long)len);
}
long read(int fd, void *buf, size_t len) {
    return syscall3(0, fd, (long)buf, (long)len);
}
int open(const char *path, int flags, int mode) {
    return (int)syscall3(6, (long)path, flags, mode);
}
int close(int fd) {
    return (int)syscall1(3, fd);
}
int fork(void) {
    return (int)syscall0(57);
}
int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)syscall3(59, (long)path, (long)argv, (long)envp);
}
int wait4(int pid, int *status, int options, void *rusage) {
    return (int)syscall4(61, pid, (long)status, options, (long)rusage);
}
void _exit(int code) {
    syscall1(60, code);
    for (;;) {
    }
}
int getpid(void) {
    return (int)syscall0(2);
}
int sleep_ms(unsigned ms) {
    return (int)syscall1(4, ms);
}
int pipe(int fds[2]) {
    return (int)syscall1(22, (long)fds);
}
long lseek(int fd, long offset, int whence) {
    return syscall3(10, fd, offset, whence);
}
int mkdir(const char *path, int mode) {
    return (int)syscall2(8, (long)path, mode);
}
int unlink(const char *path) {
    return (int)syscall1(9, (long)path);
}
int stat(const char *path, struct stat *st) {
    return (int)syscall2(7, (long)path, (long)st);
}
int shutdown_os(void) {
    return (int)syscall0(169);
}
int reboot_os(void) {
    return (int)syscall0(170);
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
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) {
    }
    return r;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b) {
        while (n--)
            *a++ = *b++;
    } else {
        a += n;
        b += n;
        while (n--)
            *--a = *--b;
    }
    return d;
}
int atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

static void u64toa(unsigned long long v, unsigned base, char *out) {
    static const char *digits = "0123456789abcdef";
    char tmp[32];
    int n = 0;
    if (!v) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v) {
        tmp[n++] = digits[v % base];
        v /= base;
    }
    int i = 0;
    while (n)
        out[i++] = tmp[--n];
    out[i] = 0;
}

static int vprintf_fd(int fd, const char *fmt, va_list ap) {
    char out[512];
    size_t o = 0;
    while (*fmt && o + 1 < sizeof(out)) {
        if (*fmt != '%') {
            out[o++] = *fmt++;
            continue;
        }
        fmt++;
        int l = 0;
        if (*fmt == 'l') {
            l++;
            fmt++;
            if (*fmt == 'l') {
                l++;
                fmt++;
            }
        }
        char tmp[64];
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s && o + 1 < sizeof(out))
                out[o++] = *s++;
            fmt++;
        } else if (*fmt == 'c') {
            out[o++] = (char)va_arg(ap, int);
            fmt++;
        } else if (*fmt == 'd' || *fmt == 'i') {
            long v = l ? va_arg(ap, long) : (long)va_arg(ap, int);
            unsigned long long m = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
            u64toa(m, 10, tmp);
            if (v < 0 && o + 1 < sizeof(out))
                out[o++] = '-';
            char *p = tmp;
            while (*p && o + 1 < sizeof(out))
                out[o++] = *p++;
            fmt++;
        } else if (*fmt == 'u' || *fmt == 'x') {
            unsigned long long v =
                l ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned int);
            u64toa(v, *fmt == 'x' ? 16 : 10, tmp);
            char *p = tmp;
            while (*p && o + 1 < sizeof(out))
                out[o++] = *p++;
            fmt++;
        } else if (*fmt == '%') {
            out[o++] = '%';
            fmt++;
        } else {
            out[o++] = '%';
        }
    }
    out[o] = 0;
    write(fd, out, o);
    return (int)o;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf_fd(1, fmt, ap);
    va_end(ap);
    return r;
}
int puts(const char *s) {
    write(1, s, strlen(s));
    write(1, "\n", 1);
    return 0;
}
int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}
