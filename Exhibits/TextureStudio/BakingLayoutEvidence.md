# Baking layout verification

## Results

| Check                                      | Result |
| ------------------------------------------ | ------ |
| Native tile selection, square geometry     | Passed |
| Batch queue toggle and independent selection | Passed |
| Mesh Maps / Scene Outliner tab switching   | Passed |
| Shared C++ front-distance slider interaction | Passed |
| Selected normal map bake and PNG export    | Passed |
| Existing CPU / field bake comparison        | Passed: exact 27-map Teapot and Cube comparisons |
| Native Volume SDF                         | Subsequent work: see `VolumeDistanceEvidence.md` |
| Imported geometry, 16K                   | Not implemented |
| Windows desktop and Vulkan execution        | Not tested |

The captures are generated from native ImGui draw lists with the CPU evidence rasterizer, not a Windows desktop capture:
`Evidence/BakingSquareMaps.png`, `Evidence/BakingOutliner.png`, and `Evidence/BakingSingleMap.png`.

## Reproduction

Host: Linux x86-64, GCC 12.2, Release, tested against `71e4a17` with the uncommitted changes in this workspace.
Prepare dependencies and fonts using `Automation/PrepareUI.py` and `Automation/PrepareFonts.py`, then:

```sh
cmake -B build -G Ninja -DFRONTIER_HOST_VERIFICATION=ON -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BakingLayoutEvidence BakingWorkspacePanelConstructionValidation BakeFieldValidation
./build/BakingWorkspacePanelConstructionValidation
./build/BakingLayoutEvidence /tmp/baking-layout-evidence
./build/BakeFieldValidation /tmp/bake-field-evidence
```

`BakingLayoutEvidence.cpp` emits PPM images; the retained PNGs are converted from those PPMs. The existing
native 2D CPU and field bakers were not changed. The subsequent SDF work is described in `VolumeDistanceEvidence.md`.
