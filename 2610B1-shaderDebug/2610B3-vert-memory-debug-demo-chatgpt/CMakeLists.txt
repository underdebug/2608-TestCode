cmake_minimum_required(VERSION 3.16)
project(shader_cpp_demo LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/example.vert")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "This stdin compilation demo requires GCC or Clang")
endif()

add_executable(shader_cpp_demo main.cpp)
set_property(TARGET shader_cpp_demo PROPERTY CXX_COMPILER_LAUNCHER
    "${Python3_EXECUTABLE};${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_in_memory.py;${CMAKE_CURRENT_SOURCE_DIR}/main.cpp;${SHADER_SOURCE}"
)
# The compiler reads stdin, so explicitly track the original inputs too.
set_property(SOURCE main.cpp PROPERTY OBJECT_DEPENDS
    "${SHADER_SOURCE};${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_in_memory.py"
)
target_compile_features(shader_cpp_demo PRIVATE cxx_std_17)
target_compile_definitions(shader_cpp_demo PRIVATE CPU_SHADER=1)
target_compile_options(shader_cpp_demo PRIVATE
    "$<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-O0;-g3>"
)

# Managed by buildpath.py
set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
# End buildpath.py
