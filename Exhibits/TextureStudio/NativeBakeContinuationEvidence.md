# Native bake continuation evidence

The Linux headless ImGui draw-list capture `Evidence/VolumeIsosurface.png` shows the baked teapot's CPU ray-cast isosurface and volume export control. It is not a Windows desktop screenshot. Dragging rotates the 3D view; the 2D tab retains the axis-aligned slice. The coarse nearest-voxel ray caster is a preview, not the prototype's full renderer.

`VolumeDistanceFieldValidation` verifies anisotropic cube distances, signs, open surfaces, narrow band and the raw NRRD header/payload. `BakeSurfaceImportValidation` checks OBJ groups, triangulation, negative indices, UV seams, UV-required rejection, ASCII/binary STL and name normalization. `BakingLayoutEvidence` tests the volume-export button, low import drop dispatch, volume invalidation, inactive-tab rejection and prior square-map/single-bake behaviour. The actual OS file picker and GLFW drop callback are not tested on Windows here.

NRRD export stores little-endian Float32 distances in X-fastest order with all three voxel spacings and voxel-centre origin; confidence is not exported. Native custom resolution is square up to 16384. Padding, per-map overrides, and large images use the disk-backed CPU tile path; a GPU refusal falls back to that path rather than silently reducing resolution. Name matching supports sequential GPU requests per target and CPU fallback. `BakingMatchingValidation` exercises the real panel state machine with a CPU field-backed mock GPU: two different target/source pairs, GPU failure on the second target, full CPU restart, and bit-identical normal/alpha output. It also checks the volume-plus-maps batch and a nearest-surface projection fixture. This is not a real Vulkan or Windows GPU test.

Reproduce using the existing host-verification build:

```sh
.venv/bin/cmake --build build --target BakingMatchingValidation BakeSurfaceImportValidation VolumeDistanceFieldValidation BakingLayoutEvidence
.venv/bin/ctest --test-dir build -R 'BakingMatchingValidation|BakeSurfaceImportValidation|VolumeDistanceFieldValidation' --output-on-failure
./build/BakingLayoutEvidence _ClaudeScratch/bake-evidence
```

The SDF parity rays now use a conservative bounding hierarchy; the 24 × 18 × 20 cube comparison against the browser remains within 1.785e-7. The hierarchy accelerates ray intersection only; surface seeding and large volume grids may still be expensive.

## Disk-backed 16K and per-map controls

A later pass added CPU field tiles with a padding halo, temporary raw images and streaming RGBA PNG output. `BakeFieldTilingValidation` compares all 27 unpadded maps against a full-frame 65-pixel bake, tests a 259-pixel partial-tile disk bake and PNG decode, independently compares two-pixel padded tile seams, and checks map-specific output and wire-width overrides. `Bake16kStress` completed an actual 16384 × 16384 sparse-coverage bake and streamed a full-size PNG, with two-pixel padding; it checked the raw byte count and PNG dimensions. This is one sparse fixture, not a performance claim for dense 16K maps. The 16K route is CPU and disk intensive, disk-backed PNG exports run in the background, and results use a reduced in-app preview. Nearest-surface projection is currently limited to 1024, with zero padding and no per-map bake overrides. The prototype's entire map-property schema is not yet ported: native maps now have independent RGB output controls and the native evaluator's existing map-specific parameters can be overridden individually.

```sh
cmake --build build --target BakeFieldTilingValidation Bake16kStress
./build/BakeFieldTilingValidation
./build/Bake16kStress  # writes approximately 2 GiB temporarily, and may take time
```
