#!/bin/sh
# Build, denoise the bundled image, and report quality.
set -eu
cd "$(dirname "$0")"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/test
./build/psnr clean.pgm noisy.pgm
./build/psnr clean.pgm denoised.pgm
