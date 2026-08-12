#include <Arduino.h>
#include <PxMatrix.h>
#include <TetrisMatrixDraw.h>
#include <esp_log.h>

#include "time_helper.h"
#include "brightness_schedule.h"
#include "matrix.h"
#include "animations.h" // Day Splash animation registry (include from this TU only)

static const char* TAG = "MATRIX";

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t *timer = NULL;
hw_timer_t *animationTimer = NULL;

PxMATRIX display(64, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D);
TetrisMatrixDraw tetris(display);

static uint8_t tim_100hz;

static uint8_t _brightness = INIT_BRIGHTNESS;
static uint8_t _brightness_last = 0;

static uint8_t _manual_brightness_nibble = INIT_BRIGHTNESS;
static bool _use_auto_brightness = false;
static bool _show = true;

static bool _use_rrbbgg = false; // false = RRGGBB (previous hardcoded default), true = RRBBGG

static bool _show_colon = true;
static volatile bool _finished_animating = false;
static bool _display_intro = true;

static char _matrix_time_buffer[6]; // the buffer of the string containing time, like "12:34\n"
static struct tm _dt;               // time to display on the matrix
static bool _force_update;          // if "true", there will be performed forced update of the time

static bool _ota_mode = false;      // true while showing the OTA progress screen instead of the clock
static int16_t _ota_percent_last = -1; // last percentage drawn, in tenths of a percent; -1 = not drawn yet
static bool _ota_display_frozen = false; // true while the panel is blanked for the OTA data transfer

// Animation Splash: an animated GIF played at the start of the hour instead of the usual Tetris
// layout, at a configurable cadence and for a configurable duration (see the state machine below).
static uint8_t _animation_splash_mode = animation_splash_off;  // cadence, 0..3 (persisted in RTC memory)
static uint8_t _animation_splash_duration = animation_splash_10s; // duration, 0..3 (persisted in RTC memory)
static uint8_t _animation_splash_index = 0;      // selected animation index (persisted in RTC memory)
static bool _splash_mode = false;                // true while an animation is playing
static const animation_t* _splash_anim = nullptr;// animation currently playing
static uint8_t _splash_frame = 0;                // current frame index
static unsigned long _splash_frame_due_ms = 0;   // millis() at which to advance to the next frame
static unsigned long _splash_end_ms = 0;         // millis() at which the current run ends
static volatile bool _splash_preview_requested = false; // set from the BLE task, consumed in the loop

// Playback duration in seconds, indexed by animation_splash_duration_t (0..3).
static const uint16_t _animation_splash_duration_secs[] = { 10, 20, 40, 60 };

#define DISPLAY_REFRESH_NORMAL_US (2000U) // matches the timerAlarmWrite() value matrix_init() sets up
#define DISPLAY_REFRESH_OTA_US    (4000U) // half the normal refresh rate while OTA is running, to
                                           // free up CPU time for the BLE stack - OTA data transfer
                                           // was proving unreliable on some units at the normal rate

/**
 * @brief Used to convert nibble (4-bits unsigned integer) value of brightness
 * (from RTC and BLE, for example) to the byte (8-bits unsigned integer) value
 * used in matrix, see call of display.setBrightness().
 */
static const uint8_t _matrix_brightness_nibbles[] =
{
    0x07, 0x10, 0x20, 0x30,
    0x40, 0x50, 0x60, 0x70,
    0x80, 0x90, 0xA0, 0xB0,
    0xC0, 0xD0, 0xE0, 0xFF
};

#pragma region Private methods declaration

void handle_colon_after_animation();
void IRAM_ATTR display_updater();
void animation_handler();
void draw_intro(int x, int y);
void set_matrix_time();
void check_brightness_tick();
uint8_t brightness_nibble_to_byte(uint8_t brightness_nibble);
void enter_splash_mode(const animation_t* anim);
void exit_splash_mode();
void step_splash();
void draw_splash_frame();

#pragma endregion // Private methods declaration

#pragma region Public methods definition

