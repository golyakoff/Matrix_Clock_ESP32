#ifndef __TIME_HELPER_H__
#define __TIME_HELPER_H__

#include "time.h"
#include "stdbool.h"

bool times_are_different(const struct tm* dt1, const struct tm* dt2);
void println_tm(const char *prefix, const struct tm *dt);

#endif // __TIME_HELPER_H__

