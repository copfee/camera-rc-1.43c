if(CONFIG_RC_SHUTTER_STYLE_DRAGON)
    set(RC_SHUTTER_STYLE dragon)
else()
    set(RC_SHUTTER_STYLE model)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/shutter_assets.cmake")
idf_component_register(SRCS "src/ui.cpp" "src/font_20.c" "src/font_24.c" "src/fujifilm_logo.c" "src/xe5_logo.c" ${RC_SHUTTER_SOURCES} INCLUDE_DIRS "include"
    REQUIRES app_runtime board lvgl)


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1 ${RC_SHUTTER_DEFINITIONS})
