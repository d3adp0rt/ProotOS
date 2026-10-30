#include "mm.h"
#include "kernel.h"

void *malloc(size_t n) { return kmalloc(n); }
void *calloc(size_t n, size_t s) { return kzalloc(n * s); }
void *realloc(void *p, size_t n) { return krealloc(p, n); }
void free(void *p) { kfree(p); }

int errno;

int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = kvsnprintf(out, cap, fmt, ap);
    va_end(ap); return r;
}
int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    return kvsnprintf(out, cap, fmt, ap);
}
int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[512];
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kprintf("%s", buf);
    return (int)strlen(buf);
}

long time(long *t) { if (t) *t = 0; return 0; }
struct tm { int tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec; };
struct tm *gmtime(const long *t) { (void)t; static struct tm tm; return &tm; }
struct tm *localtime(const long *t) { return gmtime(t); }
size_t strftime(char *s, size_t n, const char *f, const struct tm *tm) { (void)f;(void)tm; if(n)s[0]=0; return 0; }

void __assert_fail(const char *expr, const char *file, unsigned line, const char *func) {
    panic("assert %s at %s:%u (%s)", expr, file, line, func);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    uint64_t x = 0;
    for (size_t i = 0; i < len; i++) {
        if ((i & 7) == 0) {
            uint32_t lo, hi;
            __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
            x = ((uint64_t)hi << 32) | lo;
        }
        output[i] = (unsigned char)(x >> ((i & 7) * 8));
    }
    if (olen) *olen = len;
    return 0;
}

unsigned __int128 __udivti3(unsigned __int128 n, unsigned __int128 d) {
    unsigned __int128 q = 0, r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= ((unsigned __int128)1 << i); }
    }
    return q;
}
unsigned __int128 __umodti3(unsigned __int128 n, unsigned __int128 d) {
    unsigned __int128 r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) r -= d;
    }
    return r;
}
__int128 __divti3(__int128 n, __int128 d) {
    int neg = (n < 0) ^ (d < 0);
    unsigned __int128 un = n < 0 ? (unsigned __int128)(-n) : (unsigned __int128)n;
    unsigned __int128 ud = d < 0 ? (unsigned __int128)(-d) : (unsigned __int128)d;
    unsigned __int128 q = __udivti3(un, ud);
    return neg ? -(__int128)q : (__int128)q;
}