void matrix_init(struct tm *init_dt)
{
    matrix_set_time(init_dt, true);

    if (!adcAttachPin(LDR_PIN))
        ESP_LOGE(TAG, "Error: adcAttachPin(LDR_PIN) returned FALSE.");   

    // Initialize display library
    display.begin(16); // Generic ESP32 including Huzzah
    display.setColorOrder(_use_rrbbgg ? RRBBGG : RRGGBB);
    display.flushDisplay();

    // Setup timer for driving display
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, false);
    timerAlarmWrite(timer, DISPLAY_REFRESH_NORMAL_US, true);
    timerAlarmEnable(timer);
    yield();

    display.clearDisplay();
    display.setBrightness(_brightness);

    // "Powered By"
    draw_intro(6, 12);
    delay(2000);

    // Start the Animation Timer
    tetris.setText(F("GOLYAKOFF"));
    animationTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(animationTimer, &animation_handler, false);
    timerAlarmWrite(animationTimer, 100000, true);
    timerAlarmEnable(animationTimer);

    // Wait for the animation to finish
    while (!_finished_animating)
    {
        delay(10); // waiting for intro to finish
    }

    delay(2000);
    _finished_animating = false;
    _display_intro = false;
    tetris.scale = 2;
}

void matrix_pause_timers()
{
    // Both handlers execute code living in flash (PxMatrix and TetrisMatrixDraw), so they must not
    // fire while the flash cache is disabled, i.e. during an NVS write or an OTA flash write.
    if (timer)
        timerAlarmDisable(timer);

    if (animationTimer)
        timerAlarmDisable(animationTimer);
}

void matrix_resume_timers()
{
    // Leave the animation timer off while the OTA screen or a Day Splash animation is up:
    // set_ota_data_ble_write() (and the NVS brightness flush) call matrix_pause_timers()/
    // matrix_resume_timers() around flash writes, and re-enabling the timer here would restart the
    // Tetris clock animation on top of the OTA text / splash frames.
    if (animationTimer && !_ota_mode && !_splash_mode)
        timerAlarmEnable(animationTimer);

    // Keep the display refresh timer off too once the panel has been deliberately blanked for the
    // OTA data transfer (matrix_ota_freeze_display()) - the per-chunk pause/resume around each flash
    // write must not silently bring the refresh ISR back and start eating CPU/RF headroom again.
    if (timer && !_ota_display_frozen)
        timerAlarmEnable(timer);
}

void matrix_enter_ota_mode()
{
    ESP_LOGI(TAG, "Switching matrix to the OTA progress screen");

    // Stop stepping the Tetris animation, but leave the refresh timer running (it just re-scans the
    // frame buffer already sitting in the panel's memory, so the screen stays lit for the whole
    // transfer instead of going dark) - slowed down to DISPLAY_REFRESH_OTA_US, though: OTA transfers
    // were proving unreliable on some units, and cutting how often this ISR interrupts the CPU frees
    // up more headroom for the BLE stack. Reliably completing the transfer matters far more than a
    // perfectly smooth progress screen.
    if (animationTimer)
        timerAlarmDisable(animationTimer);

    if (timer) {
        timerAlarmDisable(timer);
        timerAlarmWrite(timer, DISPLAY_REFRESH_OTA_US, true);
        timerAlarmEnable(timer);
    }

    _ota_mode = true;
    _ota_percent_last = -1;

    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setTextColor(display.color565(255, 144, 0));
    display.setCursor(9, 2);
    display.print("Updating");
    display.setCursor(9, 12);
    display.print("firmware");

    matrix_set_ota_progress(5.0f);
}

