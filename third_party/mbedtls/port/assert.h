#pragma once
void __assert_fail(const char *, const char *, unsigned, const char *);
#define assert(x) ((x) ? (void)0 : __assert_fail(#x, __FILE__, __LINE__, __func__))
