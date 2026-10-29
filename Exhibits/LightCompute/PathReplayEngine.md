# Engine-owned bounded replay domain

The formerly exhibit-local replay intake is now authoritative in `Frontier/Engine/Rendering/`:

- `PathReplayFootprint.h` defines the C++17-compatible 144-byte receiver and facet records.
- `PathReplayDomain.{h,cpp}` validates the bounded BVH, material atlas and receiver inputs; compiles the
  emissive-facet index list; and owns the exact input snapshots used by the existing history-cap policy.
- `FrontierPathReplayDomain` is a standalone C++20 CMake library. Its scene view is non-owning: the caller
  must keep the pointed-to arrays alive and immutable during each call. Domain failures throw
  `std::runtime_error`; a changed input caps retained history at one, while an unchanged input caps it at 64.
  The full-history switch is an audit control, not a recommended dynamic-scene policy.
- The exhibit adapts its existing archive to the Engine view and links the Engine library. The old
  exhibit-local domain implementation was removed. The replay shader and its normal Vulkan command recorder
  now reside under Engine; allocation, descriptors, pipeline creation, submission, fixture generation and
  validation remain in the exhibit.

This is an ownership and reuse checkpoint, **not** an editor integration or a complete ReSTIR GI pipeline.
The policy still snapshots inputs on the CPU and is not a scalable production scene-revision system. Its
1/64 cap is a heuristic, not an unbiasedness guarantee. Previous limits on material support, reprojection,
finite random streams and physical-GPU validation remain in force.

## Caller-owned Vulkan recorder

`LightComputeFootprint.h` supplies the shared 128-byte push-constant ABI and `PathReplayVulkan.{h,cpp}` records
optional primary visibility, replay generation and resolve. It also supports an opt-in
[bounded temporal colour reconstruction](ReplayReconstruction.md) with two more pipelines. It inserts compute
dependencies **between** those stages, but deliberately does not submit, wait, transition resources, present, or
insert the final consumer barrier. The caller must provide input and output synchronization, queue ownership,
properly initialized histories, descriptor and resource lifetimes, matching pipelines and non-aliasing storage.
The regular LightCompute replay path now calls this recorder; diagnostic-only probe and tail passes still use
exhibit-local scheduling. The renderer does not currently call it.

Use CMake target `FrontierPathReplayVulkan`, enabled by setting `FRONTIER_RENDERING_VULKAN_HEADERS` to a Vulkan
header include directory before adding `Frontier/Engine/Rendering`. The consumer supplies Vulkan function
pointers, device workgroup/push-constant limits, pipeline layout, descriptor set and compute pipelines. The
layout must expose bindings 0–7 as in `LightInfluence.slang` and a compute-stage 128-byte push range at offset 0.
The pipeline entry points are `TracePrimarySurfaces` (optional), `GenerateReplayReservoirs` and
`ResolveReplayReservoirs`; `Primary` writes receiver records, and resolved colour goes to output records `[0, N)`.
Opt-in reconstruction adds `GatherReplayReconstruction` and `FinalizeReplayReconstruction` from the separate
reconstruction shader. The replay environment contributes to escaping indirect paths, but the recorder does not enable the separate
primary sky-map flags: primary misses are black unless the caller handles background composition separately.
All eight descriptor bindings must satisfy the actual Vulkan pipeline/layout contract, even for unused resources.

For this recorder, the push fields are:

| Field | Replay interpretation |
|---|---|
| `Dimensions` | `x = N` receivers; `y = 0` (analytic lights are not integrated); other components unused here |
| `Minimum` | Nonnegative RGB uniform environment, each at most 256; `w` must be **bitwise** zero (no fog payload) |
| `Maximum` | `x =` positive replay target floor in `[0.0001, 10]`; `y =` bit-cast `uint32_t` offset of the optional source table in `Angles`; `w` must be bitwise zero (no distance-field payload) |
| `Grid` | `w =` enclosure count |
| `Sampling` | `{seed, initial candidates, facet count, stack limit}`; initial candidates 1–4, legacy zero means one |
| `Sampling.w` | Stack limit 1–64 |
| `History` | `{width, height, sequence index, reset}` with `width × height = N`; the caller controls/reset-preserves the GPU histories |
| `Reconstruction` | `{temporal reuse, spatial reuse, source count, history cap}`; reuse values are 0/1 and cap is 1–64. This is reservoir reuse, **not** a general temporal denoiser |
| `Primary` | `{enabled, camera float offset, bounce count, option bits}`; enabled is 0/1, bounce count is 1–8. Named option bits are in `PathReplayVulkan.h`; the double-count and strict-motion bits are audit controls |