void matrix_ota_freeze_display()
{
    if (!_ota_mode || _ota_display_frozen)
        return;

    ESP_LOGI(TAG, "Blanking panel for the OTA data transfer");

    // Free the CPU/RF entirely for the firmware stream: stop refreshing the panel (it goes dark)
    // rather than keeping the "Updating firmware" screen lit for the whole transfer. The refresh ISR
    // is the one thing still running continuously during OTA, and the fast stream is delicate enough
    // that link drops were happening under load (see ota.cpp); taking the display fully out of the
    // picture removes it as a variable. Clear the buffer and let a couple of refresh cycles shift the
    // blank frame onto the panel so it latches all-black, then stop the timer. The panel comes back
    // only to show the failure screen (matrix_show_ota_failed()); a success reboots straight away.
    display.clearDisplay();
    delay(20); // a few DISPLAY_REFRESH_OTA_US cycles to push the blank frame out before freezing
    if (timer)
        timerAlarmDisable(timer);
    _ota_display_frozen = true;
}

void matrix_set_ota_progress(float percent)
{
    // Nothing is drawn while the panel is blanked for the transfer (matrix_ota_freeze_display()) -
    // skip the buffer redraw entirely (it runs on the writer task, once per chunk).
    if (!_ota_mode || _ota_display_frozen)
        return;

    int16_t tenths = (int16_t)(percent * 10.0f + 0.5f);
    if (tenths == _ota_percent_last)
        return;

    _ota_percent_last = tenths;

    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f%%", tenths / 10.0f);

    // Only clear/redraw the percentage row; "Loading"/"firmware" above never change.
    display.fillRect(17, 22, 32, 10, display.color565(0, 0, 0));
    display.setCursor(17, 22);
    display.setTextColor(display.color565(220, 0, 15));
    display.print(buf);
}

void matrix_show_ota_failed()
{
    if (!_ota_mode)
        return;

    // The panel was blanked for the transfer (matrix_ota_freeze_display()); bring the refresh timer
    // back so the error message below is actually visible before the 10s auto-reboot.
    if (_ota_display_frozen) {
        _ota_display_frozen = false;
        if (timer)
            timerAlarmEnable(timer);
    }

    display.clearDisplay();
    display.setCursor(18, 2);
    display.setTextColor(display.color565(220, 0, 15));
    display.print("ERROR");
    display.setCursor(6, 12);
    display.setTextColor(display.color565(220, 120, 0));
    display.print("Reboot in");
    display.setCursor(15, 22);
    display.print("10 sec");
}

void IRAM_ATTR matrix_1hz_isr_loop()
{
    portENTER_CRITICAL_ISR(&timerMux);
    _dt.tm_sec += 1;            // add a second
    _show_colon = !_show_colon; // inverse colon
    portEXIT_CRITICAL_ISR(&timerMux);
}

void matrix_100hz_loop()
{
    // The OTA screen is static and fully managed by matrix_enter_ota_mode()/matrix_set_ota_progress().
    // Brightness re-checks are skipped too: check_brightness_tick()'s analogRead()/setBrightness()
    // briefly interferes with the display refresh ISR, which was the once-a-second flicker seen
    // during OTA (brightness itself just stays frozen at whatever it was when OTA started).
    if (_ota_mode)
        return;

    // Start a preview run requested from the app (staged as a flag by the BLE task, so the actual
    // drawing happens here on the main task rather than concurrently from the BLE stack).
    if (_splash_preview_requested)
    {
        _splash_preview_requested = false;
        if (_show && !_splash_mode)
            enter_splash_mode(animation_get(_animation_splash_index));
    }

    // While an Animation Splash is playing it fully owns the panel; skip the clock logic.
    if (_splash_mode)
    {
        step_splash();
        return;
    }

    // Update if minutes are different only or if update was forced after the time correction
    if (_dt.tm_sec >= 60 || _force_update)
    {
        set_matrix_time();

        if (_force_update)
            _force_update = false;
    }

    if (++tim_100hz > 100)
    {
        check_brightness_tick();
        tim_100hz = 0;
    }

    // To reduce flicker on the screen we stop clearing the screen
    // when the animation is finished, but we still need the colon to
    // to blink
    if (_finished_animating)
    {
        handle_colon_after_animation();
    }
}

void matrix_set_time(const struct tm *new_dt, bool force_update_display)
{
    memcpy(&_dt, new_dt, sizeof(struct tm));

    if (force_update_display)
        _force_update = true;
}

void matrix_get_time(struct tm *dt_out)
{
    memcpy(dt_out, &_dt, sizeof(struct tm));
}

