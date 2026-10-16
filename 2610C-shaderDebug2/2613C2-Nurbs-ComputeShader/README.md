# NURBS with Vulkan shaders and QML controls

The application embeds a basic QWindow viewport and a QQuickView control panel
side by side in a QWidget host. Main.qml defines the controls and their layout;
The ordinary createControls() function in main.cpp connects QML signals through
a standard QTimer to a C++ lambda. No custom Q_OBJECT class or project-generated
moc file is needed. Qt handles the UI
and native window events.
`MouseTool.h/.cpp` contains four separate classes: `MousePan`, `MouseRotate`,
`MouseZoom`, and `MouseAxis`. VulkanWindow owns one instance of each:

- Shift + move or left-drag pans.
- Ctrl + move or right-drag rotates.
- The wheel zooms in/out around the cursor.
- Alt + wheel moves the axis origin nearer (up) or farther (down) along the
  camera depth direction, computed with inverse X/Y rotation quaternions.
  Translation is `(angleDelta().y() / 120.0)` × orthographic height / viewport height × 20.
  Depth compensation keeps the surface fixed while
  the axis origin changes depth. In this orthographic view, depth movement changes
  occlusion and the rotation pivot, without changing screen position or size.
- Alt (or Meta) + mouse movement, with or without the left button, moves the origin in the camera plane, keeping
  the surface stationary. Shift and Ctrl take priority over this movement.
- Alt + left double-click sets the coordinate origin and rotation center.
  Single clicks do not pick an origin; Alt + left-drag moves the origin without panning or rotating the surface.

Shift takes priority over Ctrl. Both modifier movement modes work without a mouse
button. For coordinate picking, a far-plane/background click borrows the nearest
valid depth in screen space while retaining the original clicked pixel's X/Y for
world-space conversion. An empty depth buffer leaves the origin unchanged; axes
and rulers are excluded from picking. Changing the origin keeps the surface in
place and chooses the pivot for subsequent rotations. Reset view restores the
coordinate origin. The controls panel sits to the left of the viewport.
Scroll the mouse wheel up to zoom in or down to zoom out, keeping the point under
the mouse cursor fixed in the viewport. The Camera distance
slider stays synchronized and its limits also apply to wheel zoom.
Zoom spans 20–5000 (default 450), with a 15% distance reduction per upward wheel notch.
The Rotation and Tilt sliders stay synchronized; Reset view restores the default camera.
The bottom and right edges show numbered rulers with adaptive tick spacing.
The orthographic camera matches the reference: Shift panning translates the entire
view uniformly at every depth. Ruler values measure camera-plane units rather
than distances on the curved surface. Panning shifts the values and zooming adjusts
the scale. The labeled world axes are X (red), Y (green) and Z (blue), and follow
the surface rotation. Rulers and axes are drawn directly with Vulkan line primitives.
X/Y/Z labels stay upright facing the camera at a fixed 8×12 framebuffer-pixel size,
anchored to the rotating axis tips regardless of zoom.
The axes share one world-space length, adjusted so the longest projected axis
measures one-fifth of the viewport width after rotation, zoom or resize (excluding labels).
Coordinate axis lines use two adjacent pixel-wide strokes for a two-pixel width.
`VulkanWindow` replaces the former inline VulkanViewport class. VulkanWindow.h exposes the
camera state, navigation callbacks, rendering and depth-picking API. VulkanWindow.cpp
owns the Vulkan instance, native X11 surface, device, queue, swapchain, depth image,
render pass, framebuffers and frame synchronization. It handles mouse/keyboard
input, camera projection, nearest-valid-depth picking, coordinate axes and rulers.
The basic Vulkan setup uses a combined graphics/compute/presentation queue, FIFO
presentation, a single in-flight frame and D32 float depth (D16 fallback).
Resize and out-of-date results rebuild the swapchain; resources are released before
the native window is destroyed. No GLFW, QVulkanInstance or Quick 3D is used.

`SurfaceGeometry : public VulkanWindow` implements the scene lifecycle methods for NURBS
input validation, GPU storage, descriptors, compute interpolation and surface drawing.
These resources and methods live directly in SurfaceGeometry, without a Program wrapper.
The base VulkanWindow supplies the physical/logical device, render pass, camera matrix
and frame command buffer through protected virtual methods. Its header does not
depend on SurfaceGeometry. Main.cpp instantiates SurfaceGeometry directly.
`setInputPoints` requests a frame automatically. SurfaceGeometry releases the
renderer in its destructor while the derived scene methods are still available,
before its shader resources and the base window are destroyed.
Vulkan helpers are local to the implementation files. `shader/overlay.vert` draws window rulers and axes;
`shader/nurbs.vert` draws only the surface and input/control-point markers.
Main.cpp composes the window, generates demo points and binds the QML controls.

The CPU supplies input points through `SurfaceGeometry::setInputPoints(points, rows, columns)`.
Each point is `{x, y, z, 1}` and the array is row-major:
`points[row * columns + column]`. Each dimension supports 4–16 points for cubic interpolation.
The default 10×11 demo is generated in main.cpp and passed through setInputPoints.
The QML “Change surface style” button cycles Waves (10×11), Bowl (13×9) and Saddle (16×12); each change
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

CMake compiles shader/nurbs.vert, shader/nurbs.frag, shader/nurbs.comp and shader/overlay.vert into
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

Validation: the VulkanWindow refactor builds successfully and all shaders pass spirv-val.
An offscreen Qt event check verified left-drag/Shift pan equivalence, right-drag/Ctrl rotation equivalence,
modifier priority, Alt+either-button picking and drag suppression, wheel zoom,
D32/D16 nearest-depth selection, world-position conversion, input validation and
repeated release. Interactive Vulkan rendering could not be verified because
Qt could not connect to the session's X display `:0`.

## Code style

C++ files and shaders use four spaces, Allman braces and expanded function bodies.
Main.qml uses four-space indentation and expanded control blocks.
