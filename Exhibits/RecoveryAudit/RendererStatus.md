# Renderer and ReSTIR GI status — 2026-10-29

## Bottom line

**ReSTIR GI, PBR and reflections have bounded offscreen tests, but are not complete or connected to the
editor.** Engine C++ and Slang contain temporal/spatial replay, optional seed history, SVGF, compact storage,
authored-material support and software/ray-query acceleration. The software route executes on SwiftShader CPU
Vulkan; the hardware path has compiled SPIR-V but has not run on a physical RT device.

A recent independent check found and repaired double counting through thin glass
([evidence](../LightCompute/ReplayTransmission.md)). The PBR showroom's 80-sample reference is a finite same-shader
comparison, not independent ground truth. An optional completion-gated Engine submission sequence is now tested
[offscreen](../LightCompute/ReplaySubmissionSequence.md), but it does not create resources or present an image.
`Frontier/Applications/Editor/FrontierMain.cpp` and `UI/Editor/EditorWorkspace.cpp` still contain viewport
placeholders. Windows, physical-GPU timing, deforming scenes and production scene/resource integration remain open.

## Ray tracing versus non-ray paths

- The tested GI and reflection research shaders trace triangles using software BVH traversal on Vulkan
  compute. The demo also calls its route “RTX”/TLAS/BLAS, but its pinned upstream
  [`RayTraversal.wgsl`][ray] in the pinned VehicleHMI prototype explicitly describes **hardware-agnostic**
  traversal and uses storage-buffer BVH nodes, a shader stack and
  triangle intersections. Such software ray tracing is not evidence of hardware ray-query/RT-core use.
- `Frontier/Engine/Rendering/PathReplayRayQuery.{h,cpp}` and `Shaders/PathReplayRayQuery.slang` now implement
  `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` capability negotiation, BLAS/TLAS sizing and command
  recording, and verified SPIR-V `1.4` (`spvRayQueryKHR`) inline ray-query kernels alongside 16-bin SAH-2 +
  front-to-back ordered software BVH traversal ([evidence](../LightCompute/ReplayRayAcceleration.md)). Because
  CPU SwiftShader (`Vulkan 1.3`) does not expose KHR ray-query extensions, physical-GPU RT-core execution/timing,
  dynamic BLAS refit for deforming meshes, and live editor viewport integration remain open.
- Screen-space reflection proposals and distance-field traversal exist in validation paths; a finished,
  editor-integrated **non-ray-traced ReSTIR GI** alternative is not established. Distance-field marching is
  still ray-based even when it does not use triangle BVHs or hardware RT. Raster/offscreen PBR exists but is
  not the completed alternative GI pipeline.

## Actual tested capabilities and gaps

| Area | Bounded evidence | Still missing |
| --- | --- | --- |
| Multibounce GI | Seed/spatial replay, SVGF, offscreen sequencing | Equal-time game tests, broader motion, viewport. |
| Reflections | Rough screen proposals and PBR replay fixtures | Independent glossy reference, history, GPU timing. |
| PBR | Authored replay and independent thin-sheet integral | Material loading, solid GI, SSS, editor submission. |
| Atmosphere, clouds, fog | CPU models and bounded Vulkan fixtures | Production scheduling and scene linkage. |
| Sun, Moon, stars, flares | CPU celestial and isolated Vulkan Sun work | GPU/editor integration and calibration. |
| Analytic / emissive lights | Clustered point/spot/IES and global facet CDF | Clustered facets and production. |
| Tyres | Standalone GPU projection, solid and shell work | Stable dynamics, validation and integration. |
| Gas / fluids | Footprints and a face-flux pressure prototype | Advection, boundaries, stability and production use. |

GI evidence: [seed replay](../LightCompute/ReplaySpatialDisk.md), [SVGF](../LightCompute/ReplayVarianceFilter.md)
and [sequencing](../LightCompute/ReplaySubmissionSequence.md). Reflection/PBR evidence:
[rough screen proposals](../LightCompute/RoughReflections.md), [PBR replay](../LightCompute/ReplayPbrReflection.md)
and [thin sheets](../LightCompute/ReplayTransmission.md). Tyre and gas evidence remains under
[VehiclePhysics](../VehiclePhysics/) and [FluidReference](../FluidReference/).

The vehicle dynamics/Jolt port remains explicitly deferred. The new tyre work is standalone and does not
change it.

## What the GI chart actually means

In [the rigid-red chart](../LightCompute/ReplayRigidVarianceRepeat.md), the orange points are the **older
256-batch estimates**; green points are the newer 2,048-seed estimates, blue is the earlier CPU reference,
and the green vertical band is the new CPU mean ± two nominal standard errors. Horizontal bars are ± two
sample standard errors; the printed `±` numbers are ± one. These show uncertainty in this particular
fixture, not percentage completion or frame-rate improvement.

The newer mixture/spatial red standard deviation was `0.587×` uniform in that sample, but the earlier
estimate was `1.468×`, and the individual new groups ranged `0.306–1.203`. Rare bright paths dominate the
sample tails; the later [bounce audit](../LightCompute/ReplayRigidTailAudit.md) found the three largest
uniform paths hitting the same bright emissive triangle at zero-based bounce 2. Its mirrored breakdown
shares shader helpers and is not independent validation of path values, PDFs/MIS or random streams. The
old mean discrepancy remains unexplained. Spatial reuse costs additional work, so neither the lower SD nor
narrower bar is an equal-time speedup or proof of universally better images.

[ray]: https://github.com/SultanAladin/Frontier/blob/6afb6f0/Experimental/VehicleHmiPrototype/Shader/RayTraversal.wgsl
