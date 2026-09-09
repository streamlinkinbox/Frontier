# ReSTIR realtime architecture plan — v2.1 (refined + research-reviewed)

Date: 2026-09-04 (v2.1 amendments same day)
Supersedes: `RestirGIRealtimeArchitecturePlan.md` (2026-08-25). Planning only — no renderer code changes accompany this document.

This revision folds in three things the v1 plan did not have: (1) an audit of what is actually in the tree today, (2) how *many* meshes get into the renderer at all (scene → GPU buffers → acceleration structure → visibility), and (3) how a visibility raster / depth buffer fits, since it was asked whether an Unreal-style visibility pass helps ReSTIR. Short answer: yes, it is the natural primary-ray stage for Tier A and B and is exactly what ReSTIR wants as its G-buffer.

---

## 0. Where we are (audit of `Engine/Shaders/ReSTIRViewport.slang`, `ReSTIRIntegrator`, `SwapchainExchange`)

| Piece | Today | Plan target |
|---|---|---|
| Scene input | 36 hard-coded Cornell triangles in one SSBO; luminaire must be the *last* triangles | Arbitrary mesh count, instances, per-instance transforms, emissive triangle list |
| Primary visibility | Möller–Trumbore over **every** triangle per pixel (O(N)) | Visibility raster (depth + IDs) → reconstruct hit from IDs; rays only for secondary/visibility |
| Acceleration structure | none | CPU SAH BVH → flattened GPU nodes (Tier A); BLAS/TLAS + `rayQueryEXT` (Tier B) |
| ReSTIR DI | initial RIS reservoir only; "spatial" loop is extra candidates from the same pixel (mislabelled; biased) | temporal + spatial reservoir reuse with neighbour validation and unbiased/MIS weights |
| ReSTIR GI | plain one-bounce NEE path tracing, no reservoir | GI reservoirs (Ouyang 2021), temporal + spatial |
| Denoise / history | running mean reset on any camera move | motion vectors, disocclusion, à-trous denoiser |
| Capability detection | none | `RayTracingCapabilitySet` probe → tier select |
| Fake terms | `albedo × 0.015` ambient floor | removed once GI is real |

Everything below is designed so the current shader can be *replaced stage by stage* rather than rewritten in one go.

---

## 1. Rendering many meshes at once — the scene → GPU pipeline

The question "how do you render a lot of geometry" has the same answer for raster and ray tracing: **do not draw objects; draw a scene that is already resident on the GPU, and let the GPU decide what is visible.** Unreal (Nanite) is the extreme version of this; we take the same structure at a scale we can implement.

### 1.1 Resident scene (built once per load, updated by dirty range)

```
GeometryStructure (CPU, Engine/GeometricRaster)      GPU buffers (one of each, whole scene)
  Mesh { vertices, indices, material }         →     VertexSoa      : positions, normals, tangents, uvs (SSBO)
  MeshChunk (≤ 128 triangles)                  →     IndexBuffer    : uint32 triple per triangle (SSBO)
  Instance { mesh, transform, prevTransform }  →     InstanceRecord : world, prevWorld, meshRange, materialIndex, flags
  Material  { albedo, roughness, metallic, emissive, textures } → MaterialSummary : packed 32 B + bindless texture ids
  Emissive triangles                           →     LuminaireTable : (instance, triangle, power) + alias table for O(1) light pick
```

- **One vertex buffer, one index buffer, one instance buffer** for the entire level. Meshes are *ranges*. Adding a mesh = appending; moving an object = writing one `InstanceRecord`.
- Chunks of ≤ 128 triangles are the unit for culling and for BVH leaves. Chunk bounds are stored in a `ChunkBounds` SSBO.
- Bindless: one descriptor array for all textures (`VK_EXT_descriptor_indexing`, required for both tiers) so no per-material descriptor sets.

### 1.2 GPU-driven culling (this is the "lots of geometry at once" part)

Per frame, one compute pass over `InstanceRecord[]` × chunks:

1. **Frustum cull** against chunk bounds (world-space AABB from mesh AABB × transform).
2. **Occlusion cull** against the previous frame's hierarchical depth pyramid (HiZ) — two-phase: draw what was visible last frame, build HiZ, test the rest, draw the newly visible. This is the standard GPU-driven technique (Assassin's Creed Unity / Nanite phase 1) and removes almost all hidden geometry before any triangle is rasterised.
3. Survivors are written to an **indirect draw buffer** (`VkDrawIndexedIndirectCommand[]` + count). The CPU issues a single `vkCmdDrawIndexedIndirectCount`.

Result: draw-call cost is constant regardless of object count; cost scales with *visible* triangles only. A 5–10 M triangle level is the realistic ceiling for this approach on a 1060; going beyond that needs meshlet/cluster LOD (Nanite territory — see §7 later phases).

