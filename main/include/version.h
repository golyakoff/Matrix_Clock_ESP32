#ifndef __VERSION_H__
#define __VERSION_H__

#include "esp_app_desc.h"

// The firmware version, e.g. "v2.0.0": PROJECT_VER, read from version.txt by the top CMakeLists.txt
#define MATRIX_CLOCK_FIRMWARE_VERSION (esp_app_get_description()->version)

#endif // __VERSION_H__
