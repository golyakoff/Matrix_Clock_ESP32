// Host test of time_helper.cpp: calendar arithmetic of the clock's local time must not depend on
// the time zone (TZ is set on the clock for the time from the internet)
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "time_helper.h"

static struct tm fields(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    return t;
}

static void expect(const struct tm *t, int y, int mo, int d, int h, int mi, int s, int wday)
{
    assert(t->tm_year == y - 1900 && t->tm_mon == mo - 1 && t->tm_mday == d);
    assert(t->tm_hour == h && t->tm_min == mi && t->tm_sec == s);
    assert(t->tm_wday == wday);
    assert(t->tm_isdst == 0);
}

int main(void)
{
    // a zone with daylight saving time and a non-zero offset: mktime() would shift both ways
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    struct tm t = fields(1970, 1, 1, 0, 0, 0);
    assert(tm_to_seconds(&t) == 0);
    t = fields(2026, 10, 22, 13, 31, 24);
    assert(tm_to_seconds(&t) == timegm(&t));

    // the same as timegm() over 1970..2105 (the u32 of the Time characteristic), a pseudo-random walk
    srand(1);
    for (int i = 0; i < 200000; i++)
    {
        time_t v = (time_t)(((uint64_t)rand() << 16 ^ (uint64_t)rand()) % 0x100000000ULL);
        struct tm g;
        gmtime_r(&v, &g);
        assert(tm_to_seconds(&g) == v);
    }

    // the 1 Hz tick: 61 seconds carry over minute, hour, day, month and year
    t = fields(2026, 12, 31, 23, 59, 60);
    tm_normalize(&t);
    expect(&t, 2027, 1, 1, 0, 0, 0, 5);
    t = fields(2028, 2, 28, 23, 59, 60);
    tm_normalize(&t);
    expect(&t, 2028, 2, 29, 0, 0, 0, 2);
    t = fields(2100, 2, 28, 23, 59, 60);
    tm_normalize(&t);
    expect(&t, 2100, 3, 1, 0, 0, 0, 1);

    // summer time in the zone: the clock's hour stays (mktime() would make 13:00 of 12:00 isdst=0)
    t = fields(2026, 7, 15, 11, 59, 60);
    tm_normalize(&t);
    expect(&t, 2026, 7, 15, 12, 0, 0, 3);
    // the hours of the zone's transitions are ordinary hours
    t = fields(2026, 3, 29, 1, 59, 60);
    tm_normalize(&t);
    expect(&t, 2026, 3, 29, 2, 0, 0, 0);
    t = fields(2026, 10, 25, 2, 59, 60);
    tm_normalize(&t);
    expect(&t, 2026, 10, 25, 3, 0, 0, 0);

    // fields below their ranges
    t = fields(2026, 1, 1, 0, 0, -1);
    tm_normalize(&t);
    expect(&t, 2025, 12, 31, 23, 59, 59, 3);
    t = fields(2026, 1, 1, 0, 0, 0);
    t.tm_mon = -1;
    tm_normalize(&t);
    expect(&t, 2025, 12, 1, 0, 0, 0, 1);

    printf("test_time_helper: OK\n");
    return 0;
}
