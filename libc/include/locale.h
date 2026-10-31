#pragma once
struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
};
struct lconv *localeconv(void);
char *setlocale(int category, const char *locale);
#define LC_ALL 6
