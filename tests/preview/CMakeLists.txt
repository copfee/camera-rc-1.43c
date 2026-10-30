if(NOT EXISTS "${LVGL_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR "Set LVGL_SOURCE_DIR to LVGL v9.4.0 source checkout")
endif()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "LVGL config" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl")
set(RC_SHUTTER_STYLE "model" CACHE STRING "Shutter button style (model or dragon)")
set_property(CACHE RC_SHUTTER_STYLE PROPERTY STRINGS model dragon)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/shutter_assets.cmake")
add_executable(ui_preview main.cpp ../../components/ui/src/ui.cpp
    ../../components/ui/src/font_20.c ../../components/ui/src/font_24.c
    ../../components/ui/src/fujifilm_logo.c ../../components/ui/src/xe5_logo.c ${RC_SHUTTER_SOURCES})
target_compile_features(ui_preview PRIVATE cxx_std_17)
target_compile_definitions(ui_preview PRIVATE RC_UI_PREVIEW=1 ${RC_SHUTTER_DEFINITIONS})
target_include_directories(ui_preview PRIVATE include ../../components/ui/include
    ../../components/app_runtime/include ../../components/remote_core/include)
target_link_libraries(ui_preview PRIVATE lvgl::lvgl)
foreach(scenario touch-shot swipe-cancel wake-touch removed-video touch-error
        idle-touch-error touch-error-recover blocked-swipe blocked-status-tap blocked-photo photo-hold scan-select scan-cancel confirm-yes confirm-no retry
        button-navigation no-swipe-navigation edge-gestures device-connect device-forget connecting-animation
        device-settings fullscreen fullscreen-start spinner-cadence shutter-feedback device-refresh progress-continuity connection-status device-identity shooting-layout hint-fade)
    add_test(NAME ui-${scenario} COMMAND ui_preview "${CMAKE_CURRENT_BINARY_DIR}/${scenario}.ppm" ${scenario})
endforeach()

