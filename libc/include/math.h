#pragma once
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inf())
#define NAN (__builtin_nan(""))
#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536

double fabs(double x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double sqrt(double x);
double fmod(double x, double y);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *ip);
double fmin(double a, double b);
double nan(const char *tag);
double copysign(double x, double y);
double nearbyint(double x);
double rint(double x);
long lround(double x);
long lrint(double x);
#define isnan(x) ((x) != (x))
#define isinf(x) (!isnan(x) && fabs(x) > 1.7976931348623157e308)
#define isfinite(x) (!isnan(x) && !isinf(x))
#define signbit(x) ((x) < 0.0 || ((x) == 0.0 && 1.0 / (x) < 0.0))
double fmax(double a, double b);
