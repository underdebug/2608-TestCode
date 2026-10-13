# CUDA Sample for VS Code on Linux

Requirements: CUDA Toolkit (nvcc), CMake, C++ compiler, working NVIDIA driver and GPU. To debug with F5 install NVIDIA Nsight Visual Studio Code Edition (CUDA-GDB support).

## Source files

- `main.cu`: host allocations, transfers, kernel launch, output
- `kernel.cu`: GPU `vectorAdd` kernel

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
./build/test
```

Or use Ctrl+Shift+B in VS Code.

The build selects `nvcc` from your PATH. If you change CUDA installations, reset
the cached compiler with `cmake --fresh -S . -B build -DCMAKE_BUILD_TYPE=Debug`.
You can select a specific toolkit with `-DCMAKE_CUDA_COMPILER=/path/to/nvcc`.

## Debug

Set a breakpoint inside `vectorAdd` and press F5. `launch.json` intentionally has no `preLaunchTask`: F5 debugs the existing executable without rebuilding.

Note: CUDA architecture is set to 89 (Ada). Change `CMAKE_CUDA_ARCHITECTURES` to match your GPU if needed.