void matrix_set_show(bool show)
{
    _show = show;
}

bool matrix_get_show()
{
    return _show;
}

void matrix_set_auto_brightness(bool use_auto_brightness)
{
    _use_auto_brightness = use_auto_brightness;
}

bool matrix_get_auto_brightness()
{
    return _use_auto_brightness;
}

void matrix_set_manual_brightness(uint8_t manual_brightness_nibble)
{
    _manual_brightness_nibble = manual_brightness_nibble;
}

uint8_t matrix_get_manual_brightness()
{
    return _manual_brightness_nibble;
}

void matrix_set_color_order(bool use_rrbbgg)
{
    _use_rrbbgg = use_rrbbgg;
    display.setColorOrder(_use_rrbbgg ? RRBBGG : RRGGBB);
}

bool matrix_get_color_order()
{
    return _use_rrbbgg;
}

void matrix_set_animation_splash(uint8_t mode, uint8_t duration, uint8_t index)
{
    _animation_splash_mode = mode;
    _animation_splash_duration = duration;
    _animation_splash_index = index;
}

void matrix_get_animation_splash(uint8_t *mode, uint8_t *duration, uint8_t *index)
{
    *mode = _animation_splash_mode;
    *duration = _animation_splash_duration;
    *index = _animation_splash_index;
}

void matrix_request_animation_splash_preview()
{
    // Just raise the flag; matrix_100hz_loop() (main task) starts the run.
    _splash_preview_requested = true;
}

uint8_t matrix_get_animation_count()
{
    // ANIMATION_COUNT comes from the generated registry (animations.h), which is gated per build
    // (the 16MB build may ship more animations than the 4MB one), so this reflects this firmware's
    // actual set.
    return (uint8_t)ANIMATION_COUNT;
}

#pragma endregion // Public methods definition

#pragma region Private methods definition

// This method is needed for driving the display
void IRAM_ATTR display_updater()
{
    display.display(10);
}

// This method is for controlling the tetris library draw calls
void animation_handler()
{
    // Not clearing the display and redrawing it when you
    // dont need to improves how the refresh rate appears
    if (!_finished_animating)
    {
        display.clearDisplay();
        if (_display_intro)
        {
            _finished_animating = tetris.drawText(1, 21);
        }
        else
        {
            _finished_animating = tetris.drawNumbers(2, 26, _show_colon);
        }
    }
}

void draw_intro(int x = 0, int y = 0)
{
    tetris.drawChar("P", x, y, tetris.tetrisCYAN);
    tetris.drawChar("o", x + 5, y, tetris.tetrisMAGENTA);
    tetris.drawChar("w", x + 11, y, tetris.tetrisYELLOW);
    tetris.drawChar("e", x + 17, y, tetris.tetrisGREEN);
    tetris.drawChar("r", x + 22, y, tetris.tetrisBLUE);
    tetris.drawChar("e", x + 27, y, tetris.tetrisRED);
    tetris.drawChar("d", x + 32, y, tetris.tetrisWHITE);
    tetris.drawChar(" ", x + 37, y, tetris.tetrisMAGENTA);
    tetris.drawChar("b", x + 42, y, tetris.tetrisYELLOW);
    tetris.drawChar("y", x + 47, y, tetris.tetrisGREEN);
}

void set_matrix_time()
{
    mktime(&_dt); // normalize it
    log_tm(TAG, "_dt", &_dt);

    // Animation Splash: at the start of an hour (:00) play the selected animation over the whole
    // minute instead of the usual Tetris layout, at the configured cadence, but only while the
    // display is on (if the night off-alarm has the panel off the splash is skipped).
    // step_splash() ends it at :01.
    if (_show && _dt.tm_min == 0 && !_splash_mode)
    {
        bool trigger = false;
        switch (_animation_splash_mode)
        {
            case animation_splash_daily:    trigger = (_dt.tm_hour == 0); break;
            case animation_splash_every_3h: trigger = (_dt.tm_hour % 3 == 0); break;
            case animation_splash_hourly:   trigger = true; break;
            default:                        trigger = false; break; // animation_splash_off
        }

        if (trigger)
        {
            enter_splash_mode(animation_get(_animation_splash_index));
            return;
        }
    }

    strftime(_matrix_time_buffer, sizeof(_matrix_time_buffer), "%H:%M", &_dt);
    tetris.setTime(String(_matrix_time_buffer), FORCE_UPDATE_ALL_DIGITS);

    // Must set this to false so animation knows to start again
    _finished_animating = false;
}

