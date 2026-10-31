#pragma once
typedef long time_t;
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int clock_gettime(int clk, struct timespec *ts);
int nanosleep(const struct timespec *req, struct timespec *rem);
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L
time_t time(time_t *t);
clock_t clock(void);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
double difftime(time_t a, time_t b);
