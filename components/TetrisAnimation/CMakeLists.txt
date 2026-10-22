# TetrisAnimation, https://github.com/golyakoff/TetrisAnimation
# Sources of commit 9848a4cad1a29fe9ef24cb24b1a2a6c0d0240b90, MIT License

idf_component_register(SRCS "TetrisMatrixDraw.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES arduino-esp32 Adafruit_GFX)
