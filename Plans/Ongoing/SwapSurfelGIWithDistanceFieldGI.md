# Architectural Plan: Swap Surfel GI with Distance Field GI (SDF GI)

**Date**: 2026-10-02
**Target Branch**: `arena/01a0fd52-frontier`
**Location**: `Plans/Ongoing/SwapSurfelGIWithDistanceFieldGI.md`

## Slate implementation — 2026-10-02

The imported `46476f3` still left field/cache memory unpopulated and did not match the resolve shader.
Slate replaces that scaffold with an executing scene-dependent pipeline:

- `DistanceFieldStructure` projects scene vertices through instance transforms and builds a stackless triangle BVH.
  Instance refresh changes the geometry revision; unchanged rows preserve temporal history.
- `DistanceFieldConstruct` generates three camera-snapped distance grids on the GPU from closest triangle points.
  The distance is oriented by the closest facet normal; open/thin meshes are supported as two-sided distance surfaces.
- A surface-radiance cache is associated with each voxel's closest surface point. This is **not** a UV card atlas.
  Two separate GPU buffers implement Jacobi diffuse propagation, direct sun, sky and arbitrary emissive materials.
- `DistanceFieldGIResolve` consumes raster visibility, the shared OpenPBR material decoder and the cache.
  Reflections/refraction traverse the exact world triangle BVH, never voxel surfaces. No RT cores are required.
- The shared push payload is exactly 96 bytes, with `static_assert` offsets on the host and matching GLSL fields.
  All resources are initialized, descriptors written and transfer/compute dependencies recorded before use.
- Missing shaders/resources refuse readiness. Scene/camera/lighting changes invalidate radiance history.
  Shading-table arrival and swapchain reconstruction rebind the stage; retirement is idempotent.
- The fixed-descriptor variant uses constant material coefficients when descriptor indexing is unavailable.
  GPU cache bounce albedo/emission currently uses flattened material coefficients rather than per-texel textures.
- Dynamic transforms rebuild the CPU world BVH and upload it in the command stream. This is correctness-first,
  not a demonstrated real-time performance budget. Hi-Z SSGI and capsule proxies remain future optimizations.
- The sky remains the existing non-RT analytic approximation, not the full raytraced atmosphere.
  Refraction is bounded to four mesh intersections; this is not an unbounded nested-medium path tracer.

`DistanceFieldExecution.cpp` runs the actual production SPIR-V with Vulkan synchronization validation and readback.
CPU showcase images below remain CPU proofs; they are not GPU startup screenshots or hardware performance evidence.

The following upstream sections describe design targets; the implementation and limits above take precedence.

---

## 1. Architectural Motivation & Render Mode Taxonomy

Frontier's three-way render mode matrix cleanly decouples primary visibility, indirect illumination transport, and specular transport:

| Mode Index | Raytracing Tile | Global Illumination Tile | Render Path Name | Primary Visibility | Diffuse Indirect GI | Direct Shadows | Specular Reflections | Glass Transport |
|---|---|---|---|---|---|---|---|---|
| **0** | **ON** | ON or OFF | **Raytraced ReSTIR** | Mesh Rasterization / BVH Hit | ReSTIR Spatiotemporal Reservoir | Raytraced Shadow Rays | Raytraced BVH (Multi-bounce) | Full Path Refraction |
| **1** | **OFF** | **ON** | **Distance Field GI (SDF GI)** | **Mesh Rasterization** | **Cascaded GDF Clipmaps + Surface Cache** | **Monotonic $k \cdot H / t$ Contact Soft Shadows** | **Raytraced BVH (Mesh, No SDF Blocks) / Sky** | **Snell's Law Refraction + Beer-Lambert** |
| **2** | **OFF** | **OFF** | **Plain Visibility Raster** | **Mesh Rasterization** | Sky Ambient Dome Direct | Cascaded Shadow Maps / Direct | Sky Dome | Sky Dome Transmission |

