# Real-mesh lighting: implementation plan

## Decision and evidence

Use the same indexed world triangles, skinned positions, material IDs and instance transforms for visibility, indirect lighting and reference renders. Do not fit limbs with capsules or bake voxel distance fields. A surfel is a lighting sample attached to a triangle, not replacement character geometry. Keep the offline SDF volume baker; it is not part of frame rendering.

Baseline: `SultanAladin/Frontier@daed305`, after `63db120` retired the old runtime. Read `Exhibits/CleanEmissiveGiCandidates.h` and its validator at `2d6f15e`; `Exhibits/CleanShadowAndGiCandidates.h` and `CleanEmissiveShadowValidation.cpp` at `536cfe1`. Do not copy their removed engine dependencies back. The JSON summaries under `Exhibits/Images/CleanEmissive{Gi,Shadows}` at `536cfe1` are historical evidence, not GTX measurements:

| 560×315 GI candidate | Normal GI RMSE | Probe output RMSE |
|---|---:|---:|
| Stokes contour | .2238 | .2238 |
| Surfel cascade | .2239 | .2269 |
| Continuous voxel field | .2259 | .2262 |

Advance Stokes for direct emitter detail and surfels for amortized lookup. The normal GI results are effectively tied on one vehicle scene; the field even beats surfels on the probe metric. For shadows the capsule/surfel experiment scored .2973 vs .2983 hybrid and .4170 old SDF. Neither its timings nor its ranking transfer to skinned triangles unchanged.

## Proposed layout

```
Engine/
  SpatialAcceleration/                       # shared triangle traversal and refit
  GeometricRaster/
    DirectLighting/SolarShadowSequence.*     # existing shadow selection
    GlobalIllumination/
      StokesContourGiSequence.{h,cpp}         # proposed
      SurfelCascadeGiSequence.{h,cpp}         # proposed
      DeterministicPathTraceSequence.{h,cpp}  # proposed
  Shaders/GlobalIllumination/                 # matching compute passes
Experimental/{ShadowAlgorithmLab,DeterministicPathTracer}/  # existing prototypes
Exhibits/WorldGiShadowPathTraceValidation.cpp # proposed regression scene
```

## Delivery gates

1. **Mesh contract and shadows.** Update skinning before rebuilding/refitting per-mesh bounds; instance rigid meshes without resampling. Select ContactPcss as the GTX starting point and WideTreeRayQuery for quality. Benchmark them against PcssPlaneAware, ShadowRayMarch and PcfBilinear. `Frontier-1@d5bbb81` actually has eight C++ enum entries: the other three use voxel SDF. If a sixth SDF-free mode is needed, prototype triangle-attached blocker surfels plus short exact contact rays; retain only if measured competitive.
2. **GTX GI.** Build a CPU reference first. For Stokes, clip real emissive triangles to the receiver hemisphere, integrate their projected solid angle and test visibility with triangle rays; accumulate diffuse patches for a second bounce. For surfels, bind samples by triangle ID and barycentrics, update positions/normals after skinning, distribute bounced radiance into two probe cascades and interpolate with depth moments to limit wall leaks. Invalidate old and new occupied regions on motion. Port both to compute; measure update cost separately from lookup.
3. **One tracer.** Start from the experimental deterministic tracer and its audit, not a second integrator. Share sample dimensions keyed by pixel/sample/bounce, BSDF, emitter sampling, MIS and accumulation. Only closest-hit/any-hit changes: software wide BVH traversal on GTX; Vulkan inline ray queries on capable RTX devices. Query features at startup, fall back to software, reset accumulation on scene/camera changes. Optimize queues after parity tests.
4. **Acceptance.** Compare the same G-buffer, camera, poses and exposure against high-sample mesh references, without denoising. Report visibility RMSE, indirect-only linear RGB error, frame/update time and peak memory on named GTX and RTX hardware. Include open sun, feet on ground, thin geometry, an occluded emitter and a moving skinned silhouette. Trace Pose B from its own G-buffer (the pasted sheet lambda captured Pose A). Check deterministic replay on each backend and compare backend means within sampling tolerance before selecting defaults.