### 1.3 Why not "Nanite"

Nanite = cluster hierarchy + continuous LOD + software rasteriser for micro-triangles + virtual geometry streaming. It is thousands of engineer-hours and depends on 64-bit atomics and mesh shaders / compute raster. We take the *first two thirds of its structure* — chunked geometry, GPU culling, indirect draws, visibility buffer — and skip continuous LOD/streaming. Discrete LOD per mesh (author 2–4 levels; select by screen-size in the cull pass) covers games at our scale.

---

## 2. Visibility raster (Unreal-style) and how it feeds ReSTIR

### 2.1 What the pass produces

A single raster pass over the indirect-draw survivors writing **no colour**, only:

| Target | Format | Use |
|---|---|---|
| Depth | D32 | HiZ pyramid, motion reprojection, plane-distance tests |
| Visibility ID | R32_UINT = `instanceId:16 | triangleId:16` (or R32G32 if ranges exceed 65 k) | reconstruct position/normal/UV/material by barycentric re-derivation |
| Motion vectors | RG16F | computed from `world` and `prevWorld` in the vertex shader |

This is a **visibility buffer** (Burns & Hunt 2013), which is what Unreal calls its Nanite visibility pass. No G-buffer fat here: albedo/normal/roughness are *resolved* in a following compute pass from the IDs — cheap, exact, and it never shades hidden pixels.

### 2.2 Why it helps ReSTIR directly

- **Primary hit for free.** Today every pixel traces a primary ray through 36 triangles; in a real level that is the most expensive ray. Raster does it at hardware speed, and the resolved position/normal/material is exactly ReSTIR's "shading point".
- **Depth + normal + object id are the temporal validation inputs.** ReSTIR temporal reuse must reject the previous reservoir when depth/normal/id disagree; the visibility buffer supplies all three (current and previous, ping-pong).
- **Motion vectors** come out of the same vertex shader (`prevWorld`), giving exact reprojection for camera *and* object motion — without them ReSTIR smears history and flickers.
- **HiZ** doubles as the occlusion culler (§1.2) and as a cheap screen-space visibility test for spatial-neighbour reuse.

So the depth buffer is not a side feature; it is the front half of the ReSTIR frame graph. Rays are reserved for what raster cannot answer: light visibility (shadow rays) and indirect bounces.

### 2.3 Cost on the baseline card

Visibility pass + resolve on a GTX 1060 at 1080p is ~1–2 ms for a few million triangles after culling. That budget is far below the current brute-force ray loop.

---

## 3. Acceleration structures

### 3.1 Tier A — Slate software BVH (compute, works on any Vulkan 1.2 device) — **the primary path for GTX-class cards**

v2.1 amendment: Pascal (GTX 10xx) never received `VK_KHR_ray_query` (NVIDIA's DXR fallback on those parts is a compute
emulation that was not extended to Vulkan ray queries). Tier A is therefore not a fallback but the main path for the
baseline card, and it must use the fastest known GPU layout rather than a hand-written binary BVH.

**Build (CPU, at load; background thread via `TaskScheduler`) — via `tinybvh` (single header, MIT, ExternalPackages/tinybvh):**
- `BVH::BuildHQ` (spatial splits) for static meshes, `BVH::Build` (binned SAH) for the rest → collapse to **CWBVH**
  (compressed 8-wide BVH, Ylitie et al. 2017: 1.9–2.1× faster incoherent traversal than binary layouts, 35–60 % of the memory).
- Top level: tinybvh TLAS over instance world AABBs, rebuilt per frame (thousands of instances, not triangles).
- GPU H-PLOC builds for dynamic geometry are the later upgrade (R7), not phase 1.

**Traversal (GLSL compute):**
- CWBVH traversal kernel ported from tinybvh's OpenCL sample (compressed node fetch, octant-ordered child visiting, compressed stack).
- Two-level: TLAS yields (instance, BLAS root); transform ray into object space; closest-hit and any-hit (shadow, early-out) variants.
- Triangles fetched from the shared `IndexBuffer`/`VertexSoa`, so no duplicate geometry.

**Refit:** rigid instance motion only touches the TLAS. Skinned meshes get a per-frame BLAS refit — R7.

### 3.2 Tier B — Vulkan hardware AS (`VK_KHR_acceleration_structure` + `VK_KHR_ray_query`)

- One BLAS per mesh (compacted after build), one TLAS per frame with `VkAccelerationStructureInstanceKHR[]` mirroring `InstanceRecord[]` (same index → the shader uses `rayQueryGetIntersectionInstanceIdEXT` to fetch our record).
- Ray queries from **the same compute shaders** as Tier A: traversal is behind one function `TraceClosest(ray)` / `TraceAny(ray)` with two implementations selected by specialisation constant. Reservoir code is written once.
- Flags: opaque for solid, `NO_OPAQUE` + any-hit alpha test for masked geometry, cull-disable for two-sided.

