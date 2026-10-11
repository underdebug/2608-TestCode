# Flat Qt / QML NURBS project

All application files are in the same directory:

- CMakeLists.txt
- main.cpp
- SurfaceGeometry.cpp
- SurfaceGeometry.hpp
- surface.hpp
- Main.qml

No src/ or qml/ directories. QML is retained for the requested Qt/QML UI.
.vscode/ contains only VS Code configuration.

CMake preserves your project(test LANGUAGES CXX CUDA), CUDA flags/architectures,
third-party paths, existing dependencies, libraries and executable output location.
Additions: Qt6 Quick/QuickControls2/Quick3D, AUTOMOC, geometry source files,
and Main.qml as an embedded resource. main.cpp loads qrc:/Main.qml.

## Build

On Ubuntu, install `qt6-shadertools-dev` along with the Qt Quick 3D development
packages. Quick 3D's CMake configuration requires it; without it, configuration
fails with `Qt6ShaderTools could not be found` even when `qt6-quick3d-dev` is
installed:

```bash
sudo apt install qt6-shadertools-dev
```

Run within your original development directory so ../../../3rdParty points to
its intended location. Qt 6.4+ with Quick 3D and all your existing dependencies
must be installed. Your CUDA compiler must support the architecture list you
provided (preserved unchanged).

```bash
cmake -S . -B build
cmake --build build -j
./test
```

If Qt is not found, add your actual Qt kit path to configure:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/your/Qt/gcc_64
```

For F5, add that same -DCMAKE_PREFIX_PATH argument to the configure task in
.vscode/tasks.json if necessary. Open this folder in VS Code, install C/C++ and
CMake Tools, then Ctrl+Shift+B to build or F5 to build and debug ./test.

CPU interpolation is verified with maximum error 4.51828e-16. This version
still evaluates the surface on CPU; CUDA language/dependencies are retained
from your CMake template but no CUDA kernel is added. Qt Quick 3D requests
Vulkan by default. The Qt GUI and the complete CUDA/third-party build have
not been compiled or run in the authoring environment, which lacks those
installed dependencies.
