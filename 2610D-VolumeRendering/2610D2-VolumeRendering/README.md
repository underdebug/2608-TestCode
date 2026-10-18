# DICOM volume rendering with Vulkan

The application loads `/home/roots/develop/data/DICOM` by default. Pass a different
DICOM directory as the first argument. The directory must contain one regularly
spaced series of single-frame, 16-bit monochrome slices; filenames do not need
extensions. GDCM decodes the pixels, sorts slices using Image Position Patient,
and applies each slice's rescale slope and intercept. Mixed series, incompatible
dimensions, and nonuniform slice spacing are rejected.

`VolumeRendering` derives from `VulkanWindow`. It uploads signed 16-bit modality
values packed into a Vulkan storage buffer. Values outside the signed 16-bit
range are clamped. The fragment shader casts orthographic rays through the volume,
uses trilinear interpolation, and composites a CT tissue transfer function from
front to back with opacity correction for sampling distance. Voxel spacing sets
the physical box dimensions in millimeters. The box is centered in slice-local
coordinates; axes are not patient anatomical orientation labels.

The default Vessels preset uses a 275/350 HU window, gold/red contrast-filled
vessels, faint cool tissue context, and a black background. The General button
restores the broad tissue opacity ramp. Window center/width move the vessel
selection range; opacity controls extinction per millimeter. This intensity-based
preset also highlights bone and calcification at similar HU; it does not segment
coronary arteries or reproduce cinematic rendering from non-contrast data.

The QML panel controls window center, window width, opacity, rotation, tilt and
camera distance. Shift/left-drag pans, Ctrl/right-drag rotates, and the wheel
zooms around the cursor. Alt navigation, coordinate picking, axes, and rulers
are inherited from `VulkanWindow`. Depth picking uses the first ray sample where
accumulated opacity exceeds 0.1.

## Build and run

Requires Linux/X11, Qt 6.4+ Widgets/Quick/QuickControls2, Vulkan, glslangValidator,
and GDCM development libraries (`libgdcm-dev` on Ubuntu). The original build also
retains its CUDA toolkit, OpenGL/GLUT/GLEW/GLFW and OpenMesh/Open3D dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
QT_QPA_PLATFORM=xcb ./build/test /home/roots/develop/data/DICOM
```

For a copied build directory with an old source path, configure with
`cmake --fresh -S . -B build -DCMAKE_BUILD_TYPE=Debug`.

To decode and validate the entire series without opening a window:

```bash
./build/test /home/roots/develop/data/DICOM --validate
```

The supplied data loads as 512 × 512 × 256 with spacing
0.365234 × 0.365234 × 0.5 mm. Its packed GPU buffer requires 128 MiB; the renderer
checks the device's maximum storage buffer range before upload.

Shader files are compiled with debug information and optimization disabled, and
copied to `build/shader`. Keep that directory beside the executable when moving
it. `overlay.frag` is the color fragment shader for the axes/ruler
overlay; the volume uses `volume.vert` and `volume.frag`.

Validation: the C++ application builds, both volume shaders pass `spirv-val`, and
the complete supplied DICOM series passes the loader validation. Interactive
rendering could not be verified in the development session because Qt could not
connect to its X display (`:0`).
