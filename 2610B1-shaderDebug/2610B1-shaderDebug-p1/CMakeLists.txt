cmake_minimum_required(VERSION 3.18)

set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc CACHE FILEPATH "CUDA compiler")
project(test LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -G -g -O0")
set(CMAKE_CUDA_ARCHITECTURES 75 80 86 87 89 90 100 110 120)

set(THIRD_PARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdParty")

find_package(CUDAToolkit REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)

# Added for the Qt / QML interface.
find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Quick3D)
set(CMAKE_AUTOMOC ON)

include_directories(
    ${OPENGL_INCLUDE_DIR}
    ${CUDAToolkit_INCLUDE_DIRS}
    ${GLUT_INCLUDE_DIRS}
    "${THIRD_PARTY_ROOT}/OpenMesh/src"
    "${THIRD_PARTY_ROOT}/Open3D/cpp"
    "${THIRD_PARTY_ROOT}/eigen"
    "${THIRD_PARTY_ROOT}/glm"
)

link_directories(
    "${THIRD_PARTY_ROOT}/OpenMesh/build/Build/lib"
    "${THIRD_PARTY_ROOT}/Open3D/build/lib/Release"
)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    X11
    GL
    GLU
    cuda
    cudart
    glfw
    GLEW::GLEW
    OpenMeshCored
    OpenMeshToolsd
    Open3D
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Quick3D
    glm::glm
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)


find_program(GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)
file(GLOB GLSL_SHADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
)

set(SPIRV_SHADERS)
foreach(SHADER IN LISTS GLSL_SHADERS)
    set(SPIRV "${SHADER}.spv")
    get_filename_component(SHADER_NAME "${SHADER}" NAME)
    add_custom_command(
        OUTPUT "${SPIRV}"
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE=${SHADER}"
            "-DSTAGED=${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}"
            "-DOUTPUT=${SPIRV}"
            "-DCOMPILER=${GLSLANG_VALIDATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShader.cmake"
        DEPENDS "${SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShader.cmake"
        COMMENT "Compiling ${SHADER} to SPIR-V"
        VERBATIM
    )
    list(APPEND SPIRV_SHADERS "${SPIRV}")
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPIRV_SHADERS})
add_dependencies(${PROJECT_NAME} compile_shaders)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB SHADER_HEADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl"
)
set(SHADER_HEADERS)
foreach(SHADER IN LISTS SHADER_HEADER_SOURCES)
    list(APPEND SHADER_HEADERS "${SHADER}.hpp")
endforeach()

# Run on every build, restoring headers even if only a header was edited.
add_custom_target(generate_shader_headers
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.py"
    BYPRODUCTS ${SHADER_HEADERS}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.py" ${SHADER_HEADER_SOURCES}
    COMMENT "Generating C++ shader headers"
    VERBATIM
)
add_dependencies(${PROJECT_NAME} generate_shader_headers)

# Managed by buildpath.py
set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
# End buildpath.py
