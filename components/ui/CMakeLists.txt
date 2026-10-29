idf_component_register(SRCS "src/ui.cpp" "src/font_20.c" "src/font_24.c" "src/fujifilm_logo.c" "src/xe5_logo.c" "src/shutter_idle.c" "src/shutter_pressed.c" INCLUDE_DIRS "include"
    REQUIRES app_runtime board lvgl)


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
