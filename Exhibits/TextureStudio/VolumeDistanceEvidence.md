# Volume distance field verification

| Check | Result |
| --- | --- |
| Native build, GCC 12.2, Linux x86-64, Release | Passed |
| 24 × 18 × 20 cube, signed and unsigned | Passed |
| Open triangle audit and narrow-band clamp | Passed |
| Native vs browser cube distances | 8,640 voxels; maximum absolute difference 1.785e-7 |
| Native UI: SDF selection, asynchronous bake, slice and tilted-slice view | Passed using headless ImGui interaction |
| High source inclusion toggle | Passed using headless ImGui interaction |
| Windows desktop and Vulkan | Not tested |

The retained `Evidence/VolumeSlice4.png` and `Evidence/VolumeSlice5.png` are CPU-rasterized native ImGui captures,
not Windows desktop captures. The tilted view is a rotatable slice plane, not the HTML prototype's full 3D volume
renderer. The comparison covers one cube only and does not establish parity for all meshes or settings.

## Reproduction

Prepare the pinned dependencies and fonts with `Automation/PrepareUI.py` and `Automation/PrepareFonts.py`.
Configure with CMake, `FRONTIER_HOST_VERIFICATION=ON`, `GLFW_BUILD_WAYLAND=OFF`, `GLFW_BUILD_X11=OFF` and
`CMAKE_BUILD_TYPE=Release`. The tested tree includes uncommitted workspace changes.

```sh
cmake --build build --target VolumeDistanceFieldValidation BakingLayoutEvidence
./build/VolumeDistanceFieldValidation
./build/BakingLayoutEvidence _ClaudeScratch/tmp/volume-ui
c++ -std=c++20 -O2 -I. Exhibits/TextureStudio/Code/VolumeComparison.cpp \
  Engine/ContentInterchange/VolumeDistanceField.cpp Engine/ContentInterchange/BakeMesh.cpp \
  -o _ClaudeScratch/tmp/volume-comparison
_ClaudeScratch/tmp/volume-comparison > _ClaudeScratch/tmp/volume-comparison.json
node Exhibits/TextureStudio/Code/VolumeComparison.js _ClaudeScratch/tmp/volume-comparison.json
```

The native SDF uses triangle-distance seeds, an anisotropic separable Euclidean distance transform and three-axis
parity. It currently evaluates parity against all triangles rather than the browser's ray buckets, so large meshes
and grids may be slow. It bakes separately from the 2D batch queue and has no volume export yet.
