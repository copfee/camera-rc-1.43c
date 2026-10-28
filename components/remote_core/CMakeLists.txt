idf_component_register(SRCS "src/controller.cpp" INCLUDE_DIRS "include")


target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