### 3.3 Tier C — ray pipeline (`VK_KHR_ray_tracing_pipeline`)

Only after Tier B is stable; for ReSTIR PT and material-heavy hit shaders. Not scheduled in this document beyond a placeholder.

### 3.4 Capability detection

`Engine/DeviceExchange/RayTracingCapabilitySet` (new): probes at device creation for `accelerationStructure`, `rayQuery`, `rayTracingPipeline`, `bufferDeviceAddress`, `descriptorIndexing`, 64-bit atomics, subgroup size. Tier = highest satisfied; **the config file can force a lower tier** (`[render] ray_tracing_tier = "software" | "ray_query" | "auto"`) via `ConfigurationRegistry`. A tier is never faked: if `ray_query` is requested but absent, downgrade and toast it.

v2.1 amendment: trust the **device extension list** (`vkEnumerateDeviceExtensionProperties`), not the feature struct alone —
early drivers reported `rayQuery = true` on Pascal and crashed at pipeline creation (Khronos Vulkan-Docs #1241). Before
committing to Tier B, R4 builds a one-triangle BLAS/TLAS and traces one ray as a start-up smoke test.

---

## 4. Frame graph (Tier A/B share it; only the trace function differs)

```
 ①  CullPass          instances × chunks → indirect draw list       (compute, HiZ from frame n-1)
 ②  VisibilityPass    depth · visId · motion                        (raster, indirect count)
 ③  HiZPass           depth pyramid                                 (compute)
 ④  ResolveSurface    visId → pos · normal · uv · material · albedo (compute; writes thin G-buffer)
 ⑤  AsUpdate          TLAS rebuild (+ BLAS refits)                  (CPU/compute Tier A · vkCmdBuildAS Tier B)
 ⑥  DI_Initial        M light candidates via alias table → reservoir (unshadowed p̂)
 ⑦  DI_Temporal       reproject with motion; validate depth/normal/id; merge prev reservoir (M clamp 20×)
 ⑧  DI_Spatial        k neighbours (blue-noise disk), same validation, MIS-weighted merge; 1–2 rounds
 ⑨  DI_Shade          1 shadow ray to the selected light sample → direct radiance
 ⑩  GI_Initial        1 cosine (or GGX) bounce ray → hit → NEE at hit → GI sample (pos, normal, radiance)
 ⑪  GI_Temporal/Spatial  as ⑦/⑧ with Jacobian for reconnection (Ouyang 2021 §4.3)
 ⑫  GI_Shade          optional visibility ray to the reused GI vertex → indirect radiance
 ⑬  Denoise           firefly clamp → temporal accumulation (variance-guided) → à-trous ×3-5 (depth/normal/roughness stops), diffuse & specular separately
 ⑭  Composite         direct + indirect + emissive → ACES → swapchain (ImGui / Control Centre on top)
```

History resources (ping-pong): DI reservoirs, GI reservoirs, depth, normal, visId, radiance moments.
Reduced-resolution GI: ⑩–⑫ run at ½ or ¼ resolution on Tier A with checkerboard, upsampled in ⑬ using the full-res G-buffer as guide.

### Reservoir layouts (32 B each, fits 1 GB at 4 K with ping-pong)

```
DIReservoir { uint  lightId;  float2 uv;   float wSum;  float W;  uint M;  float pHat;  uint age; }
GIReservoir { float3 hitPos;  uint  packedNormal;  float3 radiance;  float wSum;  float W;  uint M;  uint age; }
```

---

## 5. Quality presets (map to existing `FidelityClassifier` tiers)

| Fidelity | GI res | DI M / spatial | GI spatial | Denoise | Intended card |
|---|---|---|---|---|---|
| Minimal | ¼ | 4 / 1 | 0 | temporal + 3 à-trous | GTX 1060 |
| Economy | ½ checker | 8 / 1 | 1 | + variance | GTX 1660 |
| Standard | ½ | 8 / 2 | 1 | full | RTX 2060+ |
| Ultra | full | 16 / 2 | 2 | full | RTX 3070+ |
| Reference | full | 32 / 3 | 2, GI visibility ray | full | offline-ish |

The dashboard already exposes these five; the mapping just gains real meaning.

---

## 6. Implementation phases (each = plan → approve → code → proofs, as with the Control Centre)

| # | Phase | Deliverable / proof |
|---|---|---|
| R0 | Honest rename + cleanup | shader/class comments say "progressive path tracer + RIS direct"; fake spatial loop removed; ambient floor behind a debug flag |
| R1 | Capability + frame-graph skeleton | `RayTracingCapabilitySet`; render-graph resource table; `Slate.config.toml` `[render] ray_tracing_tier`; overlay shows detected tier |
| R2 | Resident scene + GPU culling + visibility raster | glTF import of a real level (e.g. Sponza-class) via `GeometryStructure`; indirect draws; visId/depth/motion debug views in the FPS overlay |
| R3 | tinybvh → CWBVH + `TraceClosest/TraceAny` | BVH build stats; shadow rays from the visibility buffer; shadowed direct light on the imported level |
| R4 | **ContentInterchange** — R4a data contract ✅ 95c4f65 (`RestirPhaseR4a-ContentInterchange.md`); R4b shading lobes next. — `MaterialDescriptor` (OpenPBR 49 params + `slate_` haziness/glints, variable slab list + operations), slab cap per tier via `[render] slab_limit` (Tier A 1 flattened, Tier B 4, ceiling 8), bindless textures, `PlacementRecord` scene graph (data only, no UI), FBX (ufbx) + OBJ (fast_obj) codecs; kernel gains EON diffuse, GGX with Kulla–Conty compensation, coat, fuzz (LTC), thin-film | Standard Shader Ball white-furnace set; Sponza with textures; FBX/OBJ round trips; see `MaterialSystemResearch-2026.md` §7 |
| R5 | Hardware AS path | same picture on RTX through `rayQueryEXT`; toggle between tiers with identical output |
| R6 | ReSTIR DI proper | back-projected temporal + spatial reservoirs, **visibility reuse**, M-clamp (20×), 25°/10 % neighbour rejection; many-light scene; diagnostics: M, W, age views |
| R7 | ReSTIR GI + denoiser | GI reservoirs; internal à-trous; camera-motion capture showing no smear. R7b (optional): NRD consumer |
| R8 | GPU H-PLOC builds, skinned refit, discrete LOD, world-space reservoir cache, reservoir splatting / ReSTIR PT | later |

Renumbered 2026-09-04 (v2.2): materials/interchange inserted as R4 because the importer, textures and the surface
record are one data contract; AS/ReSTIR phases shift by one.

Estimated order of magnitude: R1–R3 are the bulk of the plumbing; R5/R6 are the research-heavy parts.

---

## 7. Acceptance criteria (unchanged from v1, made testable)

1. Runs on a device without RT extensions (Tier A) and switches to hardware AS when present; the tier is shown in telemetry and never faked.
2. A multi-million-triangle imported level renders with a single indirect draw per pass and culls to visible chunks (HiZ debug view).
3. Depth, visibility-ID and motion-vector debug views are available from the FPS overlay.
4. ReSTIR DI temporal/spatial reuse is verifiably unbiased vs. a converged reference (side-by-side diagnostic).
5. Camera and object motion do not smear history across disocclusions (recorded proof).
6. One scene switches Minimal → Reference from the dashboard with the expected quality/cost steps.

---

## 8. Answers to the specific questions

- **Is the current ReSTIR "proper"?** No — it is an initial-candidate RIS stage on a brute-force path tracer, with no reuse, no AS, and a mislabelled spatial loop. Correct in its small part, not ReSTIR as published.
- **Is it performant / does it have acceleration structures?** No AS; O(triangles) per ray. Fine for the Cornell box only.
- **Will a visibility raster / depth buffer help?** Yes, materially: it replaces the most expensive ray, and its depth/normal/id/motion outputs are precisely the validation inputs ReSTIR reuse needs (§2).
- **How do we render lots of geometry at once?** Whole-scene resident buffers + GPU frustum/HiZ culling + indirect draws + visibility buffer (§1–2). That is the part of Unreal's approach that is achievable here; continuous LOD/streaming (Nanite) is explicitly out of scope until R7+.

---

## 9. v2.1 research review (why the amendments)

| Choice | Finding | Action |
|---|---|---|
| Visibility raster front end | Wins over deferred as triangle density rises (−32 % pass cost at ~1 px triangles; up to 6.5× with software VRS); slightly slower only on huge-triangle scenes | keep |
| Hand-written binary SAH BVH | CWBVH is 1.9–2.1× faster for incoherent rays at 35–60 % memory; tinybvh provides builders, TLAS and CWBVH layout with sample GPU traversal | **replace** |
| Tier B "when available" | Pascal never got `VK_KHR_ray_query`; early drivers mis-reported support | **Tier A is primary; smoke-test AS** |
| Back-projected ReSTIR reuse | Reservoir splatting (SIGGRAPH 2025) is 5–10 % faster / 10–20 % lower error but requires full-path (GRIS/Area ReSTIR) reservoirs | keep classic for R5/R6; splatting → R7 |
| GPU-driven resident scene | mainstream; composes with visibility buffer | keep |
| Internal denoiser | writing a production denoiser is its own project | à-trous first, NRD optional R6b |
