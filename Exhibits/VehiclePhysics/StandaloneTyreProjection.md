# Standalone demo tyre deformation and surface projection

## Scope and ownership

The 160-particle XPBD and 128-cell Hex8 demo models already lived in
`Frontier/Engine/Physics/TyreContinuum.{h,cpp}`. They now have a standalone C++17 CMake library,
`FrontierTyreContinuum`, that does not require a vehicle solver or Jolt. Existing spring-style and
continuum **visual vertex deformation** were separated from `TyreDeformation.h` into the
header-only `TyreSurfaceProjection.h`; the vehicle-state adapter remains in the old header and
includes the shared projection. This is a dependency/ownership extraction, not a new tyre solver.

A later [GPU XPBD-labelled projection slice](TyreProjectionDevice.md) adds an Engine compute shader and
caller-owned Vulkan recorder; this does not yet link the particles to the editor or a tyre mesh renderer.

The demo's spring contact-force law is still coupled to the existing `VehicleSolver.cpp`; it was not
extracted or changed here. The separate demo continuum shell has not been ported. The older
84-particle lattice is a different model, not the demo's 160-particle XPBD. No vehicle dynamics,
Project Drive or Jolt sources were changed.

## Evidence

- `Results/StandaloneTyre-{release,sanitized}.txt`: 16/16 native checks, including 12 cases × 8
  steps for each unchanged demo JavaScript model. Prescribed reaction and XPBD positions match;
  worst solid local-position error is `2.71e-20 m`. This is source-behaviour comparison, not an
  independent physical measurement. The library and projection probe have no vehicle/Jolt include
  or unresolved-symbol dependency.
- The same runner executes the actual retained `VehicleVisual.wgsl` projection function (Git blob
  `22359e7e799da472c7cb4e6a7d518458278c5813`) on **SwiftShader CPU Vulkan**, not a physical GPU.
  Eleven cases × 2,048 vertices cover spring pressure, angle and deflection, synthetic and original
  demo XPBD/solid nodal displacement fields, non-tyre vertices, the unloaded branch and shredded
  rubber. The maximum absolute difference from the native projection was `9.69171524e-5 m` in
  position and `5.72428107e-5` in a normal component (acceptance limits `3e-4` each). The continuum
  position maximum was `5.96046448e-8 m`. The nearly-zero spring case retains approximately the
  same position discrepancy, so this is **not** bitwise GPU parity and its precise numerical cause
  has not been established.
- Each of those 22,528 samples also compares native output byte-for-byte against the actual previous
  Engine `TyreDeformation.h` at commit `9388c96` (header SHA-256
  `22df4c1e0f5479e67f57191cc733152d319dc67b3900e6947423be9042d39358`). The extraction did not
  change those sampled CPU results.
- `Results/TyreAdapterRegression-release.txt` records the unchanged vehicle adapter's existing
  reference tests: 3,480 prescribed-pressure spring samples and 180 mode-change advances pass.
  This is a compatibility regression, not permission to port or alter the vehicle solver.

Earlier extended hard-drop divergence and timestep non-convergence remain unresolved; see
`Continuation.md`. These tests do not establish tyre material calibration, continuous-contact
convergence, arbitrary input robustness, physical-GPU results, engine/editor integration or
vehicle/Jolt parity.

## Reproduce

Prerequisites: Node.js, CMake/CTest, a C++17 compiler, `git` with checkpoint `9388c96`, NumPy,
`wgpu` and the repository's configured Vulkan/SwiftShader tools. Defaults use
`_ClaudeScratch/venv` and `ExternalPackages/VulkanCompute`; set `PYTHON` or `VULKAN_TOOLS` to
other prepared locations. The runner writes generated fixtures/builds to `_ClaudeScratch`.

```sh
Exhibits/VehiclePhysics/RunStandaloneTyreValidation.sh
SANITIZE=1 Exhibits/VehiclePhysics/RunStandaloneTyreValidation.sh
```

CMake/CTest also builds and runs just the native JavaScript/Engine continuum comparison:

```sh
cmake -S Exhibits/VehiclePhysics -B _ClaudeScratch/build/tyre-standalone -DCMAKE_BUILD_TYPE=Release
cmake --build _ClaudeScratch/build/tyre-standalone -j2
ctest --test-dir _ClaudeScratch/build/tyre-standalone --output-on-failure
```
