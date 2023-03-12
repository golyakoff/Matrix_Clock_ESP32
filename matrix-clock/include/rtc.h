#ifndef __RTC_H__
#define __RTC_H__

#include <time.h>

#define I2C_SCL     (26U)
#define I2C_SDA     (27U)

#define I2C_SPEED   (400000U)

bool rtc_init();
bool rtc_getDateTime(struct tm *dt);
bool rtc_setDateTime(const struct tm *dt);
bool rtc_getTemperature(int8_t *temperature, uint8_t *fraction);

#endif // __RTC_H__
