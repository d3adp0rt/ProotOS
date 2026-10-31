#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static size_t fmt_uint(char *buf, unsigned long long v, unsigned base, int upper) {
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0;
    if (!v) {
        buf[0] = '0';
        return 1;
    }
    while (v) {
        tmp[n++] = dig[v % base];
        v /= base;
    }
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    return (size_t)n;
}

static double pow10i(int n) {
    double v = 1.0;
    while (n-- > 0)
        v *= 10.0;
    return v;
}

static int dbl_to_fixed(char *out, double v, int prec) {
    int n = 0;
    if (v != v) {
        memcpy(out, "nan", 3);
        return 3;
    }
    if (v > 1.7976931348623157e308) {
        memcpy(out, "inf", 3);
        return 3;
    }
    if (v < 0) {
        out[n++] = '-';
        v = -v;
    }
    if (v > 1.0e18)
        v = 1.0e18; /* fixed form fallback */
    unsigned long long ip = (unsigned long long)v;
    double frac = v - (double)ip;
    double scale = pow10i(prec);
    unsigned long long fp = (unsigned long long)(frac * scale + 0.5);
    if (fp >= (unsigned long long)scale && scale >= 1.0) {
        ip++;
        fp = 0;
    }
    n += (int)fmt_uint(out + n, ip, 10, 0);
    if (prec > 0) {
        out[n++] = '.';
        char digits[24];
        for (int i = prec - 1; i >= 0; i--) {
            digits[i] = (char)('0' + (fp % 10));
            fp /= 10;
        }
        for (int i = 0; i < prec; i++)
            out[n++] = digits[i];
    }
    return n;
}

static int dbl_to_exp(char *out, double v, int prec) {
    int n = 0;
    if (v != v) {
        memcpy(out, "nan", 3);
        return 3;
    }
    if (v > 1.7976931348623157e308) {
        memcpy(out, "inf", 3);
        return 3;
    }
    if (v < 0) {
        out[n++] = '-';
        v = -v;
    }
    int exp = 0;
    if (v != 0) {
        while (v >= 10.0) { v /= 10.0; exp++; }
        while (v < 1.0) { v *= 10.0; exp--; }
    }
    double scale = pow10i(prec);
    double mant = v * scale + 0.5;
    unsigned long long mi = (unsigned long long)mant;
    if (mi >= (unsigned long long)(10 * scale)) {
        mi /= 10;
        exp++;
    }
    char md[24];
    int mn = 0;
    for (int i = prec; i >= 0; i--) {
        md[i] = (char)('0' + (mi % 10));
        mi /= 10;
    }
    md[prec + 1] = 0;
    mn = prec + 1;
    out[n++] = md[0];
    if (prec > 0) {
        out[n++] = '.';
        for (int i = 1; i < mn; i++)
            out[n++] = md[i];
    }
    out[n++] = 'e';
    if (exp < 0) { out[n++] = '-'; exp = -exp; }
    else out[n++] = '+';
    char eb[8];
    size_t en = fmt_uint(eb, (unsigned long long)exp, 10, 0);
    if (en < 2) out[n++] = '0';
    for (size_t i = 0; i < en; i++)
        out[n++] = eb[i];
    return n;
}

static int dbl_to_g(char *out, double v, int prec, int alt) {
    if (prec <= 0) prec = 1;
    int neg = 0;
    double a = v;
    if (a < 0) { neg = 1; a = -a; }
    int exp = 0;
    if (a != 0) {
        while (a >= 10.0) { a /= 10.0; exp++; }
        while (a < 1.0) { a *= 10.0; exp--; }
    }
    char tmp[64];
    int n;
    if (exp < -4 || exp >= prec)
        n = dbl_to_exp(tmp, v, prec - 1);
    else
        n = dbl_to_fixed(tmp, v, prec - 1 - exp);
    if (!alt) {
        char *dot = 0;
        for (int i = 0; i < n; i++)
            if (tmp[i] == '.') dot = tmp + i;
        if (dot) {
            int e = 0;
            while (e < n && tmp[e] != 'e' && tmp[e] != 'E') e++;
            int end = n;
            if (e < n) end = e;
            int j = end - 1;
            while (j > (int)(dot - tmp) && tmp[j] == '0') j--;
            if (j == (int)(dot - tmp)) j--;
            int k = j + 1;
            for (int i = end; i < n; i++)
                tmp[k++] = tmp[i];
            n = k;
        }
    }
    for (int i = 0; i < n; i++)
        out[i] = tmp[i];
    (void)neg;
    return n;
}

