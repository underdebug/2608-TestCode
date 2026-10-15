# NURBS with Vulkan shaders and QML controls

The application embeds a basic QWindow viewport and a QQuickView control panel
side by side in a QWidget host. Main.qml defines the controls and their layout;
The ordinary createControls() function in main.cpp connects QML signals through
a standard QTimer to a C++ lambda. No custom Q_OBJECT class or project-generated
moc file is needed. Qt handles the UI
and native window events.
SurfaceGeometry is a plain C++ renderer with no Qt dependency. It receives the
window's native X11 ID and pixel dimensions from the UI.

SurfaceGeometry.cpp creates its own Vulkan instance and calls
vkCreateXlibSurfaceKHR directly. It selects the GPU, creates the device, queue,
swap chain, image views, depth image, render pass, framebuffers and graphics
pipeline, and records, submits and presents command buffers through Vulkan API
calls. Fences protect the single in-flight command buffer, and presentation
semaphores belong to swap-chain images. Resize and out-of-date results trigger
swap-chain recreation. Vulkan resources are released before Qt destroys the
native window. No GLFW, QVulkanInstance, QVulkanWindowRenderer or Quick 3D is used.

The CPU supplies input points through `SurfaceGeometry::setInputPoints(points, rows, columns)`.
Each point is `{x, y, z, 1}` and the array is row-major:
`points[row * columns + column]`. Each dimension supports 4–13 points for cubic interpolation.
The default 10×11 demo is generated in main.cpp and passed through setInputPoints.
The QML “Change surface style” button cycles Waves (10×11), Bowl (11×12) and Saddle (12×13); each change
calls setInputPoints with a new CPU grid and requests a frame. Input coordinates must be finite;
weights are currently fixed at 1. Call the setter before initialization; it also works during rendering;
updates upload and recalculate on the next requested frame.

```cpp
std::vector<SurfaceGeometry::InputPoint> points(rows * columns);
// Fill points[row * columns + column] with {x, y, z, 1.0f}.
geometry.setInputPoints(points, rows, columns);
```

The renderer uploads the points to a host-visible storage buffer at binding 0.
The compute push constants supply the phase, row count and column count.
nurbs.comp builds the cubic basis matrices, solves both interpolation directions
with partial-pivot Gauss–Jordan elimination, then generates the surface grid and
input/control-point cross markers. Three dispatches run with memory barriers
between them. Intermediate results and computed control points stay in GPU storage.
nurbs.vert reads
cached positions and applies the camera matrix. Rotation, visibility changes and
swap-chain recreation reuse those positions. All surface calculations use shader
single precision; weights are fixed at 1. The CPU surface helpers in surface.h have been removed. The former CPU interpolation-error label
has been removed because the render path no longer performs a CPU solve.

## Build and run

The native surface implementation currently supports Linux/X11. Requires Qt 6.4+
Widgets, Quick and QuickControls2, X11 development libraries, Vulkan development headers and loader, a
working Vulkan driver, and glslangValidator (Ubuntu package glslang-tools).
The restored template also requires the CUDA toolkit, OpenGL/GLUT/GLEW/GLFW
development packages, and OpenMesh/Open3D libraries at ../../../3rdParty.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j
QT_QPA_PLATFORM=xcb ./build/test
```

On a Wayland desktop, the application requires XWayland and the xcb Qt platform
plugin. Native Wayland Vulkan surfaces are not implemented.

CMake compiles shader/nurbs.vert, shader/nurbs.frag and shader/nurbs.comp into
the corresponding shader/*.spv files with glslangValidator -V -g -Od. These are separate runtime
files, with shader debug information and optimization disabled. The renderer
loads them from the shader/ folder beside the executable using /proc/self/exe, so changing the working
directory does not affect shader loading. Keep the shader/ folder beside test when moving the executable. Editing GLSL and rebuilding regenerates the affected file;
restart the application to load the new shader.

Debug is the default for a fresh single-config build. GNU/Clang Debug builds use
-O0 -g3. VS Code F5 configures Debug, compiles shaders and launches gdb.
CMake retains the original CUDA language, architecture list, debug flags,
third-party paths and legacy libraries. Qt Widgets, Quick and QuickControls2
provide the UI, and Vulkan provides surface rendering. Quick3D and AUTOMOC are
not required. The executable is generated in the build folder; .spv files are generated
in shader/. Main.qml is embedded as qrc:/Main.qml.
If needed, configure with -DCMAKE_PREFIX_PATH=/your/Qt/gcc_64.
VS Code tasks configure and build the local build directory; F5 launches
build/test with CUDA-GDB and requires the NVIDIA Nsight Visual Studio Code Edition extension.

Validation: configured and built against Qt 6.4.2 and Vulkan 1.3.275 with compiler
warnings enabled; both shaders pass spirv-val. A standalone check linked the
renderer without Qt and verified camera projection and interpolation (maximum
error 4.51828e-16). QML loading, view controls, visibility toggles and reset passed
an offscreen integration check. Interactive Vulkan rendering has not been verified because the
authoring session's X display is unavailable.

## Code style

C++ files and shaders use four spaces, Allman braces and expanded function bodies.
Main.qml uses four-space indentation and expanded control blocks.
