idf_component_register(
    SRCS
        main.cpp
        alarm.cpp
        ble.cpp
        brightness_schedule.cpp
        matrix.cpp
        ota.cpp
        rtc.cpp
        time_helper.cpp
        touch_sensor.cpp
    INCLUDE_DIRS include
    REQUIRES
        arduino-esp32
        esp-nimble-cpp
        Adafruit_GFX
        PxMatrix
        TetrisAnimation
        ErriezDS3231
        app_update
        nvs_flash
)

# The project code was written for -Werror-free Arduino builds
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=reorder -Wno-unused-variable)
