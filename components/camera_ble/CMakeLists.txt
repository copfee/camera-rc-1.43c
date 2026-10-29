idf_component_register(SRCS "src/xe5_camera.cpp" "src/fuji_protocol.cpp" INCLUDE_DIRS "include"
    REQUIRES remote_core nvs_flash bt h2zero__esp-nimble-cpp)


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
# Security and GATT execute inside the owner; retain per-function stack evidence.
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Werror=frame-larger-than=2048)
# Read-only diagnostics against the pinned IDF 5.5.3 privacy API.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/src")
