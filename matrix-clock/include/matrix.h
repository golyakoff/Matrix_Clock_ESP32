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

// Comment for using constant brightness INIT_BRIGHTNESS
#define ADJUST_BRIGHTNESS

#ifdef ADJUST_BRIGHTNESS
#define VARISTOR_PIN        (34)
#define ADC_SCALE         (4095)
#define PWM_MIN_VALUE       (40)
#define PWM_MAX_VALUE      (220)
#endif //ADJUST_BRIGHTNESS

void matrix_init(struct tm initDateTime);
void matrix_1hz_isr_loop();
void matrix_100hz_loop();
void matrix_sync_dt(struct tm initDateTime);

#endif // __MATRIX_H__
