# 🧩 Phase R4a — ContentInterchange (materials, bindless textures, scene-graph records, FBX / OBJ)

Plan v2.2 phase R4, first half (data contract). Plan approved in `RestirPhaseR4-ContentInterchange-Plan.md`; five rows,
one commit each on `arena/01a0683c-slate`: f37a6b3 (row 1) · 99b2319 (row 2) · 38f77cb (row 3) · 95c4f65 (row 4) ·
this note (row 5). R4b (shading lobes) follows after the hardware check.

## 1. What it does

| Piece | File | Role |
|---|---|---|
| Descriptor | `Engine/ContentInterchange/MaterialDescriptor.h` | CPU authoring form: `Slabs[]` (OpenPBR §5 parameter set + `slate_haziness_*`, `slate_glint_*`, 16 texture channels with UV set / channel selection) and `Operations[]` (mix / layer). Never uploaded. |
| Index | `MaterialIndex.{h,cpp}` | Flattens descriptors into the GPU tables: `MaterialRecord` 64 B (albedo / roughness / emissive / metalness / slab range / flags / complexity / base + normal texture slot / alpha cutoff / normal scale) and `MaterialSlabRecord` 288 B (58 floats + flags + 16 × uint16 texture slots + UV-set bits + runtime scalars). `Finalise(SlabLimit, Report)` folds slabs past the per-tier cap and reports every fold. |
| Codec | `MaterialCodec.{h,cpp}` | glTF ⇄ descriptor (core + 10 ratified `KHR_materials_*` + `KHR_texture_transform` + `extras.slate_slabs / slate_operations` for what glTF cannot say), ufbx material → descriptor (PBR maps first, legacy Phong shininess → GGX roughness fallback), `.mtl` → descriptor. |
| Textures | `TextureIndex.{h,cpp}` | Register-by-path / register-encoded-bytes (GLB buffer view, FBX embedded blob) with (path, colour-space) de-duplication, deferred `Decode(MaximumEdge, Report)` through stb_image, sRGB-aware box mip chains, RGBA8 sRGB / RGBA8 linear / RGBA16F, white / flat-normal placeholders on failure. |
| Scene graph | `GeometricRaster/SceneStructure.{h,cpp}` | Owns a `MaterialIndex`; adds `PlacementRecord` (Name, Ancestor, FirstDescendant, NextPeer, Local, World, instance range, camera, luminaire), `CameraRecord`, `PunctualLuminaireRecord` — data only, no UI. Flat `TriangleIndex` carries three vertex UVs (64 B). |
| glTF | `ContentInterchange/SceneCodec.{h,cpp}` (moved from GeometricRaster) | Whole node tree → placements (DFS, ancestors first), cameras, `KHR_lights_punctual`, textures via URI or GLB view. `Encode` unchanged in output (Cornell byte-identical). |
| FBX | `FbxCodec.{h,cpp}` + `UfbxTranslation.cpp` | ufbx with `target_axes = right-handed Z-up`, `target_unit_meters = 1`, `MODIFY_GEOMETRY` — CLAUDE.md §7 space straight out of the loader; n-gons via `ufbx_triangulate_face`; per-node material overrides; pivots through `geometry_to_world`; embedded textures. |
| OBJ | `ObjCodec.{h,cpp}` | fast_obj; objects → groups → whole file as placements; per-material splits; fan triangulation; flat normals when the file has none; `.mtl` maps relative to the file. Y-up assumed (glTF swap) + `--scale` for units. |
| Dispatcher | `ContentCodec.{h,cpp}` | `.gltf/.glb/.fbx/.obj` by extension; `GameExecution --scene` now opens any of them. |
| GPU | `DeviceExchange/SwapchainExchange.{h,cpp}`, `VisibilityExchange.cpp`, `Shaders/SceneRecords.slang`, `Shaders/ReSTIRViewport.slang` | Compute set 0: binding 2 `MaterialRecord[]`, 10 `MaterialSlabRecord[]`, 11 `sampler2D Textures[]` (Vulkan 1.2 descriptor indexing: runtime array, partially bound, variable count 1024, update-after-bind; one optimal-tiled image + full mip chain per texture, uploaded in one staging submission). Kernel derives the face normal from the triangle edges (the normal slot became UVs) and samples base colour × `baseColorFactor` at LOD 0 via barycentrics rebuilt from the hit position. Resolve pass binding 5 reads the new 64 B record (albedo view stays valid). |
| Config | `[render] slab_limit` (1–8, default 1 = Tier A flattened), `[render] texture_edge_limit` (0 = unlimited, default 2048) | Read/written by `ConfigurationRegistry`; `GameExecution` passes both through. |
| Build | `CMakeLists.txt`, `ToolchainSequence.ps1` | ContentInterchange TUs, `ufbx` / `fast_obj` / `stb` submodules (ufbx v0.23.0 fcc5d6b, fast_obj d620667, stb 2c980bb), include roots. PS 5.1-safe. |

Bindings 12/13 (vertex / index buffers for the kernel) from the plan were **not** added: the flat `TriangleIndex` already
carries the three UVs, so the kernel needs nothing else until R5 replaces the flat list with the BLAS payload. Recorded
as a deviation from plan §2; no functional gap.

## 2. Proofs (CPU harnesses in `Scratchpad/`, run in this sandbox — no Vulkan runtime here)

### 2.1 `MaterialIndexTest.cpp` (row 1, unchanged since f37a6b3)

