if(NOT EXISTS "${LVGL_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR "Set LVGL_SOURCE_DIR to LVGL v9.4.0 source checkout")
endif()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "LVGL config" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl")
add_executable(ui_preview main.cpp ../../components/ui/src/ui.cpp
    ../../components/ui/src/font_20.c ../../components/ui/src/font_24.c)
target_compile_features(ui_preview PRIVATE cxx_std_17)
target_compile_definitions(ui_preview PRIVATE RC_UI_PREVIEW=1)
target_include_directories(ui_preview PRIVATE include ../../components/ui/include
    ../../components/app_runtime/include ../../components/remote_core/include)
target_link_libraries(ui_preview PRIVATE lvgl::lvgl)
foreach(scenario touch-shot swipe-cancel wake-touch removed-video touch-error
        idle-touch-error touch-error-recover blocked-swipe blocked-status-tap blocked-photo photo-hold scan-select scan-cancel confirm-yes confirm-no retry
        horizontal-navigation horizontal-reverse)
    add_test(NAME ui-${scenario} COMMAND ui_preview "${CMAKE_CURRENT_BINARY_DIR}/${scenario}.ppm" ${scenario})
endforeach()

