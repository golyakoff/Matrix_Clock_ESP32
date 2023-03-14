#ifndef __RTC_H__
#define __RTC_H__

#include <time.h>

#define RTC_SPEED   (400000U)
#define RTC_SCL     (26U)
#define RTC_SDA     (27U)

#define RTC_SQW     (35U)

bool rtc_init();
bool rtc_getDateTime(struct tm *dt);
bool rtc_setDateTime(const struct tm *dt);
bool rtc_getTemperature(int8_t *temperature, uint8_t *fraction);

#endif // __RTC_H__
