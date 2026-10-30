#pragma once
typedef long time_t;
struct tm { int tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec; };
time_t time(time_t *);
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
size_t strftime(char *, size_t, const char *, const struct tm *);
