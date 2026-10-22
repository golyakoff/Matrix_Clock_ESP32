idf_component_register(
    SRCS
        main.cpp
        alarm.cpp
        ble.cpp
        brightness_schedule.cpp
        matrix.cpp
        net_settings.cpp
        ota.cpp
        rtc.cpp
        time_helper.cpp
        touch_sensor.cpp
        updater.cpp
        wifi.cpp
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
        esp_wifi
        esp_netif
        esp_http_client
        mbedtls
        json
)

# The project code was written for -Werror-free Arduino builds
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=reorder -Wno-unused-variable)

# The release variants are optimized for size (room in the 4MB build); the display refresh ISR and
# the drawing code keep -O2, as before. A debug build stays at -Og.
if(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
endif()
