cmake_minimum_required(VERSION 3.18)
project(test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# main.cu contains ordinary CPU C++; no CUDA toolkit is needed.
set_source_files_properties(main.cu PROPERTIES LANGUAGE CXX)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(main.cu PROPERTIES COMPILE_FLAGS "-x c++")
endif()

add_executable(test main.cu)
add_executable(psnr psnr.cpp)
