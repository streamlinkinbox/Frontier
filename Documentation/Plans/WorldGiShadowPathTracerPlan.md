# Lighting real characters: two GTX GI paths, two shadow finalists, one tracer

**Decision for review:** Use the rendered triangles for visibility, including the current skinned vertex positions. Do **not** restore a runtime voxel SDF, a per-character distance-field bake, or capsule/box stand-ins. Keep `GpuSdfBaker` for offline volume authoring. A surfel is a light sample attached to a face; it does not replace the face in the visibility tree.

| Purpose | Advance first | Why |
|---|---|---|
| GTX GI, emitter detail | Stokes contour | Integrate projected solid angle over real emissive triangles. Small bright sources such as headlights are a good test. |
| GTX GI, predictable shading cost | Surfel cascade | Pay for transport when surfaces or lights change, then interpolate cached irradiance with a bounded number of reads per receiver. Count update time, not just lookup time. |
| GTX direct shadows | `ContactPcss` and `WideTreeRayQuery` | Start with cascaded shadow depth plus short triangle rays for interactive use. Sample the solar disc against the triangle tree when silhouette accuracy matters more than cost. These are finalists, **not** measured winners on animated meshes. |
| GTX + RTX reference | One deterministic path tracer | Same sampling and shading code; only closest-hit and occlusion queries change. GTX must work without ray-tracing extensions. |

## Recovery and evidence

The active branch restores [c14761a](https://github.com/streamlinkinbox/Frontier/commit/c14761aab92a755ccb5c0c3ffd197d48e55059d0), a **code-only** sync based on [daed305](https://github.com/SultanAladin/Frontier/tree/daed305). It does not include the upstream `EngineContent/`, `Experimental/`, or rendered `Exhibits/Images/` trees. The old `Engine/DistanceFieldSpace/` is absent. No need to delete the baking tools.

All four `Exhibits/Clean*` sources match the Git blobs at [536cfe1](https://github.com/SultanAladin/Frontier/tree/536cfe1/Exhibits). The GI pair originated in [2d6f15e](https://github.com/SultanAladin/Frontier/commit/2d6f15e); the shadow pair was added in `536cfe1`. They were removed upstream in [63db120](https://github.com/SultanAladin/Frontier/commit/63db120). Keep these as historical references, **not build targets**: they include deleted SDF headers and the shadow exhibit uses a capsule rig.

- [GI measurements](https://github.com/SultanAladin/Frontier/blob/2d6f15e/Exhibits/Images/CleanEmissiveGi/validation_summary.json): at 560×315 against the 128-sample reference, normal-GI RMSE was **0.2238 Stokes**, **0.2239 surfels**, **0.2259 voxel field**. On the probe metric the field (0.2262) edged out surfels (0.2269). This motivates testing two different cost profiles, not a claim that either port is faster on GTX.
- [Shadow measurements](https://github.com/SultanAladin/Frontier/blob/536cfe1/Exhibits/Images/CleanEmissiveShadows/shadow_validation_summary.json): surfel + capsules (0.2973) and hybrid cone + capsules (0.2983) beat the old SDF (0.4170) on visibility error. Hybrid used a vehicle SDF. Neither result ranks real skinned silhouettes; neither was faster than the old SDF in that test. We can omit the volume because triangle rays supply visibility, **not** because a capsule benchmark proves the replacement.
- [Frontier-1 shadow enum](https://github.com/SultanAladin/Frontier-1/blob/d5bbb81/Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h): five non-SDF modes and three SDF modes. This recovery has six non-voxel choices: the two finalists above, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, and experimental `SurfelFaceShadow`. Compare all six; use the actual triangles, not the surfel disks, for the reference.

## Folder structure and implementation status

```text
Engine/
  SpatialAcceleration/                 WideBoundingTree.*, SpatialSplitBoundingVolume.h
  GeometricRaster/
    DirectLighting/                    SolarShadowSequence.*, SurfelOccluderField.h
    GlobalIllumination/                StokesContourGiSequence.h, SurfelCascadeGiSequence.h
                                       DeterministicIntegrators.h, DeterministicPathTraceSequence.h
                                       TieredWorldRenderSequence.*, VulkanWorldRenderSequence.*
    Baking/                            GpuSdfBaker.* (outside runtime lighting)
  Shaders/
    DirectLighting/                    SurfelFaceShadow.slang, WideTreeRayQuery.slang
    GlobalIllumination/                StokesContourGiCompute.slang, SurfelCascadeGiCompute.slang
                                       DeterministicQuadrature.slang, WavefrontPathTraceCompute.slang
Exhibits/
  CleanEmissiveGiCandidates.h          historical
  CleanEmissiveGiValidation.cpp        historical
  CleanShadowAndGiCandidates.h         historical
  CleanEmissiveShadowValidation.cpp    historical
  WorldGiShadowPathTraceValidation.cpp current CPU smoke test
```

**What runs today:** the CPU exhibit builds polygon faces, traces a BVH, and checks open/blocked visibility, lamp response and repeatable sample coordinates. Local result: **15 passed, 0 failed** on triangulated boxes. This was not a character render or a Vulkan test. The recovered Stokes CPU code uses a centroid/area estimate and constant fill; the cascade uses average radiance, not directional transport. The wavefront shader writes a primary-hit mask. Acceleration-structure descriptors are not a working RTX backend.

## Implementation order and review gates

1. **Make the comparison scene trustworthy.** Load the original ControlVehicle asset and an actual indexed, skinned character with two poses. Preserve face/material IDs and instance transforms. Skin once, then refit triangle bounds and update attached surfel positions and normals from barycentrics. Invalidate caches in both the old and new occupied regions. Render each pose with **its own** primary-hit buffer (the pasted validator reused Pose A receivers for Pose B shadows). Compare the six modes against a 128-ray solar-disc reference; report moving silhouette, foot contact and thin-part errors separately from whole-frame error.
2. **Complete the two GI paths in C++ before the Slang port.** For Stokes, clip each emissive face to the receiver hemisphere, integrate its contour, test visibility over the full receiver-to-source segment, then light diffuse patches for the next bounce. For surfels, exchange radiance between sampled faces, bake two spatial cascades with directional irradiance and first/second depth moments, and interpolate without leaking through walls. Test a large near emitter, a fully hidden lamp and moving cast shadows. With lights and environment off, both paths must return zero; constant fill cannot pass as transport.
3. **Reuse one integrator, switch traversal by capability.** Consult the [Frontier-1 tracer](https://github.com/SultanAladin/Frontier-1/tree/d5bbb81/Experimental/DeterministicPathTracer). Share camera rays, pixel/sample/bounce dimensions, BSDF, next-event estimation, multiple importance sampling, and accumulation. First compare software BVH4 against CPU closest/any-hit results. Then add Vulkan BLAS/TLAS and inline ray queries for supported devices; fall back to software otherwise. Reset accumulation on camera, material or pose changes. Add wavefront queues and compressed BVH8 only after matching the simpler path.
4. **Choose defaults on hardware.** Compare identical camera, exposure, material inputs, poses and sample budgets, without denoising. Record GPU model, driver, resolution, indirect-only linear RGB RMSE, visibility RMSE, peak memory, update time and steady-state frame time. Check replay across batch sizes on the same backend and radiometric agreement across backends, not bit identity at shared triangle edges. Publish the raw scores and a matched comparison image; only then label a GTX algorithm “best.”
