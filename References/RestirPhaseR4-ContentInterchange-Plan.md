# 🧩 R4 — ContentInterchange: plan (v1, 2026-09-04)

Phase R4 of `RestirRealtimeArchitecturePlan-v2.md` (v2.2). Decisions it implements are §7 of
`MaterialSystemResearch-2026.md` (naming, slab list + per-tier cap, `slate_` extras, linear Rec.709, data-only scene graph).
Nothing here is code yet — this document is for approval.

## 0. Split proposal — R4a data contract, R4b shading

The v2.2 table lists both the data contract (descriptors, textures, scene graph, FBX/OBJ) **and** the new lobes
(EON, GGX + Kulla–Conty, coat, fuzz LTC, thin-film) under R4. Those two halves have opposite acceptance criteria:
the data half must leave Cornell and Sponza **pixel-identical** (proves the new records are read correctly), while
the shading half changes every pixel by design (proved by white-furnace tests). Doing both at once would leave no
way to tell a record bug from a BRDF change.

Proposal: **R4a = this plan** (data contract, textures resident and sampled for base colour + normal, kernel
otherwise unchanged) → hardware check → **R4b = shading** (its own short plan: EON / GGX-KC / coat / fuzz /
thin-film, furnace set, Standard Shader Ball). ⚠️ This is a deviation from the v2.2 row and needs your yes.

## 1. Scope of R4a

