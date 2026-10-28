idf_component_register(SRCS "src/board.cpp" "src/shutter_audio.cpp" INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_driver_i2s esp_lcd esp_adc esp_timer
        lvgl espressif__esp_lcd_sh8601 espressif__esp_codec_dev)


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
