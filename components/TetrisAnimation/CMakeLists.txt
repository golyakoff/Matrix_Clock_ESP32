# TetrisAnimation, https://github.com/golyakoff/TetrisAnimation
# Sources of commit 9848a4cad1a29fe9ef24cb24b1a2a6c0d0240b90, MIT License

idf_component_register(SRCS "TetrisMatrixDraw.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES arduino-esp32 Adafruit_GFX)

# Drawing code runs from the display timers: keep it at -O2 in the size-optimized release builds
if(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
endif()