void enter_splash_mode(const animation_t* anim)
{
    if (anim == nullptr || anim->frame_count == 0 || anim->frames == nullptr)
        return;

    _splash_anim = anim;
    _splash_frame = 0;
    _splash_end_ms = millis() +
        (unsigned long)_animation_splash_duration_secs[_animation_splash_duration & 0x03] * 1000UL;
    _splash_mode = true;

    // Stop stepping the Tetris animation so it can't draw over the frames; the display refresh
    // timer stays running at its normal rate so the panel keeps scanning the frame we draw below.
    if (animationTimer)
        timerAlarmDisable(animationTimer);

    display.clearDisplay();
    draw_splash_frame();
    _splash_frame_due_ms = millis() + anim->frame_ms[0];
}

void exit_splash_mode()
{
    _splash_mode = false;
    _splash_anim = nullptr;

    // Resume the Tetris animation and force a clean redraw of the current time (whatever the clock
    // reads once the run's duration has elapsed).
    if (animationTimer)
        timerAlarmEnable(animationTimer);

    display.clearDisplay();
    _force_update = true;
    _finished_animating = false;
}

void step_splash()
{
    unsigned long now = millis();

    // Exit conditions: the display was turned off mid-run, or the configured duration has elapsed.
    if (!_show || (long)(now - _splash_end_ms) >= 0)
    {
        exit_splash_mode();
        return;
    }

    if ((long)(now - _splash_frame_due_ms) >= 0)
    {
        if (++_splash_frame >= _splash_anim->frame_count)
            _splash_frame = 0;
        draw_splash_frame();
        _splash_frame_due_ms = now + _splash_anim->frame_ms[_splash_frame];
    }
}

void draw_splash_frame()
{
    const animation_t* a = _splash_anim;
    const uint16_t* frame = a->frames + (uint32_t)_splash_frame * a->width * a->height;

    for (uint8_t y = 0; y < a->height; y++)
    {
        const uint16_t* row = frame + (uint32_t)y * a->width;
        for (uint8_t x = 0; x < a->width; x++)
            display.drawPixel(x, y, row[x]);
    }
}

void handle_colon_after_animation()
{
    // It will draw the colon every time, but when the colour is black it
    // should look like its clearing it.
    uint16_t colour = _show_colon ? tetris.tetrisWHITE : tetris.tetrisBLACK;
    // The x position that you draw the tetris animation object
    int x = 2;
    // The y position adjusted for where the blocks will fall from
    // (this could be better!)
    int y = 26 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
    tetris.drawColon(x, y, colour);
}

void check_brightness_tick()
{
    if (_show)
    {
        if (_use_auto_brightness)
        {
            // LDR-based auto-adjustment is not wired up yet, so brightness is
            // driven by the user-configurable hourly schedule instead (see brightness_schedule.h).
            _brightness = brightness_nibble_to_byte(brightness_schedule_get_for_hour(_dt.tm_hour));
        }
        else
        {
            _brightness = brightness_nibble_to_byte(_manual_brightness_nibble);
        }
    }
    else
    {
        _brightness = 0;
    }

    if (_brightness != _brightness_last)
    {
        display.setBrightness(_brightness);

        #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
        ESP_LOGD(TAG, "Brightness updated with value %d", _brightness);
        #endif

        _brightness_last = _brightness;
    }
}

uint8_t brightness_nibble_to_byte(uint8_t brightness_nibble)
{
    return _matrix_brightness_nibbles[brightness_nibble & 15];
}

#pragma endregion // Private methods definition