| # | Deliverable | Where |
|---|---|---|
| 1 | `MaterialDescriptor` — authoring record: `Slabs[]` (one `MaterialSlabDescriptor` = 49 OpenPBR params + `slate_haziness_*`, `slate_glint_*` + a `TextureReference` per texturable channel) and `Operations[]` (`VerticalLayer`, `HorizontalMix(mask)`, `Weight`, `Coverage`) as a post-order list; `Flags` (double-sided, alpha mode + cutoff, unlit); `Name` | `Engine/ContentInterchange/MaterialDescriptor.h` |
| 2 | `MaterialIndex` — resident table addressed by material id. Owns the **GPU record layout** and the **flatten** step: `Register(descriptor)` → `Finalise(slab_limit)` produces `MaterialRecord[]` (header) + `MaterialSlabRecord[]` (per slab, ≤ `slab_limit`, hard ceiling 8). Horizontal mixes fold into their slab; verticals past the cap fold by OpenPBR §3.10 albedo scaling; every fold is reported once in the log. Replaces `RadianceStructure` / `MaterialRecord` (slang) / `GpuMaterial` | `Engine/ContentInterchange/MaterialIndex.{h,cpp}` |
| 3 | `MaterialCodec` — glTF material ⇄ `MaterialDescriptor`: core PBR + `KHR_materials_{emissive_strength, ior, specular, clearcoat, sheen, transmission, volume, dispersion, iridescence, anisotropy, diffuse_transmission, unlit}` + `extras.slate_*` and `extras.slate_slabs` (the full slab/operation graph when there is more than one slab; single-slab materials round-trip through plain glTF). Also FBX (ufbx `ufbx_material_pbr_maps` → OpenPBR is ~1:1) and OBJ `.mtl` (Kd/Ks/Ns/Ni/d/Ke/map_*) → descriptor (Simple class) | `Engine/ContentInterchange/MaterialCodec.{h,cpp}` |
| 4 | `TextureIndex` — bindless image table: decode (stb_image; 8-bit sRGB/linear per channel semantics, 16-bit PNG, HDR float), CPU box mip chain, one `VkImage` per texture, **one `sampler2D[]` binding** via Vulkan 1.2 descriptor indexing (`runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `shaderSampledImageArrayNonUniformIndexing` — Pascal/GTX 1060 supports all three). Capacity from `[render] texture_limit` (default 1024). Dedup by path; missing files → 1×1 white/flat-normal placeholder + one log line | `Engine/ContentInterchange/TextureIndex.{h,cpp}`, `SwapchainExchange` (device features, binding 10) , `VisibilityExchange` (binding for SurfaceResolve albedo debug view) |
| 5 | `PlacementRecord` scene-graph rows in `SceneStructure`: `{ Name, Ancestor, FirstDescendant, NextPeer, LocalTransform[16], WorldTransform[16], Instance, Camera, Luminaire }` (0xFFFFFFFF = none). glTF nodes, FBX nodes, OBJ groups all produce them. `CameraRecord { Fov, Aspect, Near, Far }` and `PunctualLuminaireRecord { Category, Colour, Intensity, Range, InnerCone, OuterCone }` (KHR_lights_punctual / FBX lights; **stored only**, not yet lit — flagged). No UI | `SceneStructure.{h,cpp}` |
| 6 | Codecs under ContentInterchange: `SceneCodec` (glTF, **moved** from GeometricRaster, gains placements/cameras/lights/textures), `FbxCodec` (ufbx: `target_axes = right_handed_z_up`, `target_unit_meters = 1`, triangulation via `ufbx_triangulate_face`, per-material parts), `ObjCodec` (fast_obj: fan triangulation, groups → placements, `.mtl` via MaterialCodec). Common entry `ContentCodec::Decode(path, …)` dispatches on extension | `Engine/ContentInterchange/{SceneCodec,FbxCodec,ObjCodec,ContentCodec}.{h,cpp}` |
| 7 | Kernel / raster: `SceneRecords.slang` gets `MaterialRecord` + `MaterialSlabRecord`; `ReSTIRViewport.slang` reads `Materials[id].Albedo/Emissive` from the header (so Cornell stays identical) and, when `BaseColourTexture != 0xFFFF`, multiplies by the sampled texel using the hit's UV (barycentrics already come back from `TraverseClosest`; primary hits interpolate from `Vertices[]`). Normal map applied to the shading normal when present. `SurfaceResolve` albedo debug view samples the texture too; new debug view `Uv` | `Engine/Shaders/*.slang` |
| 8 | Vertex UVs for the kernel: the flat `Triangles[]` SSBO (48 B/tri) loses its `normal` payload and gains per-vertex UV (`u0v0 u1v1 u2v2`) — same size. ⚠️ Flat buffer still retained (R5 removes it as planned) | `SwapchainExchange.h` (`TriangleIndex`), `SceneStructure::Finalise` |
| 9 | Config: `[render] slab_limit` (Tier A default 1, Tier B/C 4, ceiling 8) and `[render] texture_limit` (1024) | `ConfigurationStructure/Registry` |
| 10 | Build: `ExternalPackages/ufbx` (@fcc5d6ba) and `ExternalPackages/fast_obj` (@d620667f) as submodules; `stb` already listed; source/include lists in `CMakeLists.txt` and `ToolchainSequence.ps1`; the shader include stamp list | build files |

Removed: `RadianceStructure`, `ReSTIRIntegrator::BuildRadianceStructures` (Cornell export goes through `MaterialDescriptor`), `Materials` SSBO of `RadianceStructure`.

## 2. GPU record layout (owned by `MaterialIndex`)

```
MaterialRecord        64 B   (std430)
  vec4 Albedo          xyz = flattened base colour (linear Rec.709), w = flattened specular roughness
  vec4 Emissive        xyz = emission_luminance × emission_color [nit], w = flattened metalness
  uint SlabOffset      first MaterialSlabRecord
  uint SlabCount       1 … slab_limit
  uint Flags           bit0 double-sided · bit1 alpha mask · bit2 alpha blend · bit3 unlit · bit4 thin-walled · bit5 emissive
  uint Complexity      0 Simple · 1 Single · 2 Complex · 3 ComplexSpecial  (selects the R4b permutation)
  uint BaseColourTexture · NormalTexture     (0xFFFFFFFF = none)   — header fast path for Tier A
  float AlphaCutoff · float Pad

MaterialSlabRecord   256 B   (std430) — OpenPBR §5 in spec order, floats, no packing this phase
  vec4 ×14  base(weight, color³) · base(metalness, diffuse_roughness, spec_weight, spec_roughness) · specular(color³, anisotropy)
            · specular(ior, transmission_weight, transmission_depth, transmission_scatter_anisotropy) · transmission_color³+dispersion_scale
            · transmission_scatter³ + abbe · subsurface(weight, color³) · subsurface(radius, radius_scale³) · subsurface_anisotropy + coat(weight, roughness, anisotropy)
            · coat(color³, ior) · coat_darkening + fuzz(weight, roughness) + thin_film_weight · fuzz_color³ + thin_film_thickness
            · thin_film_ior, emission_luminance, geometry_opacity, slate_haziness_weight · emission_color³ + slate_haziness_roughness
  uint  ×8  slate_glint(density, uv_scale as float bits) · 16 × uint16 texture slots packed in 6 uints
            (base_color, metalness, roughness, specular, normal, coat_normal, emission, opacity, transmission, subsurface, coat, fuzz, thin_film, anisotropy, occlusion, mask)
```
⚠️ Deviation from the research §4.4 target of ≤ 128 B: full floats first, half/UNORM packing when R4b fixes which
channels the shader really reads. 2 000 materials × 4 slabs × 256 B = 2 MB — still trivially resident.

Bindings after R4a (compute set 0): 0 out · 1 Triangles · 2 **MaterialRecords** · 3 history · 4 surface · 5 normal · 6 instances
· 7 luminaires · 8 CWBVH nodes · 9 CWBVH tris · **10 MaterialSlabRecords · 11 Vertices · 12 Indices · 13 sampler2D Textures[]**.

## 3. Flatten rules (`MaterialIndex::Finalise`)

1. Evaluate `Operations[]` post-order on constant parameters (textures are carried by reference, not resampled).
2. `HorizontalMix(a, b, t)` → lerp every parameter; texture slots: keep `a`'s unless `t ≥ 0.5` constant; if the
   mask is a texture the mix is kept as **two slabs + mask slot** when the cap allows, otherwise folded at `t`.
3. `VerticalLayer(top, bottom)` → if slab budget remains, both stay (top first); else OpenPBR §3.10: bottom albedo
   scaled by `(1 − top_weight · E_top)` where `E_top` is the top slab's directional albedo at normal incidence
   (Fresnel `((n−1)/(n+1))²` for coat/dielectric, `fuzz_weight` for fuzz).
4. `Weight`, `Coverage` multiply `base_weight` / `geometry_opacity`.
5. The header's `Albedo/Emissive/roughness/metalness` come from the **bottom-most opaque slab after folding** — for
   every glTF material we have today that is bit-identical to what `DecodeMaterial` produces now.

## 4. Proofs (CPU harness; hardware items wait for the GTX 1060)

* **Cornell round trip**: `CornellBox.gltf` → `MaterialDescriptor[]` → `MaterialIndex` → header `Albedo/Emissive`
  compared field-by-field to the R3 `RadianceStructure` values (must be identical) — and the exported glTF must be
  byte-identical to the current file.
* **Sponza**: 25 materials → descriptors; 69 textures decoded (count, dimensions, mip levels, bytes) — log table in
  the phase note; kernel/resolve albedo view now textured (screenshot = hardware item).
* **KHR extension coverage**: a synthetic glTF with every supported extension → descriptor → glTF (`extras.slate_slabs`)
  → descriptor equality.
* **Slab cap**: a 3-slab car-paint descriptor flattened at `slab_limit` 1, 2, 4 — printed slab tables + the fold log.
* **FBX / OBJ**: decode ufbx's `data/` samples and a generated `.obj/.mtl`; print placements tree, materials,
  triangle counts; OBJ ↔ glTF triangle-count and bounds equality for the same geometry.
* `PlacementRecord` tree dump for Sponza (105 instances under their glTF nodes) — data only.
* Syntax check of every touched TU (`chk.sh`), shaders eyeballed (no GLSL compiler in the sandbox — unchanged limitation).

Hardware acceptance (yours): Cornell identical to R3; Sponza with textured albedo in the kernel and in the F3 albedo
view; `Materials: N descriptors → N records, S slabs (limit L), T textures (M MB)` log line; FBX/OBJ file opens with
`--scene`.

## 5. Out of scope (explicit)

Shading lobes (R4b) · outliner UI · MaterialX/USD codecs · texture streaming / KTX2 / BC compression (all textures
are resident RGBA8/RGBA16F this phase — Sponza ≈ 350 MB uncompressed at full mips; a `texture_limit`-driven
downscale keeps 1060-class cards safe) · punctual lights lighting the scene (stored only) · animation.

## 6. Order of work (one commit per row, all on `arena/01a0683c-slate`)

1. `MaterialDescriptor.h`, `MaterialIndex`, `MaterialCodec` (glTF only) + Cornell identity harness.
2. `SceneCodec` move + placements/cameras/lights + `TextureIndex` (CPU side) + Sponza table.
3. Shader records, `SwapchainExchange` descriptor indexing, `VisibilityExchange` binding, `TriangleIndex` UV payload.
4. `FbxCodec`, `ObjCodec`, `ContentCodec`, submodules, build lists.
5. Phase note `References/RestirPhaseR4a-ContentInterchange.md`, harnesses into `Scratchpad/`.
