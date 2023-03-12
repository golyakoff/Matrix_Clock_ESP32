#include "matrix.h"

#include <PxMatrix.h>
#include <TetrisMatrixDraw.h>

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t * timer = NULL;
hw_timer_t * animationTimer = NULL;

PxMATRIX display(64,32,P_LAT, P_OE, P_A, P_B, P_C, P_D);
TetrisMatrixDraw tetris(display);

bool showColon = true;
volatile bool finishedAnimating = false;
bool displayIntro = true;

String lastDisplayedTime = "";

// If this is set to false, the number will only change if the value behind it changes
// e.g. the digit representing the least significant minute will be replaced every minute,
// but the most significant number will only be replaced every 10 minutes.
// When true, all digits will be replaced every minute.
bool forceRefresh = true;

int adc_brightness = 255;

// This method is needed for driving the display
void IRAM_ATTR display_updater() {
    portENTER_CRITICAL_ISR(&timerMux);
    display.display(10);
    portEXIT_CRITICAL_ISR(&timerMux);
}

// This method is for controlling the tetris library draw calls
void animationHandler()
{
    portENTER_CRITICAL_ISR(&timerMux);

    // Not clearing the display and redrawing it when you
    // dont need to improves how the refresh rate appears
    if (!finishedAnimating) {
    display.clearDisplay();

    if (displayIntro) {
        finishedAnimating = tetris.drawText(1, 21);
    } else {
        finishedAnimating = tetris.drawNumbers(2, 26, showColon);
    }
  }

  portEXIT_CRITICAL_ISR(&timerMux);
}

void drawIntro(int x = 0, int y = 0)
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

void drawConnecting(int x = 0, int y = 0)
{
  tetris.drawChar("C", x, y, tetris.tetrisCYAN);
  tetris.drawChar("o", x + 5, y, tetris.tetrisMAGENTA);
  tetris.drawChar("n", x + 11, y, tetris.tetrisYELLOW);
  tetris.drawChar("n", x + 17, y, tetris.tetrisGREEN);
  tetris.drawChar("e", x + 22, y, tetris.tetrisBLUE);
  tetris.drawChar("c", x + 27, y, tetris.tetrisRED);
  tetris.drawChar("t", x + 32, y, tetris.tetrisWHITE);
  tetris.drawChar("i", x + 37, y, tetris.tetrisMAGENTA);
  tetris.drawChar("n", x + 42, y, tetris.tetrisYELLOW);
  tetris.drawChar("g", x + 47, y, tetris.tetrisGREEN);
}

void setMatrixTime(uint8_t hours, uint8_t minutes) {
  String timeString = "";
  
  /// TODO: add real building of string in that format.
  timeString = "12:34";
  
  // Only update Time if its different
  if (lastDisplayedTime != timeString) {
    Serial.println(timeString);
    lastDisplayedTime = timeString;
    tetris.setTime(timeString, forceRefresh);

    // Must set this to false so animation knows
    // to start again
    finishedAnimating = false;
  }
}

void handleColonAfterAnimation() {
  // It will draw the colon every time, but when the colour is black it
  // should look like its clearing it.
  uint16_t colour =  showColon ? tetris.tetrisWHITE : tetris.tetrisBLACK;
  // The x position that you draw the tetris animation object
  int x = 2;
  // The y position adjusted for where the blocks will fall from
  // (this could be better!)
  int y = 26 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  tetris.drawColon(x, y, colour);
}

void adcTick() {
   adc_brightness = map(
      analogRead(VARISTOR_PIN),
      0, ADC_SCALE,
      PWM_MIN_VALUE, PWM_MAX_VALUE);

    display.setBrightness(adc_brightness);
    Serial.println(adc_brightness);
}

void matrix_init()
{
#ifdef ADJUST_BRIGHTNESS
    pinMode(VARISTOR_PIN, INPUT);
#endif //ADJUST_BRIGHTNESS

    // Intialise display library
    display.begin(16); // Generic ESP32 including Huzzah
    display.flushDisplay();

    // Setup timer for driving display
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, true);
    timerAlarmWrite(timer, 2000, true);
    timerAlarmEnable(timer);
    yield();
    
    display.clearDisplay();

    // "connecting"
    drawConnecting(5, 10);
    delay(2000);
    display.clearDisplay();
    
    // "Powered By"
    drawIntro(6, 12);
    delay(2000);

    // Start the Animation Timer
    tetris.setText("GOLYAKOFF");
    animationTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(animationTimer, &animationHandler, true);
    timerAlarmWrite(animationTimer, 100000, true);
    timerAlarmEnable(animationTimer);

    // Wait for the animation to finish
    while (!finishedAnimating)
    {
        delay(10); //waiting for intro to finish
    }
    delay(2000);
    finishedAnimating = false;
    displayIntro = false;
    tetris.scale = 2;
}

void matrix_loop_every_second()
{
    adcTick();

    // We can call this often, but it will only
    // update when it needs to
    setMatrixTime(((uint8_t)12), ((uint8_t)34));
    showColon = !showColon;

    // To reduce flicker on the screen we stop clearing the screen
    // when the animation is finished, but we still need the colon to
    // to blink
    if (finishedAnimating)
    {
      handleColonAfterAnimation();
    }
}