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
 * @brief Blanks the panel for the OTA data transfer: stops the display refresh timer (the panel
 *        goes dark) so the delicate firmware stream has the whole CPU/RF to itself, instead of
 *        keeping the "Updating firmware" screen lit. Call once the transfer proper begins. The
 *        panel is brought back only by matrix_show_ota_failed(); a successful update reboots.
 *        Ignored if matrix_enter_ota_mode() wasn't called first.
 */
void matrix_ota_freeze_display();

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

/**
 * @brief Sets the LED matrix pixel color order and applies it immediately.
 *        Used when update comes from BLE, or once at boot with the value loaded from RTC memory.
 *
 * @param use_rrbbgg true to drive the matrix with the RRBBGG pixel order, false for the default RRGGBB.
 */
void matrix_set_color_order(bool use_rrbbgg);

/**
 * @brief Gets the LED matrix pixel color order currently in use.
 *        Used when the actual value is requested by BLE.
 *
 * @return true if RRBBGG is in use, false if the default RRGGBB is in use.
 */
bool matrix_get_color_order();

/**
 * @brief Animation Splash mode: how often the animated screensaver replaces the clock over the
 *        :00 minute at the start of the hour (instead of the usual Tetris layout).
 */
typedef enum {
    animation_splash_off = 0,       // never
    animation_splash_daily = 1,     // once per day, at 00:00
    animation_splash_every_3h = 2,  // every 3 hours (00:00, 03:00, 06:00, ... 21:00)
    animation_splash_hourly = 3     // every hour, at HH:00
} animation_splash_mode_t;

/**
 * @brief Animation Splash playback duration: how long the animation loops before the clock returns.
 */
typedef enum {
    animation_splash_10s = 0,  // 10 seconds
    animation_splash_20s = 1,  // 20 seconds
    animation_splash_40s = 2,  // 40 seconds
    animation_splash_60s = 3   // 60 seconds (the whole :00 minute)
} animation_splash_duration_t;

/**
 * @brief Configures the "Animation Splash" feature: an animated screensaver played at the start of
 *        the hour instead of the usual Tetris layout, at the selected cadence and for the selected
 *        duration. Used when update comes from BLE, or once at boot with the value loaded from RTC.
 *
 * @param mode how often to play the animation (see animation_splash_mode_t, 0..3).
 * @param duration how long each run lasts (see animation_splash_duration_t, 0..3).
 * @param index the selected animation index (0..15); out-of-range values fall back to animation 0.
 */
void matrix_set_animation_splash(uint8_t mode, uint8_t duration, uint8_t index);

/**
 * @brief Gets the current "Animation Splash" configuration.
 *        Used when the actual value is requested by BLE.
 *
 * @param mode the pointer to the memory for storing the cadence mode (0..3).
 * @param duration the pointer to the memory for storing the playback duration (0..3).
 * @param index the pointer to the memory for storing the selected animation index (0..15).
 */
void matrix_get_animation_splash(uint8_t *mode, uint8_t *duration, uint8_t *index);

/**
 * @brief Requests an immediate one-off "play now" preview of the currently selected animation,
 *        independent of the time of day and the cadence mode (used by the app to test the animation).
 *        The request is served from matrix_100hz_loop() and ignored while the display is off,
 *        during OTA, or while an animation is already playing.
 */
void matrix_request_animation_splash_preview();

/**
 * @brief Returns how many Animation Splash animations this build ships (the size of the generated
 *        registry, ANIMATION_COUNT). The 16MB build may ship more than the 4MB build; the app reads
 *        this over BLE to show exactly the animations the connected clock actually has.
 */
uint8_t matrix_get_animation_count();

#endif // __MATRIX_H__
