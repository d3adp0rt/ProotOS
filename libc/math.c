#include <math.h>
#include <stdint.h>

#define LN2 0.69314718055994530942
#define LN10 2.30258509299404568402
#define PI 3.14159265358979323846

typedef union {
    double d;
    uint64_t u;
} dbits;

double fabs(double x) {
    dbits b;
    b.d = x;
    b.u &= 0x7fffffffffffffffULL;
    return b.d;
}

double trunc(double x) {
    if (x != x || fabs(x) >= 9.0e18)
        return x;
    long long i = (long long)x;
    return (double)i;
}

double floor(double x) {
    double t = trunc(x);
    if (x < 0 && t != x)
        t -= 1.0;
    return t;
}

double ceil(double x) {
    double t = trunc(x);
    if (x > 0 && t != x)
        t += 1.0;
    return t;
}

double round(double x) {
    return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5);
}

double sqrt(double x) {
    if (x < 0)
        return NAN;
    if (x == 0 || x != x)
        return x;
    dbits b;
    b.d = x;
    b.u = (b.u >> 1) + 0x1ff8000000000000ULL;
    double r = b.d;
    for (int i = 0; i < 12; i++)
        r = 0.5 * (r + x / r);
    return r;
}

double fmod(double x, double y) {
    if (y == 0 || x != x || y != y)
        return NAN;
    double q = trunc(x / y);
    double r = x - q * y;
    if (fabs(r) >= fabs(y))
        r = r - (r > 0 ? fabs(y) : -fabs(y));
    return r;
}

double ldexp(double x, int e) {
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x /= 2.0; e++; }
    return x;
}

double frexp(double x, int *e) {
    if (x == 0 || x != x) {
        if (e) *e = 0;
        return x;
    }
    int ex = 0;
    double a = fabs(x);
    while (a >= 1.0) { a /= 2.0; ex++; }
    while (a < 0.5) { a *= 2.0; ex--; }
    if (e) *e = ex;
    return x < 0 ? -a : a;
}

double modf(double x, double *ip) {
    double t = trunc(x);
    if (ip) *ip = t;
    return x - t;
}

double exp(double x) {
    if (x != x)
        return x;
    if (x > 700)
        return INFINITY;
    if (x < -745)
        return 0;
    int k = (int)round(x / LN2);
    double r = x - k * LN2;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 20; i++) {
        term *= r / i;
        sum += term;
    }
    return ldexp(sum, k);
}

double log(double x) {
    if (x < 0)
        return NAN;
    if (x == 0)
        return -INFINITY;
    int e = 0;
    double m = frexp(x, &e);
    if (m < 0.70710678118654752440) {
        m *= 2.0;
        e--;
    }
    double z = (m - 1.0) / (m + 1.0);
    double z2 = z * z;
    double term = z, sum = 0.0;
    for (int i = 1; i < 40; i += 2) {
        sum += term / i;
        term *= z2;
    }
    return 2.0 * sum + e * LN2;
}

double log10(double x) { return log(x) / LN10; }
double log2(double x) { return log(x) / LN2; }

double pow(double x, double y) {
    if (y == 0)
        return 1.0;
    if (x == 0)
        return y > 0 ? 0.0 : INFINITY;
    if (x < 0) {
        long long iy = (long long)y;
        if ((double)iy == y) {
            double r = pow(-x, y);
            return (iy & 1) ? -r : r;
        }
        return NAN;
    }
    if ((double)(long long)y == y && fabs(y) < 64) {
        long long n = (long long)y;
        int neg = n < 0;
        if (neg) n = -n;
        double r = 1.0, base = x;
        while (n) {
            if (n & 1) r *= base;
            base *= base;
            n >>= 1;
        }
        return neg ? 1.0 / r : r;
    }
    return exp(y * log(x));
}

static double reduce_pi(double x) {
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    return x;
}

double sin(double x) {
    x = reduce_pi(x);
    double term = x, sum = x, x2 = x * x;
    for (int i = 3, s = -1; i < 30; i += 2, s = -s) {
        term *= x2 / ((i - 1) * i);
        sum += s * term;
    }
    return sum;
}

double cos(double x) {
    x = reduce_pi(x);
    double term = 1.0, sum = 1.0, x2 = x * x;
    for (int i = 2, s = -1; i < 30; i += 2, s = -s) {
        term *= x2 / ((i - 1) * i);
        sum += s * term;
    }
    return sum;
}

double tan(double x) { return sin(x) / cos(x); }

double atan(double x) {
    int neg = x < 0;
    if (neg) x = -x;
    int inv = 0;
    if (x > 1) { x = 1 / x; inv = 1; }
    double term = x, sum = x, x2 = x * x;
    for (int i = 3, s = -1; i < 60; i += 2, s = -s) {
        term *= x2;
        sum += s * term / i;
    }
    if (inv) sum = PI / 2 - sum;
    return neg ? -sum : sum;
}

double atan2(double y, double x) {
    if (x > 0) return atan(y / x);
    if (x < 0) return y >= 0 ? atan(y / x) + PI : atan(y / x) - PI;
    if (y > 0) return PI / 2;
    if (y < 0) return -PI / 2;
    return 0;
}

double asin(double x) { return atan2(x, sqrt(1 - x * x)); }
double acos(double x) { return atan2(sqrt(1 - x * x), x); }

double sinh(double x) { double e = exp(x); return (e - 1 / e) / 2; }
double cosh(double x) { double e = exp(x); return (e + 1 / e) / 2; }
double tanh(double x) { double e = exp(2 * x); return (e - 1) / (e + 1); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
