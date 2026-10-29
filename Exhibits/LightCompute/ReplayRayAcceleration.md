# Dual-Path Ray Acceleration (`VK_KHR_ray_query` + SAH-2 Ordered BVH) — 2026-10-28

Engine `VK_KHR_ray_query` / `VK_KHR_acceleration_structure` negotiation, BLAS/TLAS build recorder, SPIR-V 1.4
inline ray-query kernels, 16-bin SAH-2 BVH construction, and ordered software traversal for replayed GI.
The [later 32x32 regression](Results/ReplayRayAcceleration-regression.txt) repeated the measured operation counts
and same-tree parity after the PBR transport changes. SwiftShader is CPU Vulkan, not physical RT hardware.

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
     while endpoint-excluded NEE rays use `RAY_FLAG_FORCE_NON_OPAQUE` to inspect candidate triangles. Authored
     thin-sheet handling and its limits are documented in [ReplayTransmission.md](ReplayTransmission.md).
2. **Tier 1 — 16-Bin SAH-2 BVH + Front-to-Back Ordered Traversal (`PathReplayOrderedTraversal = 8388608u`):**
   - `TriangleVisibilitySpace::BuildSurfaceAreaHierarchy(..., MaxLeafFacets = 2)` evaluates `16` centroid bins per axis
     via linear prefix/suffix AABB sweeps under cost
     `C_split = 1.0 + 1.5 * (N_L * A_L + N_R * A_R) / A_P` with bounded depth (`<= 24` + median fallback) and `1`-ULP
     conservative float32 bounds certified by `ValidatePathReplayDomain`.
   - Closest-hit `NearestFacet` evaluates both child slabs (`IntersectsBoxDistance`), steps immediately into
     `NearChild` (`LeftNear <= RightNear`), pushes only `(FarChild, FarNear)` (halving stack depth), and discards stale
     subtrees in registers on pop whenever `StackNear > Strike.x`.
   - Fixed-distance NEE shadow `ReplaySegmentTransmittance` uses lazy LIFO dominant-axis octant ordering so
     ray-ward blockers are visited first without eager sibling AABB loads on early-exit occluded rays.

## Quantitative Verification (`RunReplayRayAcceleration.sh`)

| Fixture / metric                             | Median-8, unordered   | SAH-2, ordered      | Result                 |
| -------------------------------------------- | --------------------- | ------------------- | ---------------------- |
| Historical host CTests, Release / ASan+UBSan | 7/7                   | 8/8                 | Historical suites pass |
| Ray-query SPIR-V 1.4 opcode audit            | Absent                | 4/4                 | Not hardware execution |
| 20k closest/shadow rays, triangle tests      | 814,628               | 35,056              | 95.70% fewer           |
| 32x32 driving, same-tree max RGB difference  | -                     | 0                   | Exact tested output    |
| 32x32 rough GGX, triangle tests              | 37,892                | 28,884              | 23.8% fewer            |
| 32x32 single-source, cross-tree max RGB      | -                     | 0                   | Exact tested output    |
| 32x32 urban, box / triangle tests            | 961,558 / 1,292,034   | 478,276 / 39,816    | 50.26% / 96.92% fewer  |
| 64x64 urban, box / triangle tests            | 4,283,487 / 5,739,088 | 2,152,053 / 179,221 | 49.76% / 96.88% fewer  |
| 64x64 urban, total box + triangle tests      | 10,022,575            | 2,331,274           | 76.74% fewer           |
| 64x64 urban, RMSE vs 384-sample render       | 0.0699                | 0.0667              | Finite comparison      |

## Boundaries & Honest Hardware Scope

- SwiftShader (`Vulkan 1.3.0`) is a CPU Vulkan implementation that does not expose `VK_KHR_acceleration_structure` or
  `VK_KHR_ray_query`; `SelectPathReplayAccelerationTier` detects this at runtime and selects `OrderedSoftwareBvh`
  (`FallbackReason = "Device lacks VK_KHR_acceleration_structure or VK_KHR_ray_query"`). Physical RT-core frame-time
  measurements require a capable physical GPU; the reductions above are deterministic shader AABB and triangle
  test counts on SwiftShader Vulkan, not measured frame-time speedups.
- Visual and per-pixel `Slot 9` traversal heatmap:
  [ReplayRayAccelerationComparison.png](Images/ReplayRayAccelerationComparison.png)
  (`SHA-256: e317731328b11e96123ac26e7db3885eeefe027d627c4d59ae012316cc886415`), reproducible via
  `CAPTURE=1 bash Exhibits/LightCompute/RunReplayRayAcceleration.sh`
  (`Exhibits/LightCompute/Results/ReplayRayAcceleration-release.txt`).
