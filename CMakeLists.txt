cmake_minimum_required(VERSION 3.16)

# The firmware version, e.g. "v2.0.0": the app compares it with the GitHub release tags.
# -DMC_VERSION=v2.0.1 overrides it for a test update (tools/make_test_update.sh).
if(DEFINED MC_VERSION)
    set(PROJECT_VER ${MC_VERSION})
else()
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/version.txt PROJECT_VER LIMIT_COUNT 1)
endif()
# reconfigure (and rebuild with the new version) when version.txt changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/version.txt)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only main and what it depends on: the Arduino component pulls in many optional components
idf_build_set_property(MINIMAL_BUILD ON)
project(tetris-clock)

# The Arduino component requires a component for every Arduino library (Zigbee, RainMaker, Modbus,
# ...), so they are compiled although CONFIG_ARDUINO_SELECTIVE_* leaves them out of the firmware.
# Not our code: do not let its new compiler warnings (ESP-IDF 5.5) fail the build.
if(TARGET __idf_espressif__esp-modbus)
    target_compile_options(__idf_espressif__esp-modbus PRIVATE -Wno-error=stringop-truncation)
endif()
