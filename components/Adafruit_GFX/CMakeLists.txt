# Adafruit GFX Library 1.13.0
# https://github.com/adafruit/Adafruit-GFX-Library
# BSD License

idf_component_register(SRCS "Adafruit_GFX.cpp" "Adafruit_GrayOLED.cpp" "Adafruit_SPITFT.cpp" "glcdfont.c"
                       INCLUDE_DIRS "."
                       REQUIRES arduino-esp32 Adafruit_BusIO)

# Drawing code runs from the display timers: keep it at -O2 in the size-optimized release builds
if(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
endif()
