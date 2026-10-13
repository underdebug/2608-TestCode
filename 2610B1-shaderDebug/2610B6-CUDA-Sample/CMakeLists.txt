cmake_minimum_required(VERSION 3.24)
# Prefer nvcc on PATH so compiler and toolkit tools come from the same install.
if(NOT DEFINED CMAKE_CUDA_COMPILER AND "$ENV{CUDACXX}" STREQUAL "")
    find_program(CMAKE_CUDA_COMPILER NAMES nvcc REQUIRED)
endif()
# Set the architecture before CUDA compiler detection, including its test build.
set(CMAKE_CUDA_ARCHITECTURES 89 CACHE STRING "CUDA architectures to build for")
project(CUDASample LANGUAGES CXX CUDA)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
add_executable(test main.cu kernel.cu)
target_compile_options(test PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-G;-g;-O0>)
set_target_properties(test PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
