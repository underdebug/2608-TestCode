cmake_minimum_required(VERSION 3.18)

set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc CACHE FILEPATH "CUDA compiler")
project(test LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -G -g -O0")
set(CMAKE_CUDA_ARCHITECTURES 75 80 86 87 89 90 100 110 120)

set(THIRD_PARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdParty")

find_package(CUDAToolkit REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# Qt is used for the window and QML controls; no custom moc files are needed.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Quick QuickControls2)

# Direct Vulkan rendering and build-time GLSL -> SPIR-V compilation.
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

include_directories(
    ${OPENGL_INCLUDE_DIR}
    ${CUDAToolkit_INCLUDE_DIRS}
    ${GLUT_INCLUDE_DIRS}
    "${THIRD_PARTY_ROOT}/OpenMesh/src"
    "${THIRD_PARTY_ROOT}/Open3D/cpp"
    "${THIRD_PARTY_ROOT}/eigen"
)

link_directories(
    "${THIRD_PARTY_ROOT}/OpenMesh/build/Build/lib"
    "${THIRD_PARTY_ROOT}/Open3D/build/lib/Release"
)

add_executable(${PROJECT_NAME}
    main.cpp
    SurfaceGeometry.cpp
    SurfaceGeometry.h
    surface.h
)

# Main.qml is next to main.cpp, embedded as qrc:/Main.qml.
qt_add_resources(${PROJECT_NAME} qml_resources
    PREFIX "/"
    FILES Main.qml
)

# CPU debug symbols and no optimization; CUDA flags are configured above.
target_compile_options(${PROJECT_NAME} PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-O0;-g3>"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    X11::X11
    GL
    GLU
    cuda
    cudart
    glfw
    GLEW::GLEW
    OpenMeshCored
    OpenMeshToolsd
    Open3D
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Vulkan::Vulkan
)


# Shader sources and generated .spv files are in ./shader.
# -g retains shader debug information; -Od disables shader optimization.
set(SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shader")
set(SHADER_BINARIES)
foreach(SHADER nurbs.vert nurbs.frag)
    set(BINARY "${SHADER_DIRECTORY}/${SHADER}.spv")
    add_custom_command(
        OUTPUT "${BINARY}"
        COMMAND "${GLSLANG_VALIDATOR}" -V -g -Od
                "${SHADER_DIRECTORY}/${SHADER}" -o "${BINARY}"
        DEPENDS "${SHADER_DIRECTORY}/${SHADER}"
        VERBATIM
    )
    list(APPEND SHADER_BINARIES "${BINARY}")
endforeach()
add_custom_target(nurbs_shader
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shader"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${SHADER_BINARIES} "${CMAKE_CURRENT_BINARY_DIR}/shader"
    DEPENDS ${SHADER_BINARIES}
    VERBATIM
)
add_dependencies(${PROJECT_NAME} nurbs_shader)
