#ifndef __RTC_H__
#define __RTC_H__

#include <time.h>

#define RTC_SPEED   (400000U)
#define RTC_SCL     (26U)
#define RTC_SDA     (27U)

#define RTC_SQW     (35U)

bool rtc_init();
bool rtc_get_time(struct tm *dt_out);
bool rtc_set_time(const struct tm *dt_new);
bool rtc_get_temperature(int8_t *temperature, uint8_t *fraction);

#endif // __RTC_H__
