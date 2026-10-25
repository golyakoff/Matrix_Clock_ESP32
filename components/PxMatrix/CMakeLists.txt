# PxMatrix LED MATRIX library, https://github.com/2dom/PxMatrix
# Header of commit ea8dc954f486c0f260efd98b51dd1b41312bb556, BSD License.
# Local changes for ESP-IDF 5 / Arduino core 3.x are marked "Matrix Clock" in PxMatrix.h.

idf_component_register(INCLUDE_DIRS "."
                       REQUIRES arduino-esp32 Adafruit_GFX)

# Every user of the header must agree on the class layout: the switch goes to them all
if(CONFIG_MC_DISPLAY_DOUBLE_BUFFER)
    target_compile_definitions(${COMPONENT_LIB} INTERFACE double_buffer)
endif()
