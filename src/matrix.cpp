#include <Arduino.h>
#include <PxMatrix.h>
#include <TetrisMatrixDraw.h>
#include <esp_log.h>

#include "time_helper.h"
#include "matrix.h"

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

static bool _show_colon = true;
static volatile bool _finished_animating = false;
static bool _display_intro = true;

static char _matrix_time_buffer[6]; // the buffer of the string containing time, like "12:34\n"
static struct tm _dt;               // time to display on the matrix
static bool _force_update;          // if "true", there will be performed forced update of the time

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

#pragma endregion // Private methods declaration

#pragma region Public methods definition

void matrix_init(struct tm *init_dt)
{
    matrix_set_time(init_dt, true);

    //analogReadResolution(10);
    if (!adcAttachPin(LDR_PIN))
        ESP_LOGE(TAG, "Error: adcAttachPin(LDR_PIN) returned FALSE.");   

    // Initialize display library
    display.begin(16); // Generic ESP32 including Huzzah
    display.setColorOrder(COLOR_ORDER);
    display.flushDisplay();

    // Setup timer for driving display
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, true);
    timerAlarmWrite(timer, 2000, true);
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
    timerAttachInterrupt(animationTimer, &animation_handler, true);
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

void matrix_unload() {
    ESP_LOGI(TAG, "Unloading matrix display for OTA... ");

    display.clearDisplay();
    display.flushDisplay();
    display.setBrightness(0);

    if (animationTimer) {
        timerAlarmDisable(animationTimer);
        timerDetachInterrupt(animationTimer);
        timerEnd(animationTimer);
        animationTimer = NULL;
    }

    if (timer) {
        timerAlarmDisable(timer);
        timerDetachInterrupt(timer);
        timerEnd(timer);
        timer = NULL;
    }

    _finished_animating = true;
    _display_intro = false;

    ESP_LOGI(TAG, "OK: Unloaded");
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
    // Update if minutes are different only or if update was forced after the time correction
    if (_dt.tm_sec >= 60 || _force_update)
    {
        set_matrix_time();

        if (_force_update)
            _force_update = false;
    }

    if (++tim_100hz > 20)
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

#pragma endregion // Public methods definition

#pragma region Private methods definition

// This method is needed for driving the display
void IRAM_ATTR display_updater()
{
    /// TODO: Pay attention
    // AGOXXX portENTER_CRITICAL_ISR(&timerMux);
    display.display(10);
    // AGOXXX portEXIT_CRITICAL_ISR(&timerMux);
}

// This method is for controlling the tetris library draw calls
void animation_handler()
{
    /// TODO: Pay attention
    // AGOXXX portENTER_CRITICAL_ISR(&timerMux);
    
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

    // AGOXXX portEXIT_CRITICAL_ISR(&timerMux);
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

    strftime(_matrix_time_buffer, sizeof(_matrix_time_buffer), "%H:%M", &_dt);
    tetris.setTime(String(_matrix_time_buffer), FORCE_UPDATE_ALL_DIGITS);

    // Must set this to false so animation knows to start again
    _finished_animating = false;
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
            uint16_t adc_var_val = analogRead(LDR_PIN);
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "ADC value from LDR is %d", adc_var_val);
            #endif

            // TODO: Realize auto-adjust brightness
            // _brightness = map(
            //     adc_var_val,
            //     0, ADC_SCALE,
            //     PWM_MIN_VALUE, PWM_MAX_VALUE);
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