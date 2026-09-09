# R2 — Resident scene, GPU culling, visibility raster

Date: 2026-09-04. Plan for approval — no code yet. Parent: `RestirRealtimeArchitecturePlan-v2.md` v2.1 §1–2, §6 row R2.

## Goal

Replace "36 hard-coded triangles in one SSBO, primary ray per pixel" with: a whole level resident on the GPU, culled on the
GPU, rasterised once into a **visibility buffer** (depth · visibility ID · motion vectors), then resolved to the surface
attributes the path-tracing kernel needs. The existing kernel keeps running unchanged for shadow/bounce rays (still brute
force until R3) but **stops tracing primary rays** — it reads the resolved surface instead.

## What already exists and is reused (no new types where an old one fits)

| Existing | Used as |
|---|---|
| `GeometricRaster/GeometryStructure` — `VertexRecord` (pos/normal/tangent/uv, 64 B), `uint32` indices, `PolyhedralCluster` (bounding sphere + normal cone + triangle range) | the resident mesh format; clusters = cull unit (≤ 128 tris) |
| `GeometricRaster/VisibilityProjection::VisibilityIdentifier` — 18-bit instance + 14-bit primitive pack | the GPU visId encoding (same bit layout in GLSL) |
| `GeometricRaster/MaterialCodec`, `RasterSequence` | material summary source; RasterSequence stays CPU-side reference only |
| `ExternalPackages/cgltf` (already a submodule) | glTF 2.0 import |
| `SwapchainExchange` storage image + ImGui render pass | unchanged; the new passes are inserted before the compute dispatch |

## Deliverables

### R2.1 Scene import → resident buffers (`Engine/GeometricRaster/SceneAssembly.{h,cpp}`)
- `SceneAssembly::ImportGltf(path)` → `GeometryStructure` per mesh, `InstanceRecord[]` from the node tree, `MaterialSummary[]`
  (base colour, roughness, metallic, emissive; texture indices reserved, textures themselves land in R2b — flat colours first).
- Cornell box becomes a glTF in `Projects/Project-Zero/Content/Scenes/CornellBox.gltf` (generated once by a tiny exporter
  from the current `RayTracingSolver` so the reference image is unchanged); a second, larger test level (Sponza-class, OFL/CC0)
  proves scale. ⚠️ Level choice needs your OK — see Questions.
- Uploads: `VertexSoa` (SSBO), `IndexBuffer` (SSBO + index-buffer usage), `InstanceRecord[]` (world, prevWorld, meshRange,
  materialIndex, flags), `ClusterRecord[]` (sphere, cone, triangle range, instance), `MaterialSummary[]`, `LuminaireTable`
  (emissive triangle list + alias table for O(1) light pick). Emissive triangles no longer need to be "last".

### R2.2 GPU culling (`Engine/Shaders/ClusterCull.slang`, compute)
- Per cluster: frustum test on world sphere → backface cone test → HiZ occlusion test (previous frame's pyramid, two-phase:
  phase 1 draws last frame's visible set, HiZ is built from it, phase 2 tests the remainder).
- Survivors append `VkDrawIndexedIndirectCommand` (+ cluster id in `firstInstance`) to an indirect buffer;
  `vkCmdDrawIndexedIndirectCount` issues everything in one call.
- Requires `VK_KHR_draw_indirect_count` (core 1.2) — probed and reported like the RT extensions.

### R2.3 Visibility raster (`Engine/Shaders/VisibilityRaster.slang`, vertex + fragment)
- Attachments: `D32_SFLOAT` depth, `R32_UINT` visId (`VisibilityIdentifier` layout), `R16G16_SFLOAT` motion vectors from
  `world` vs `prevWorld` (camera **and** object motion). No colour.
- Reverse-Z with infinite far plane (precision for large levels). Clip-space convention documented against CLAUDE.md §7
  (RH +Z up world → Vulkan NDC: Y flipped in the projection, depth 1→0).

### R2.4 HiZ pyramid (`Engine/Shaders/HiZReduce.slang`, compute)
- Min-reduce (reverse-Z → farthest = min) into a full mip chain, one dispatch per level (subgroup single-pass later if needed).

### R2.5 Surface resolve (`Engine/Shaders/SurfaceResolve.slang`, compute)
- Reads visId → instance/triangle → 3 vertices → screen-space barycentrics (Schied & Dachsbacher 2015 ray-triangle
  intersection form, analytic derivatives) → world position, geometric + interpolated normal, uv, material index, albedo.
- Writes a **thin G-buffer**: `RGBA32F` position, `RG16F` oct-normal + `R16` roughness/`R16` metallic, `R8G8B8A8` albedo,
  `R32_UINT` visId copy (ping-pong for R5 validation).

### R2.6 Kernel hookup
- `ReSTIRViewport.slang` step 2 becomes: read the resolved surface; if visId is invalid → background. Shadow and bounce rays
  keep the brute-force loop (R3 swaps it for CWBVH). Result must match today's image to within noise on the Cornell glTF.

### R2.7 Diagnostics
- Control Centre › Render Settings page gains a **Debug View** dropdown (Off · Depth · Visibility ID · Motion Vectors ·
  Cluster ID · HiZ level N · Albedo · Normal). Persisted in `[render] debug_view`. This is the "motion vectors and previous
  frame validation are visible in diagnostics" acceptance criterion.
- Telemetry overlay row: clusters total / after frustum / after cone / after HiZ / drawn triangles; pass timings via
  `VkQueryPool` timestamps (cull, HiZ, visibility, resolve, kernel).

## Frame order after R2

```
CullPhase1 → VisibilityRaster(phase 1) → HiZ → CullPhase2 → VisibilityRaster(phase 2) → SurfaceResolve → PathKernel → Blit → ImGui
```

## Proofs
1. Cornell glTF renders identically to the current hard-coded scene (side-by-side).
2. Debug views: depth, visId (hashed colour), motion vectors under camera orbit, cluster ID.
3. Large level: telemetry shows cluster counts collapsing through frustum → cone → HiZ; single indirect draw; frame time.
4. HiZ correctness: walk behind a wall, occluded clusters drop to ~0 with no popping (recorded sequence).
5. Config round-trip of `debug_view`.

## Deviations / risks to flag now
- glslc/slangc and a Vulkan device are absent from the sandbox; shaders are review-only until your build. I will keep each
  shader small and self-contained to minimise fix-up round trips.
- `VertexRecord` is 64 B; for millions of vertices a packed 32 B variant will be wanted later (R7) — not now.
- `VisibilityIdentifier` is 18+14 bits → 262 k instances × 16 k triangles per mesh; meshes above 16 k triangles are split
  into multiple mesh ranges at import (transparent to the rest).
- Textures (bindless) are deferred to R2b so the visibility path lands first; Sponza will render with flat base colours
  until then.

## Questions before I start
1. Test level: **Intel Sponza (CC-BY, ~260 k tris, glTF)** or the smaller **Crytek Sponza (~262 k)**? Either downloads at
   build time (not committed). Say which, or name another.
2. Debug View dropdown lives on the Render Settings page (currently the only page without real content) — OK?
3. R2b (bindless textures) immediately after R2, or move on to R3 (CWBVH) first? My recommendation: R3 first — it removes
   the O(N) rays, which is the actual bottleneck; textures are cosmetic for the ReSTIR goal.
