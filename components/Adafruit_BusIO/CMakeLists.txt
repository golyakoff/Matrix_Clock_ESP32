# Adafruit Bus IO Library 1.17.4
# https://github.com/adafruit/Adafruit_BusIO
# MIT License

idf_component_register(SRCS "Adafruit_I2CDevice.cpp" "Adafruit_BusIO_Register.cpp" "Adafruit_SPIDevice.cpp" "Adafruit_GenericDevice.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES arduino-esp32)
