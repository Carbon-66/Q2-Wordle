#ifndef _TIME_H
#define _TIME_H
#include "_q2.h"
struct timespec { time_t tv_sec; long tv_nsec; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int clock_gettime(int, struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
time_t time(time_t *);
/* glibc's, o32: nine ints, then the zone's offset and name */
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char *tm_zone; };
struct tm *localtime_r(const time_t *, struct tm *);
#endif
