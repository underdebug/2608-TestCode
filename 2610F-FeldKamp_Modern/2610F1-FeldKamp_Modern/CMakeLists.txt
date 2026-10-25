cmake_minimum_required(VERSION 3.18)

set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc CACHE FILEPATH "CUDA compiler")
project(test LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CUDA_ARCHITECTURES 75 80 86 87 89 90 100 110 120)

find_package(CUDAToolkit REQUIRED)

add_executable(${PROJECT_NAME}
    main.cu
)

add_executable(feldkamp_cpu FeldKamp_modern.cuh.cc)
target_include_directories(feldkamp_cpu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mem/extension"
)

# -G disables CUDA device optimization and preserves device debug symbols.
# Also disable optimization in nvcc's host compiler for inspecting host locals.
# Keep options on the targets so repeated configuration does not append flags.
set_property(TARGET ${PROJECT_NAME} feldkamp_cpu PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(${PROJECT_NAME} PRIVATE
    "$<$<COMPILE_LANGUAGE:CUDA>:-G;-g;-O0;-Xcompiler=-O0,-g3,-fno-omit-frame-pointer>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-O0;-g3;-fno-omit-frame-pointer>"
)
target_compile_options(feldkamp_cpu PRIVATE
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-O0;-g3;-fno-omit-frame-pointer>"
)

target_link_libraries(${PROJECT_NAME} PRIVATE CUDA::cudart)
