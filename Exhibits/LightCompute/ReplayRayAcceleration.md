# Dual-Path Ray Acceleration (`VK_KHR_ray_query` + SAH-2 Ordered BVH) — 2026-10-28

🧩 Engine `VK_KHR_ray_query` / `VK_KHR_acceleration_structure` negotiation, BLAS/TLAS build recorder, SPIR-V 1.4 inline
ray-query kernels, 16-bin SAH-2 BVH construction, and front-to-back child-ordered software traversal for ReSTIR GI.

## Mathematical & Architectural Formulation

Real-time game street scenes combine large architectural slabs (`20+ m` roadways, sidewalks, canyon facades, overpass
decks) with compact foreground occluders (`~1.8 m` vehicles and `~0.7 m` pillars). Legacy `TriangleVisibilitySpace`
stopped at `8`-triangle median leaves and traversed internal nodes in fixed LIFO `Upper`-before-`Lower` order without
entry-distance culling, causing severe AABB overlap (`~75` triangle Woop-Benthin-Wald tests per ray).

Phase 3 introduces a unified three-tier ray-acceleration architecture in `Frontier/Engine/Rendering/`:

1. **Tier 2 — Hardware Inline Ray Query (`PathReplayHardwareRayQuery = 16777216u`):**
   - `PathReplayRayQuery.{h,cpp}` inspects physical-device `VkExtensionProperties` + Vulkan `1.2+` API level + enabled
     feature bits (`VK_KHR_acceleration_structure`, `VK_KHR_ray_query`, `VK_KHR_deferred_host_operations`,
     `VK_KHR_buffer_device_address`), packs contiguous `16`-byte-aligned BLAS vertex buffers from `PathReplayFacet`,
     creates double-sided identity TLAS instances (`VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR`), queries
     exact aligned scratch sizes via `vkGetAccelerationStructureBuildSizesKHR`, validates pairwise-disjoint aligned
     `VkDeviceAddress` intervals, and records `BLAS -> Barrier -> TLAS -> ComputeBarrier`.
   - `Shaders/PathReplayRayQuery.slang` compiles `TracePrimarySurfaces`, `GenerateReplayReservoirs`,
     `ResolveReplayReservoirs`, and `FinalizeReplaySampleHistory` to SPIR-V `1.4` (`spvRayQueryKHR`) sharing 100% of
     the ReSTIR GI / NEE / MIS / seed-bank / Vogel-disk code: closest-hit rays use `RayQuery<RAY_FLAG_FORCE_OPAQUE>`,
     while endpoint-excluded NEE shadow rays use
     `RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE>` and commit hits iff
     `CandidatePrimitiveIndex() != Endpoint && CandidateTriangleRayT() in (0, Distance)`.
2. **Tier 1 — 16-Bin SAH-2 BVH + Front-to-Back Ordered Traversal (`PathReplayOrderedTraversal = 8388608u`):**
   - `TriangleVisibilitySpace::BuildSurfaceAreaHierarchy(..., MaxLeafFacets = 2)` evaluates `16` centroid bins per axis
     via linear prefix/suffix AABB sweeps under cost
     `C_split = 1.0 + 1.5 * (N_L * A_L + N_R * A_R) / A_P` with bounded depth (`<= 24` + median fallback) and `1`-ULP
     conservative float32 bounds certified by `ValidatePathReplayDomain`.
   - Closest-hit `NearestFacet` evaluates both child slabs (`IntersectsBoxDistance`), steps immediately into
     `NearChild` (`LeftNear <= RightNear`), pushes only `(FarChild, FarNear)` (halving stack depth), and discards stale
     subtrees in registers on pop whenever `StackNear > Strike.x`.
   - Fixed-distance NEE shadow `ReplaySegmentOccluded` uses lazy LIFO dominant-axis octant ordering so ray-ward
     blockers are visited first without eager sibling AABB loads on early-exit occluded rays.

## Quantitative Verification (`RunReplayRayAcceleration.sh`)

| Suite / Scene | Metric | Legacy Median-8 Unordered | Engine SAH-2 + Ordered | Improvement / Parity |
|---|---|---:|---:|---:|
| Host CTests (`Release` & `ASan+UBSan`) | Passed suites / Recorder rejections | `7/7` (`43` guards) | `8/8` (`47` + `10` AS guards) | `100%` pass (`0` leaks/UB) |
| SPIR-V `1.4` `SPV_KHR_ray_query` Binary Audit | Opcodes `4472 / 5341 / 4473 / 4476 / 4477` | Not present | `4/4` `.spv` verified | `0` `slangc` warnings |
| `10k` Closest + `10k` NEE Shadow Oracle (`240` tris) | Triangle `FacetStrike` evaluations | `814,628` | `35,056` | **`-95.70%` (`23.2x` fewer)** |
| `32x32` Diffuse `4`-Frame Subpixel Driving (`276` tris) | Same-tree `|Ordered - Legacy|` Max RGB | — | `0.000000` | **Bit-exact (`Slot 9 == Counters`)** |
| `32x32` Wet-Asphalt Rough GGX (`alpha >= 0.28`) | Same-tree Max RGB / SAH Tri Tests | `37,892` tris | `0.000000` / `28,884` tris | **Bit-exact (`-23.8%` extra tris)** |
| `32x32` Single-Luminaire Cross-BVH Check (`241` tris) | `|SAH-2 Ordered - Median-8 Legacy|` | — | `0.000000` | **Bit-exact across BVH rebuild** |
| `32x32` Urban Overpass `4`-Frame GI + SVGF (`276` tris) | GPU Box / Triangle evaluations | `961,558` / `1,292,034` | `478,276` / `39,816` | **`-50.26%` boxes / `-96.92%` tris** |
| `64x64` Urban Overpass `6`-Frame Capture (`76,852` rays) | GPU Box (`Counters[18]`) / Tri (`[19]`) | `4,283,487` / `5,739,088` | `2,152,053` / `179,221` | **`-49.76%` boxes / `-96.88%` tris** |
| `64x64` Urban Overpass `6`-Frame Capture (`276` tris) | Total Ops (`Box+Tri`) / RMSE vs `384`-spp | `10,022,575` / `0.0699` | `2,331,274` / `0.0667` | **`-76.74%` (`4.30x` fewer ops)** |

## Boundaries & Honest Hardware Scope

- SwiftShader (`Vulkan 1.3.0`) is a CPU Vulkan implementation that does not expose `VK_KHR_acceleration_structure` or
  `VK_KHR_ray_query`; `SelectPathReplayAccelerationTier` detects this at runtime and selects `OrderedSoftwareBvh`
  (`FallbackReason = "Device lacks VK_KHR_acceleration_structure or VK_KHR_ray_query"`). Physical RT-core frame-time
  measurements require an RTX / RDNA / Intel Arc GPU; all reported speedups above are deterministic GPU shader AABB and
  triangle intersection reductions verified on SwiftShader Vulkan.
- Visual and per-pixel GPU `Slot 9` traversal heatmap proof: `Exhibits/LightCompute/Images/ReplayRayAccelerationComparison.png`
  (`SHA-256: e317731328b11e96123ac26e7db3885eeefe027d627c4d59ae012316cc886415`), reproducible via
  `CAPTURE=1 bash Exhibits/LightCompute/RunReplayRayAcceleration.sh`
  (`Exhibits/LightCompute/Results/ReplayRayAcceleration-release.txt`).