int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t o = 0;
#define PUTCH(c) do { if (o + 1 < cap) out[o] = (char)(c); o++; } while (0)
#define PUTS(s, n) do { for (size_t _i = 0; _i < (size_t)(n); _i++) PUTCH((s)[_i]); } while (0)
    while (*fmt) {
        if (*fmt != '%') {
            PUTCH(*fmt++);
            continue;
        }
        fmt++;
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else if (*fmt == '+') { plus = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else if (*fmt == '#') { alt = 1; fmt++; }
            else break;
        }
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        int len = 0;
        while (*fmt == 'l') { len++; fmt++; }
        if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') { len = 2; fmt++; }
        else if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') fmt++;
            len = -1;
        }
        char conv = *fmt ? *fmt++ : 0;
        char buf[80];
        int n = 0;
        char sign = 0;
        if (conv == 'd' || conv == 'i') {
            long long v = (len >= 2) ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            unsigned long long u = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
            if (v < 0) sign = '-';
            else if (plus) sign = '+';
            else if (space) sign = ' ';
            n = (int)fmt_uint(buf, u, 10, 0);
        } else if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            unsigned long long u = (len >= 2) ? va_arg(ap, unsigned long long)
                                              : (unsigned long long)va_arg(ap, unsigned int);
            n = (int)fmt_uint(buf, u, conv == 'o' ? 8 : (conv == 'u' ? 10 : 16), conv == 'X');
        } else if (conv == 'p') {
            unsigned long long u = (unsigned long long)(uintptr_t)va_arg(ap, void *);
            buf[n++] = '0'; buf[n++] = 'x';
            n += (int)fmt_uint(buf + n, u, 16, 0);
        } else if (conv == 'c') {
            buf[n++] = (char)va_arg(ap, int);
        } else if (conv == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t sl = 0;
            while (s[sl] && (prec < 0 || (int)sl < prec)) sl++;
            int pad = width > (int)sl ? width - (int)sl : 0;
            if (!left) for (int i = 0; i < pad; i++) PUTCH(' ');
            PUTS(s, sl);
            if (left) for (int i = 0; i < pad; i++) PUTCH(' ');
            continue;
        } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' || conv == 'g' ||
                   conv == 'G') {
            double v = va_arg(ap, double);
            int p = prec < 0 ? 6 : prec;
            if (conv == 'f' || conv == 'F') n = dbl_to_fixed(buf, v, p);
            else if (conv == 'e' || conv == 'E') n = dbl_to_exp(buf, v, p);
            else n = dbl_to_g(buf, v, p, alt);
            if ((conv == 'E' || conv == 'G')) {
                for (int i = 0; i < n; i++)
                    if (buf[i] == 'e') buf[i] = 'E';
            }
            sign = 0;
        } else if (conv == '%') {
            buf[n++] = '%';
        } else {
            buf[n++] = '%';
            if (conv) buf[n++] = conv;
        }
        int body = n + (sign ? 1 : 0);
        int pad = width > body ? width - body : 0;
        if (!left && !zero) for (int i = 0; i < pad; i++) PUTCH(' ');
        if (sign) PUTCH(sign);
        if (!left && zero) for (int i = 0; i < pad; i++) PUTCH('0');
        PUTS(buf, n);
        if (left) for (int i = 0; i < pad; i++) PUTCH(' ');
    }
    if (cap)
        out[o < cap ? o : cap - 1] = 0;
    return (int)o;
#undef PUTCH
#undef PUTS
}

int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}
