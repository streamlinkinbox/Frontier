# Renderer and ReSTIR GI status — 2026-10-29

## Bottom line

**ReSTIR GI, PBR and reflections have bounded offscreen tests, but are not complete or connected to the
editor.** Engine C++ and Slang contain temporal/spatial replay, optional seed history, SVGF, compact storage,
authored-material support and software/ray-query acceleration. The software route executes on SwiftShader CPU
Vulkan; the hardware path has compiled SPIR-V but has not run on a physical RT device.

Independent checks found and repaired double counting through thin glass
([evidence](../LightCompute/ReplayTransmission.md)) and a tiny-coverage authored-lobe density mismatch. Eleven
bounded materials now pass a separate hemisphere integral through the actual Engine sampler and recorded replay;
a two-bounce diffuse fixture also passes a separate area integral
([evidence](../LightCompute/ReplayMaterialSampling.md)). A later four-material
[reuse audit](../LightCompute/ReplayMaterialReuse.md) exercises temporal/spatial means and sequence resets. A
[paired floor study](../LightCompute/ReplayMaterialFloor.md) reports a large homogeneous dim-material improvement,
but a [mixed-material/visibility stress test](../LightCompute/ReplayMixedMaterialVisibility.md) exposes severe
variance and a significant lower-floor regression. A later [held-out guard test](../LightCompute/ReplayMaterialGuardCandidate.md)
failed independent means under different visibility even with the same material. A separate test-only
[fresh-neighbor static holdout](../LightCompute/ReplayFreshDonorCandidate.md) passed its candidate mean gates but
had a measured regression. Its later
[moving supplied-receiver/Fresh4 holdout](../LightCompute/ReplayFreshDonorMotion.md) failed two candidate means,
including an unchanged temporal-only case, and regressed mixed-metal combined4
RMSE by 41.81%. The candidate remains test-only. None is general ReSTIR validation. The PBR showroom's
80-sample reference is a finite same-shader comparison, not independent ground truth. An optional completion-gated
Engine submission sequence is now tested
[offscreen](../LightCompute/ReplaySubmissionSequence.md), but it does not create resources or present an image.
`Frontier/Applications/Editor/FrontierMain.cpp` and `UI/Editor/EditorWorkspace.cpp` still contain viewport
placeholders. Windows, physical-GPU timing, deforming scenes and production scene/resource integration remain open.

## Rough completion estimates

These are **subjective planning estimates**, not measurements or elapsed-time forecasts. Here 100% means a
production-ready Frontier renderer with correct broad scene/material handling, safe resource/history ownership,
live editor presentation, physical-GPU/Windows verification and acceptable real-time quality and frame cost.
Uncertainty is at least ±20 percentage points because the remaining integration and rare-path issues are not
fully characterized.

| Scope | Approximate complete | Approximate remaining | Main blockers |
| --- | ---: | ---: | --- |
| ReSTIR GI end-to-end | 40% | 60% | Failed lower-floor temporal/spatial means, real motion, scene/resources, editor and hardware performance. |
| PBR materials and reflections end-to-end | 35% | 65% | Production material/texture loading, broad reflection/transmission/lighting validation, viewport and hardware tests. |

The Engine contains bounded C++ recorder, domain and completion-gated sequence code; it is exercised by
offscreen Vulkan tests, not yet a production scene renderer or live editor viewport. The latest fresh-donor
quality candidate is **test-only** and is not an Engine default. Do not add percentages across the two overlapping
scopes or use them to promise a finish date.

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
| Multibounce GI | Seed/spatial replay, SVGF, offscreen sequencing; two-bounce diffuse area check | Equal-time game tests, broader motion, viewport. |
| Reflections | Rough screen proposals, bounded material hemisphere integrals and PBR replay fixtures | Broader independent glossy scenes, history, GPU timing. |
| PBR | Authored replay, independent hemisphere and thin-sheet integrals | Material loading, solid GI, SSS, editor submission. |
| Atmosphere, clouds, fog | CPU models and bounded Vulkan fixtures | Production scheduling and scene linkage. |
| Sun, Moon, stars, flares | CPU celestial and isolated Vulkan Sun work | GPU/editor integration and calibration. |
| Analytic / emissive lights | Clustered point/spot/IES and global facet CDF | Clustered facets and production. |
| Tyres | Standalone GPU projection, solid and shell work | Stable dynamics, validation and integration. |
| Gas / fluids | Footprints and a face-flux pressure prototype | Advection, boundaries, stability and production use. |

GI evidence: [seed replay](../LightCompute/ReplaySpatialDisk.md), [SVGF](../LightCompute/ReplayVarianceFilter.md)
and [sequencing](../LightCompute/ReplaySubmissionSequence.md). Reflection/PBR evidence:
[rough screen proposals](../LightCompute/RoughReflections.md), [PBR replay](../LightCompute/ReplayPbrReflection.md),
[material sampling](../LightCompute/ReplayMaterialSampling.md), [material reuse](../LightCompute/ReplayMaterialReuse.md),
[target-floor sensitivity](../LightCompute/ReplayMaterialFloor.md),
[mixed-material visibility](../LightCompute/ReplayMixedMaterialVisibility.md),
[moving supplied receivers](../LightCompute/ReplayFreshDonorMotion.md) and
[thin sheets](../LightCompute/ReplayTransmission.md). Tyre and gas evidence remains under
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