### Key Requirements Addressed
1. **Target Hardware**: GTX GPUs (No RT cores required; hardware RT disabled for Mode 1).
2. **Depth-Buffer Accelerated GI**: Primary visibility is rasterized first to produce the depth and G-buffer (`SurfaceImage`, `NormalImage`). Distance Field GI only evaluates shaded fragments that pass the depth buffer, completely skipping background and unoccluded sky pixels.
3. **Authentic Mesh ShaderBalls**: ShaderBalls are complex polygon meshes (outer slotted shell, inner core, floor mount), not plain mathematical spheres.
4. **Voxel SDF Artifact Elimination**: Raymarching voxelized SDF fields for specular reflections causes coarse, blocky artifacts on shiny surfaces. Specular reflections and glass refraction are therefore evaluated against mesh BVH geometry or analytic skies, and composited cleanly with the diffuse SDF GI.
5. **Monotonic Contact Soft Shadows**: Distance field shadowing uses the monotonic formula $S = \min(S, (k \cdot H) / t)$, avoiding singularity artifacts and black specks while preserving contact hardening.
6. **Continuous SDF Neighborhood Query**: Cell-based distance evaluation queries the 2×2 neighborhood to eliminate Voronoi boundary clamping discontinuities, resolving concentric stepping artifacts on the floor.

---

## 2. Engine Seam Integration

### 2.1 Host Ownership (`DistanceFieldGIStage`)
- **Header**: `Frontier/Engine/DeviceExchange/DistanceFieldGIStage.h`
  - Defines `DistanceFieldGIStage`, `DistanceFieldStageInit`, and `DistanceFieldFrameParams`.
  - Exposes lifecycle methods: `Bring()`, `Destroy()`, `SynchronizeField()`, and `RecordFrame()`.
- **Implementation**: `Frontier/Engine/DeviceExchange/DistanceFieldGIStage.cpp`
  - Allocates GPU storage buffers for Global Distance Field (GDF) 3D clipmaps and Surface Cache atlas allocations.
  - Creates the compute pipeline layout and descriptor sets with update-after-bind indexing.
  - Dispatches `DistanceFieldGIResolve.slang` (16×16 compute workgroups) with push constant parameters.

### 2.2 Swapchain Wiring (`SwapchainExchange`)
- Replaces legacy `SurfelGIStage` for `RenderPath == 1` inside `RecordComputeCommands()`.
- Adds `BringDistanceFieldGIStage()` and lifecycle management in `UploadScene()`, `RebuildSwapchain()`, and `Retire()`.
- Retains `SurfelStage` as a fallback safety layer if `DistanceFieldGIStage` is uninitialized.

### 2.3 Diagnostics and Runtime Routing
- `FrontierRuntime.cpp` and `ReSTIRIntegrator.h` updated to declare and log:
  ```
  Distance Field GI active: Cascaded GDF clipmaps, Surface Cache atlas, monotonic contact soft shadows (k*H/t), and multi-bounce emissive bleed. Evaluated against depth buffer. Specular reflection mode: raytraced (mesh BVH, no SDF blocks).
  ```

### 2.4 Multi-Toolchain Compliance
- Registered in `Frontier/CMakeLists.txt` (`FRONTIER_HOST_SOURCES`).
- Registered in `Frontier/Tools/Build/ToolchainSequence.ps1` (`$EngineRelative`).
- Validated via `Frontier/Tools/Build/CheckBuildSourceList.sh` (100% GREEN, 7/7 checks passed).

---

## 3. Visual Proof Artifacts

- `VisualProof/DistanceFieldGI/ShaderBall_Showcase_Raster_SDF_GI_4x3.png`: 1120×840 full 15×15 showcase of authentic ShaderBall meshes with SDF GI.
- `VisualProof/DistanceFieldGI/ShaderBall_Showcase_Raster_SDF_GI.png`: 1280×720 widescreen showcase render.
- `VisualProof/DistanceFieldGI/ShaderBall_Closeup_Raster_SDF_GI.png`: 1280×720 close-up view displaying ShaderBall geometric detail, inner core, outer shell, SDF soft contact shadows, and emissive indirect bleed.
- `VisualProof/DistanceFieldGI/Surface_Cache_Scene_Render.png`: Canonical surface cache render updated with the ShaderBall depth-buffered SDF GI pipeline.
