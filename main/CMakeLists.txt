idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES board app_runtime ui nvs_flash)


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