`PathReplayVulkanBuffers` contains **descriptor-range** lengths in records, not allocation byte sizes. Bind at
least `N` receivers, the declared enclosures/facets, `PathReplayCounterWords` uints and
`PathReplayRequiredOutputRecords(N)` float4s. The output formula includes the shared tail and five replay
slots; the recorder also checks `PathReplayMaxReceivers` for shader-`uint` overflow. Reconstruction requires
`PathReplayRequiredReconstructedOutputRecords(N)` and 24 counter words, with its own lower uint-index limit.
The device's storage-range, allocation and dispatch limits can be much lower. Camera storage needs 48 floats
starting at `Primary.y` not
only when primary visibility is enabled but also when textured secondary rays are enabled. Source-table and
camera arithmetic are checked against the 32-bit shader index range. The weighted table has one header float
and four floats per source; the uniform table has one float per source.

The recorder's guards check ranges, dispatch bounds, option combinations and a few scalar constraints; they do
**not** validate GPU buffer contents, descriptor type/offset correctness, BVH links, material/texture data,
source weights, camera matrices or actual device capabilities. Run `ValidatePathReplayDomain` and compile the
source list/CDF for the exact uploaded scene, maintain valid revisions/history decisions, and validate all other
resource and Vulkan requirements. Validation only applies to the bounded opaque domain. Software triangle
traversal, limited rough specular transport and existing fixture filters are not hardware RT, a separate finished
reflection system, unified PBR, or a complete reconstruction/presentation pipeline. A candidate-count change is a
sampling revision: use canonical 1–4 in `PathReplayHistorySpecification` even if the GPU uses legacy zero for one.
See [initial candidate tests](ReplayCandidates.md) for history caps, colour resets and numerical limitations. A
separate default-off [strict diffuse camera mode](ReplayCameraReprojection.md) requires a validated camera-only
transition and exact matching points; the recorder cannot certify that condition from GPU buffers.

## Verification

- At the Engine-promotion checkpoint an independent consumer checked the ABI, source index compilation,
  seven malformed-domain cases, nine revision axes, in-place atlas mutation, the audit override and invalidation.
  Its release and ASan/UBSan builds passed CTest. The later [GGX proposal](ReplayImportance.md) adds another
  domain rejection and revision-axis check.
- A separately compiled C++17 consumer reproduces 864 bytes of the previous archive layout and reloads them.
  The existing C++17 VehicleCapture also builds and exports its 28,276-facet fixture using the shared footprint.
- The Engine library also builds standalone. The recorder mock checks both stage sequences and barriers,
  padded dispatch, the full push payload, a valid weighted-source span, the largest legal uint-indexed output
  and 28 fail-before-recording cases, including an excessive initial-candidate count, reconstruction resource
  limits, overflow and a missing textured-secondary camera.
- Moving-radiant 128×160 readbacks cover eight frames per state and first-transition images. On SwiftShader CPU
  Vulkan the Engine recorder's 14 PNGs matched the pre-change exhibit scheduler byte-for-byte in release and
  ASan/UBSan-host builds; the external Vulkan driver and GPU shaders are not sanitizer-instrumented. Camera,
  rigid-object and blocker replay checks also passed. This preserves bounded fixture behavior, not physical-GPU
  validation, general denoising, real-world material calibration, or a runtime-performance claim.

```bash
bash Exhibits/LightCompute/RunPathReplayEngine.sh
SANITIZE=1 bash Exhibits/LightCompute/RunPathReplayEngine.sh
cmake -S Frontier/Engine/Rendering -B _ClaudeScratch/build/path-replay-engine-standalone
cmake --build _ClaudeScratch/build/path-replay-engine-standalone
RESULTS="$PWD/_ClaudeScratch/engine-replay-vulkan.txt" bash Exhibits/LightCompute/RunRoughTextureReplay.sh
cmake -S Exhibits/LightCompute -B _ClaudeScratch/build/replay-recorder -DCMAKE_BUILD_TYPE=Release
cmake --build _ClaudeScratch/build/replay-recorder --target PathReplayVulkanValidation
ctest --test-dir _ClaudeScratch/build/replay-recorder --output-on-failure -R '^PathReplayVulkanValidation$'
OUTPUT="$PWD/_ClaudeScratch/replay-capture" IMAGES="$PWD/_ClaudeScratch/replay-images" \
  RESULTS="$PWD/_ClaudeScratch/replay-capture.txt" bash Exhibits/LightCompute/RunReplayFacetMixtureMotionCapture.sh
# Add SANITIZE=1 for the host ASan/UBSan build.
```

[Engine release](Results/PathReplayEngine-release.txt) · [Engine sanitized](Results/PathReplayEngine-sanitized.txt) ·
[Recorder release and baseline comparisons](Results/PathReplayVulkan-release.txt) ·
[Recorder sanitized host](Results/PathReplayVulkan-sanitized.txt) ·
[C++17 vehicle export](Results/PathReplayEngine-cxx17.txt) ·
[Vulkan release](Results/PathReplayEngine-vulkan-release.txt) ·
[Vulkan sanitized host](Results/PathReplayEngine-vulkan-sanitized.txt) ·
[Replay regressions](Results/PathReplayEngine-regression.txt) ·
[Existing rough-texture scope](RoughTextureReplay.md).