* Cornell: 7 / 7 header records bit-identical to the R3 `RadianceStructure` values; 7 slab records × 288 B + 64 B header.
* Encoded `material_3` emits `emissiveStrength 32.0` + `KHR_materials_emissive_strength`.
* KHR round trip (synthetic glTF, 10 extensions) → descriptor → glTF → descriptor: every field within 1e-4.
* Car paint (3 slabs): `slab_limit 1` → 1 slab (2 folded) · `2` → 2 (1 folded) · `4` → 3 (0 folded), Complexity 2; extras
  round trip equal (763 B).

### 2.2 `SceneCodecR4Test.cpp` (row 2)

```
repo CornellBox.gltf 8463 B, re-export 8463 B, BYTE-IDENTICAL
decode: 36 tris, 6 instances, 7 materials, 2 luminaires, 1 placements
Sponza: 262267 tris, 105 instances, 26 materials (25 + fallback), 1 placements (glTF has one node), 105/105 instances attached
Textures: 69 resident (0 placeholder), 362.7 MB with mips, decoded in 2154 ms
   1024x1024 · 11 mips · 5461 KB each (sRGB8 for base colour / emissive, linear8 for normal / metal-rough), white.png 4x4
sRGB-aware mip: 2x2 black/white checker -> 1x1 = 188 (linear-correct 188, naive average would be 128)
material 0: baseTex slot 0, normalTex slot 2 -> 5061699253647017043.png
```

362.7 MB is the full-mip uncompressed footprint at `texture_edge_limit = 0`; the default 2048 leaves Sponza unchanged
(all 1024²). Compression / streaming is R4b+ (plan §5).

### 2.3 `ContentCodecR4Test.cpp` (row 4) — axis + unit proof across three DCC tools

ufbx ships `<name>.obj` dumps of its FBX test scenes in each tool's **native** frame. FbxCodec (ufbx → Z-up metres) and
ObjCodec (Y-up assumption + `UniformScale`) must land on the same world-space positions:

```
Blender (Z-up, m)        blender_279_ball          80 tris   unique positions FBX 42  / OBJ 42   IDENTICAL
Maya    (Y-up, cm)       maya_child_pivots         36 tris   unique positions FBX 24  / OBJ 24   IDENTICAL   (3-deep pCube1 > pCube2 > pCube3 with pivots)
3ds Max (Z-up, inches)   max2009_blob             832 tris   unique positions FBX 450 / OBJ 450  IDENTICAL   (Box01 + 16 child pyramids, 1 camera, 3 lights)
```
(mm-rounded; the OBJ side is re-expressed as (X, Z, −Y) for the Z-up tools and scaled 0.01 / 0.0254 for cm / inches.)

Materials + textures (FBX): `blender_293_textures` → `checkerboard_diffuse.png` on base colour, 4 textures;
`blender_293_embedded_textures` → 4 textures from embedded blobs; `max_physical_material_textures` → 9 textures, base
`Map #3`, normal `Map #9`; `blender_402_material_chart` → 7 materials incl. emissive 0.2 / 0.1 rows. Roughness values are
ufbx's own PBR mapping (Blender writes shininess = (1 − r)² · 100).

Cameras + lights (FBX): Blender default → `Camera` vfov 28.8°, aspect 1.78, near 0.10, far 100; `Lamp` point. Max blob →
directional (0.53 0.86 0.65), point, spot 43°/45°.

OBJ + MTL: `blender_279_ball` Red / White (Ns → roughness 0.38); `synthetic_color_suzanne` 968 tris with `.bmp` base
texture; `blender_331_space texture` resolves `space dir/space tex.png` (paths with spaces). Dispatcher rejects `.blend`
with an explanatory message.

### 2.4 Static checks

`chk.sh` clean on every touched TU (SceneStructure, TextureIndex, SceneCodec, FbxCodec, ObjCodec, ContentCodec,
UfbxTranslation, SwapchainExchange, VisibilityExchange, ReSTIRIntegrator, ConfigurationRegistry, RenderScheduler,
GameExecution). Shaders eyeballed only (no GLSL compiler in the sandbox — unchanged limitation).

## 3. Hardware acceptance (yours, GTX 1060)

1. Cornell renders identical to R3 (materials now come through `MaterialIndex`; kernel normal now derived from edges).
2. `--scene Sponza.gltf`: textured albedo in the kernel and in the F3 **Albedo** view.
3. Log lines: `Materials: 26 descriptors -> 26 records, 26 slabs (limit 1, 0 folded), 1 placements, 0 cameras, 0 punctual lights`,
   `Textures: 69 resident (0 placeholder), … MB with mips, decoded in … ms`, `[SwapchainExchange] Textures: 69 resident (… MB) in the bindless table.`
4. `--scene something.fbx` / `.obj` opens (any of the ufbx `data/` files works; add `--scale 0.01` for Maya cm files).
5. If the driver refuses descriptor indexing you get one log line and constant-colour materials, never a crash.

## 4. Deviations / notes

* ⚠️ Texture LOD is 0 (no ray differentials yet); R4b adds ray cones. Anisotropic filtering not requested yet.
* ⚠️ FBX light intensity is ufbx's dimensionless value (FBX has no photometric unit); glTF lights keep cd / lux.
* ⚠️ OBJ has no axis declaration; Y-up assumed (Blender's exporter default). For Z-up-native tools pass the file
  through FBX/glTF or accept the swap — the harness shows exactly which transform is applied.
* Slot 0 of every OBJ material list is fast_obj's built-in default; an extra `fallback` slot is appended by all codecs.
* Plan §2 bindings 12/13 dropped (see §1).
