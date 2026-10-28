idf_component_register(SRCS "src/runtime.cpp" INCLUDE_DIRS "include" REQUIRES remote_core board camera_ble)


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
# Keep large snapshot temporaries from silently consuming the owner task stack.
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Werror=frame-larger-than=2048)
