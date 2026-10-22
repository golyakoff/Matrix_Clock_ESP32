// Host stub of the ESP-IDF log
#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("I %s: " fmt "\n", tag, ##__VA_ARGS__)
