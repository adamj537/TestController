# component CMakeLists.txt

idf_component_register(SRCS "hal.c" "hal_gpio.c" "hal_i2c.c" "hal_spi.c"
                    INCLUDE_DIRS "."
                    REQUIRES "driver")
