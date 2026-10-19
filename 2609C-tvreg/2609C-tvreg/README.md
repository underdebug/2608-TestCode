# TV denoising in C++ (Linux)

CPU implementation of Gaussian/L2 TV denoising with split Bregman iteration:

    min_u integral |grad u| dx + (lambda/2) integral (u - f)^2 dx

Smaller lambda means stronger denoising. The source retains the name `main.cu`,
but contains standard C++ and needs no CUDA, OpenMesh, or graphics libraries.
Build requirements: CMake 3.18 or newer and a C++17 compiler.

## Build and run

From the project directory:

```sh
./run.sh
```

Or run the steps individually:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/test
./build/psnr clean.pgm noisy.pgm
./build/psnr clean.pgm denoised.pgm
```

`test` reads `noisy.pgm` and writes `denoised.pgm` in its working directory.
The bundled `clean.pgm` is the reference for quality measurements.
Edit the constants in `main.cu` to change paths, lambda (20), gamma (5), or
iteration count (80). The denoiser does not accept command-line arguments.
`pgm.h` provides image I/O; `psnr.cpp` provides the quality measurement utility.

## VS Code

Use Ctrl+Shift+B to configure and build, the **Run** task to execute, or F5
with **Debug TV denoiser** to debug. Debugging requires the Microsoft C/C++
extension and GDB. All tasks use the workspace-local `build` directory.

## Release build

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
./build-release/test
```

Debug builds include compiler debug symbols; Release builds enable the
compiler's standard release optimizations. Sanitizers and `TV_DEBUG` energy
diagnostics are not enabled by default.

The algorithm uses isotropic TV shrinkage and Neumann boundaries. Gamma is
the split Bregman constraint weight. This implementation covers grayscale
PGM denoising without blur, inpainting, or spatially varying lambda.
