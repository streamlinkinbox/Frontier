# Renderer and ReSTIR GI status — 2026-10-29

## Bottom line

**Core ReSTIR GI + GPU SVGF + Compact VRAM + Authored Multi-Strata PBR & Thin-Sheet Glazing are verified in
`Frontier/Engine/Rendering/`, while live `Frontier` C++ editor viewport wiring and physical-GPU RT-core timing
remain open.** The repository now compiles and executes unified seed-bank + Vogel spatial-disk ReSTIR GI,
`PathReplayCompactStorage` (`-63.05%` to `-80.01%` 1080p VRAM), Veach $\beta = 2$ `PathReplayPowerMis`,
roughness-driven Specular-Cone SVGF, 16-bin SAH-2 + front-to-back ordered BVH + `VK_KHR_ray_query` (`spirv_1_4`),
and `PathReplayAuthoredMaterials` (`TextureStudio` multi-strata clearcoat car paint, polished conductors, Charlie
velvet sheen, mip-mapped textures, and `Category == 2` Beer-Lambert thin-sheet glazing) on SwiftShader Vulkan
([evidence](../LightCompute/ReplayPbrReflection.md)). Vulkan resource ownership and live editor scene-canvas
submission (`Frontier/Applications/Editor/FrontierMain.cpp` and `UI/Editor/EditorWorkspace.cpp`) remain separate
integration work.

## Ray tracing versus non-ray paths

- The tested GI and reflection research shaders trace triangles using software BVH traversal on Vulkan
  compute. The demo also calls its route “RTX”/TLAS/BLAS, but its pinned upstream
  [`RayTraversal.wgsl`](https://github.com/SultanAladin/Frontier/blob/6afb6f00b5f25c45f7cda92c7da65b64845768f3/Experimental/VehicleHmiPrototype/Shader/RayTraversal.wgsl)
  explicitly describes **hardware-agnostic** traversal and uses storage-buffer BVH nodes, a shader stack and
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

| Area | Evidence | Still missing |
|---|---|---|
| Dynamic/multibounce GI | [Moving-light and blocker fixture](../LightCompute/ReplayFacetMixtureMotion.md), [14 captures](../LightCompute/ReplayFacetMixtureMotionCaptures.md), [2,048-seed follow-up](../LightCompute/ReplayRigidVarianceRepeat.md), [unified seed-bank + Vogel spatial-disk reuse](../LightCompute/ReplaySpatialDisk.md), [motion-aware GPU SVGF denoiser](../LightCompute/ReplayVarianceFilter.md), [dual-path `VK_KHR_ray_query` + SAH-2 ordered BVH](../LightCompute/ReplayRayAcceleration.md), and [compact `17*N` VRAM + $\beta=2$ Power MIS + `<= 71` cap](../LightCompute/ReplayPbrReflection.md) verified on SwiftShader Vulkan | Physical-GPU RT-core execution/timing, arbitrary deforming meshes, and live editor viewport wiring |
| Reflections | [Rough screen proposals](../LightCompute/RoughReflections.md) plus [moving-camera multi-bounce glossy clearcoat/conductor ReSTIR GI + roughness-driven Specular-Cone SVGF (`224` mirror normal exponent, `-88.5%` flicker)](../LightCompute/ReplayPbrReflection.md) | Physical-GPU hardware-RT reflection timing, deforming-mesh motion vectors, and live editor viewport linkage |
| PBR | [Offscreen authored material pipelines](../Renderer/README.md) plus [`PathReplayAuthoredMaterials` 8-strata `TextureStudio` Clearcoat Flake Paint, Chrome, Charlie Velvet, mip pyramids, and `Category == 2` Beer-Lambert thin-sheet glazing + multi-pane shadow transmittance](../LightCompute/ReplayPbrReflection.md) | Volumetric solid-dielectric caustics/dispersion/SSS inside GI, editor material-inspector binding, and native viewport submission |
| Atmosphere, clouds, fog | CPU atmosphere and reference sky; cloud/fog GPU shader payloads and bounded validation | Full GPU atmosphere/volume production scheduling, scene linkage and measured sky/volume acceptance |
| Sun, Moon, stars, flares | CPU celestial model and actual demo Moon texture; isolated Vulkan Sun and simultaneous flare components | GPU/editor Moon, star and general celestial integration, Moon occlusion/indirect-light coverage, lens calibration and texture licence review |
| Analytic / emissive lights | Bounded clustered point/spot/IES checks; global area/luminance emissive distribution | **Clustered emissive triangles**, broad IES/scene coverage and production submission; global CDF is not a clustered light grid |
| Tyres | Standalone GPU [XPBD projection](../VehiclePhysics/TyreProjectionDevice.md), [Hex8 solid](../VehiclePhysics/TyreSolidDevice.md) and [curved-shell](../VehiclePhysics/TyreMembraneDevice.md), including resident surface probes on SwiftShader | Shell mechanics counterexamples, standalone spring-contact dynamics, measured tyre validation, stable hard-drop results, physical GPU, actual mesh rendering and editor integration |
| Gas / fluids | [Engine footprints and demo WGSL transport](../FluidReference/PlumeTransport.md); separate [matched face-flux pressure prototype](../FluidReference/GasProjection.md) passes bounded checks | Original plume pressure defect remains; prototype needs compatible advection, moving/open boundaries and production scheduling; scene/editor rendering, extended stability and physical validation; liquid solver not established |

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
