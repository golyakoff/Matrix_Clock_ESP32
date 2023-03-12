#ifndef __MATRIX_H__
#define __MATRIX_H__

#define P_LAT   (22)
#define P_A     (19)
#define P_B     (23)
#define P_C     (18)
#define P_D      (5)
#define P_E     (15)
#define P_OE    (25)

#define ADJUST_BRIGHTNESS
#ifdef ADJUST_BRIGHTNESS

#define VARISTOR_PIN    (34)
#define ADC_SCALE     (4095)
#define PWM_MIN_VALUE   (40)
#define PWM_MAX_VALUE  (220)
#endif //ADJUST_BRIGHTNESS

void matrix_init();
void matrix_loop_every_second();

#endif // __MATRIX_H__
