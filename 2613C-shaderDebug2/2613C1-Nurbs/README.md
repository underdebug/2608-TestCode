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

The CPU solves the 10 × 10 cubic interpolation problem in surface.h and uploads
the control points and input points once. nurbs.vert evaluates the tensor-product
rational surface on the GPU using Cox–de Boor basis functions. Control-point
weights are currently all 1. The shader generates 40,400 grid vertices and draws
input and control points as cross markers. nurbs.frag supplies their colors.
Rotation, tilt, distance, visibility toggles and reset are available in the panel.
The displayed error measures the CPU interpolation, not GPU float precision.

## Build and run

The native surface implementation currently supports Linux/X11. Requires Qt 6.4+
Widgets, Quick and QuickControls2, X11 development libraries, Vulkan development headers and loader, a
working Vulkan driver, and glslangValidator (Ubuntu package glslang-tools).
The restored template also requires the CUDA toolkit, OpenGL/GLUT/GLEW/GLFW
development packages, and OpenMesh/Open3D libraries at ../../../3rdParty.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j
QT_QPA_PLATFORM=xcb ./test
```

On a Wayland desktop, the application requires XWayland and the xcb Qt platform
plugin. Native Wayland Vulkan surfaces are not implemented.

CMake compiles shader/nurbs.vert and shader/nurbs.frag into
shader/nurbs.vert.spv and shader/nurbs.frag.spv with glslangValidator -V -g -Od. These are separate runtime
files, with shader debug information and optimization disabled. The renderer
loads them from the shader/ folder beside the executable using /proc/self/exe, so changing the working
directory does not affect shader loading. Keep the shader/ folder beside test when moving the executable. Editing GLSL and rebuilding regenerates the affected file;
restart the application to load the new shader.

Debug is the default for a fresh single-config build. GNU/Clang Debug builds use
-O0 -g3. VS Code F5 configures Debug, compiles shaders and launches gdb.
CMake retains the original CUDA language, architecture list, debug flags,
third-party paths and legacy libraries. Qt Widgets, Quick and QuickControls2
provide the UI, and Vulkan provides surface rendering. Quick3D and AUTOMOC are
not required. The executable is generated in the project folder; .spv files are generated
in shader/. Main.qml is embedded as qrc:/Main.qml.
If needed, configure with -DCMAKE_PREFIX_PATH=/your/Qt/gcc_64.
VS Code tasks use the local build directory; debugging requires C/C++ and gdb.

Validation: configured and built against Qt 6.4.2 and Vulkan 1.3.275 with compiler
warnings enabled; both shaders pass spirv-val. A standalone check linked the
renderer without Qt and verified camera projection and interpolation (maximum
error 4.51828e-16). QML loading, view controls, visibility toggles and reset passed
an offscreen integration check. Interactive Vulkan rendering has not been verified because the
authoring session's X display is unavailable.

## Code style

C++ files and shaders use four spaces, Allman braces and expanded function bodies.
Main.qml uses four-space indentation and expanded control blocks.
