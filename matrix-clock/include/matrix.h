#ifndef __MATRIX_H__
#define __MATRIX_H__

#include "PxMatrix.h"

#define P_LAT   (22)
#define P_A     (19)
#define P_B     (23)
#define P_C     (18)
#define P_D      (5)
#define P_E     (15)
#define P_OE    (25)

#define COLOR_ORDER RRBBGG

// Initial brigtness for the welcome and "powered by" screens 7..255
#define INIT_BRIGHTNESS   (255U) 

// If this is set to false, the number will only change if the value behind it changes
// e.g. the digit representing the least significant minute will be replaced every minute,
// but the most significant number will only be replaced every 10 minutes.
// When true, all digits will be replaced every minute.
#define FORCE_UPDATE_ALL_DIGITS true

// Comment for using constant brightness INIT_BRIGHTNESS
#define ADJUST_BRIGHTNESS
#ifdef ADJUST_BRIGHTNESS
#define VARISTOR_PIN        (34)
#define ADC_SCALE         (4095)
#define PWM_MIN_VALUE       (40)
#define PWM_MAX_VALUE      (220)
#endif //ADJUST_BRIGHTNESS

void matrix_init(struct tm *init_dt);
void matrix_1hz_isr_loop();
void matrix_100hz_loop();
void matrix_update_dt(const struct tm *new_dt, bool force_update_display);
void matrix_get_time(struct tm *dt_out);

#endif // __MATRIX_H__
