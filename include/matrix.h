#ifndef __MATRIX_H__
#define __MATRIX_H__

#include "PxMatrix.h"

#define P_LAT   (22U)   /** @brief GPIO connected to LAT pin on the matrix */
#define P_A     (19U)   /** @brief GPIO connected to A pin on the matrix */
#define P_B     (23U)   /** @brief GPIO connected to B pin on the matrix */
#define P_C     (18U)   /** @brief GPIO connected to C pin on the matrix */
#define P_D      (5U)   /** @brief GPIO connected to D pin on the matrix */
#define P_E     (15U)   /** @brief GPIO connected to E pin on the matrix */
#define P_OE    (25U)   /** @brief GPIO connected to OE pin on the matrix */

/**
 * @brief Pixel order customizable for Matrix (from "PxMatrix.h")
 */
//#define COLOR_ORDER RRBBGG
#define COLOR_ORDER RRGGBB

/**
 * @brief Initial honest brightness for the welcome and "powered by" screens.
 *        Required range: 7..255.
 */
#define INIT_BRIGHTNESS (255U)

/**
 * @brief If this is set to false, the number will only change if the value behind it changes
 * e.g. the digit representing the least significant minute will be replaced every minute,
 * but the most significant number will only be replaced every 10 minutes.
 * When true, all digits will be replaced every minute.
 */
#define FORCE_UPDATE_ALL_DIGITS true

/**
 * @brief Light-dependent resistor GPIO pin (used for brightness adjustment).
 */
#define LDR_PIN (34U)

/**
 * @brief The the max value that can be read from ADC.
 *        For 8-bit ADC it will be 1023.
 *        For 10-bit ADC it will be 4095
 */
#define ADC_SCALE (4095)

#define PWM_MIN_VALUE  (40)
#define PWM_MAX_VALUE (220)

/**
 * @brief Initializes the matrix instance with the initial time structure.
 * 
 * @param init_dt is the initial time value to set in internal variable.
 *                It will be updated each second from ESR.
 */
void matrix_init(struct tm *init_dt);

/**
 * @brief Stops the display and animation timers, keeping the display instance intact.
 *        Their ISRs run code from flash, so they have to be stopped around any flash write
 *        (NVS, OTA), otherwise the ISR fires while the flash cache is disabled and the CPU panics
 *        with "Cache disabled but cached memory region accessed".
 *        The matrix stays dark until matrix_resume_timers() is called.
 */
void matrix_pause_timers();

/**
 * @brief Restarts the display and animation timers stopped by matrix_pause_timers().
 */
void matrix_resume_timers();

/**
 * @brief Switches the matrix from the clock to a static "Loading firmware" OTA screen.
 *        Stops the falling-digit animation timer, but keeps the display refresh timer running
 *        (temporarily speed up, see matrix.cpp) so the panel stays lit for the whole (potentially
 *        long) firmware transfer instead of going dark, the way matrix_unload() used to leave it.
 */
void matrix_enter_ota_mode();

/**
 * @brief Updates the percentage shown on the OTA screen, with one decimal digit (e.g. "37.5%").
 *        Only redraws when the displayed value actually changes, so this is safe to call on
 *        every received BLE chunk.
 *
 * @param percent OTA transfer progress, 0..100. Ignored if matrix_enter_ota_mode() wasn't called.
 */
void matrix_set_ota_progress(float percent);

/**
 * @brief Replaces the OTA progress screen with a static "Update failed" message, for when the
 *        transfer is aborted or fails validation. Ignored if matrix_enter_ota_mode() wasn't called.
 */
void matrix_show_ota_failed();

/**
 * @brief This methods should be called from ISR from RTC every second.
 *        It increments the internal time of the matrix by 1 second
 *        and inverts the "show colon" variable.
 */
void matrix_1hz_isr_loop();

/**
 * @brief This method should be called approximately 100 times per second.
 *        It checks whether the minutes has part has changed? 
 *        If yes it starts the time update animation on the matrix.
 */
void matrix_100hz_loop();

/**
 * @brief Sets the new internal time of the matrix (used when update comes from BLE).
 * 
 * @param new_dt structure containing the new time
 * @param force_update_display the value indicating whether we have to force the update the matrix with this new time? 
 */
void matrix_set_time(const struct tm *new_dt, bool force_update_display);

/**
 * @brief Gets the internal time of the matrix.
 *        Used in matrix_100hz_loop() for checking whether the second is passed?
 * 
 * @param dt_out pointer to the memory for storing the time structure.
 */
void matrix_get_time(struct tm *dt_out);

/**
 * @brief Sets the value indicating whether we have to show or hide the drawing of the time on the matrix.
 *        Used when On/Off command comes from BLE.
 * @param show the value indicating whether we have to switch matrix On (true) or Off (false)?
 */
void matrix_set_show(bool show);

/**
 * @brief Gets the value indicating whether we are showing or hiding now the drawing of the time on the matrix.
 *        Used when the actual On/Off state is requested by BLE.
 * 
 * @return true if the matrix is in "On" state, otherwise false.
 */
bool matrix_get_show();

/**
 * @brief Sets the value indicating whether we have to use auto-adjusted vale of the brightness
 *        based on the ADC value came from photo resistor or not?
 *        Used when update comes from BLE.
 * 
 * @param use_auto_brightness true for using auto adjustment, false for using manual brightness value instead.
 */
void matrix_set_auto_brightness(bool use_auto_brightness);

/**
 * @brief Gets the value indicating whether we have to use auto-adjusted vale of the brightness
 *        based on the ADC value came from photo resistor or not?
 *        Used when the actual state of auto adjustment is requested by BLE.
 * 
 * @return true if auto adjustment is used, otherwise false
 */
bool matrix_get_auto_brightness();

/**
 * @brief Sets the value of the manual brightness nibble (0..15)
 *        (this value is used when auto brightness adjustment is false)
 *        Method used when the updated manual brightness value comes from BLE.
 * 
 * @param manual_brightness_nibble the 4-bits unsigned integer value 
 *        of the brightness in range 0..15.
 */
void matrix_set_manual_brightness(uint8_t manual_brightness_nibble);

/**
 * @brief Gets the value of the manual brightness nibble (0..15).
 *        Used when the actual value of the manual brightness is requested by BLE.
 * 
 * @return the integer value in range 0..15
 */
uint8_t matrix_get_manual_brightness( );

#endif // __MATRIX_H__
