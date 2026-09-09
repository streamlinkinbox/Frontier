# 🧩 Phase R4b — Shading: the OpenPBR lobe set in the Tier A kernel (plan, for approval)

Plan v2.2 phase R4, second half. R4a (d05e91f) delivered the data: `MaterialRecord` / `MaterialSlabRecord` on
bindings 2 / 10, bindless `Textures[]` on 11, UVs in `TriangleIndex`. The kernel still shades **Lambert × emitter**
only (`albedo · L · cosθ / π`), ignores roughness / metalness / normal maps, and samples textures at LOD 0. R4b makes
the kernel evaluate what the records describe — no new data formats, no UI.

## 0. Scope line

**In:** BSDF evaluation + importance sampling for one resolved slab (Tier A, `slab_limit 1`); shading normal from
vertex normals + normal map; ray-cone texture LOD; metal/rough/emissive/opacity texture channels; alpha-mask
visibility; white-furnace + Shader Ball proofs; debug views for roughness / metalness / shading normal.
**Out (explicit):** multi-slab evaluation (Tier B, waits for R5's permutation system), subsurface / transmission /
dispersion / volume (need path-traced interiors — R7), punctual lights lighting the scene (R6 with ReSTIR DI light
lists), glints (Complex-Special, R6+), texture compression / streaming.

## 1. Lobe stack (OpenPBR §5 order, evaluated bottom-up with §3.10 albedo scaling)

| # | Lobe | Method | Inputs from `MaterialSlabRecord` | Cost note |
|---|---|---|---|---|
| 1 | Diffuse | **EON** (Portsmouth–Kutz–Hill 2025, energy-preserving Oren–Nayar, the reference GLSL, CLTC sampling) | `base_color`, `base_weight`, `base_diffuse_roughness` | ≈ Lambert + a few FMA; roughness 0 ⇒ bit-exact Lambert (Cornell identity) |
| 2 | Dielectric specular | GGX, Smith height-correlated, **Dupuy–Benyoub spherical-cap VNDF** sampling, **Kulla–Conty / Turquin** multiple-scattering compensation | `specular_weight`, `specular_color`, `specular_roughness`, `specular_roughness_anisotropy`, `specular_ior` | 1 LUT fetch (E(μ,α) 32×32 R16F baked at start-up) |
| 3 | Metal | same GGX with **F82-tint** Fresnel (Kutz–Hašan–Edmondson) | `base_metalness`, `base_color` (F0), `specular_color` (F82) | free over #2 |
| 4 | Thin-film | Belcour–Barla 2017 airy fit modulating #2/#3 Fresnel | `thin_film_weight`, `_thickness`, `_ior` | small polynomial, gated by weight > 0 |
| 5 | Haziness | second GGX at `slate_haziness_roughness` mixed by `slate_haziness_weight` (Barla 2018) | `slate_haziness_*` | 2nd GGX eval, gated |
| 6 | Coat | dielectric GGX on top; base darkened + tinted by `coat_color`, `coat_darkening` per §5.6 | `coat_weight`, `coat_color`, `coat_roughness`, `coat_ior`, `coat_darkening`, coat normal | gated |
| 7 | Fuzz | **Zeltner–Burley–Chiang LTC sheen** (32×32 LTC table baked at start-up) | `fuzz_weight`, `fuzz_color`, `fuzz_roughness` | 1 LUT fetch, gated |
| 8 | Emission | `emission_luminance × emission_color` × emission texture | unchanged path | — |
| 9 | Opacity | `geometry_opacity` × opacity texture; `kMaterialFlagAlphaMask` ⇒ cutoff test in **shadow rays and primary raster** (Sponza foliage) | `AlphaCutoff` | any-hit style re-trace on mask |

Everything gated by weight so a plain wall pays for #1 + #2 only (Complexity 0 path). Importance sampling: one-sample
MIS between diffuse (CLTC) and the dominant GGX lobe, lobe pick by Fresnel-weighted albedo estimate — feeds both the
RIS target function (`p̂ = f · L · cosθ / d²` now uses the real BSDF, not albedo/π) and the GI bounce direction.

## 2. Geometry inputs

* **Shading normal**: `Vertices[]` / `Indices[]` uploaded as compute bindings **12 / 13** (this is the deferred plan-R4a
  item — needed now for interpolated normals + tangents). Barycentrics from hit position (existing) → interpolated
  N, T, UV. Normal map (`geometry_normal` channel, `NormalScale`) in tangent space; MikkTSpace-compatible handedness
  from `tangent.w`. Missing tangents ⇒ derived from UV derivatives of the triangle (same formula the R2 codec would
  use). Coat normal channel handled identically. Back-facing shading normals are flipped toward the geometric one.
* **Ray cones** (Akenine-Möller 2019/2021): cone spread from pixel angle; primary width from depth, secondary widened
  by surface curvature approximation (per-triangle constant); LOD = log2(width · uvArea/worldArea) → `textureLod`.
* **Texture channel set wired**: base colour, metalness, roughness (R / G channel select from the record), normal,
  emission, opacity, occlusion (multiplies the debug ambient floor only — no AO in the path tracer proper).

## 3. Files

| File | Change |
|---|---|
| `Engine/Shaders/MaterialEvaluation.slang` (new, include) | `ResolveMaterial(hit) → ShadingRecord`, `EvaluateBsdf`, `SampleBsdf`, `PdfBsdf`; EON, GGX/VNDF, F82, thin-film, LTC fuzz, coat; `SampleTextureCone` |
| `Engine/Shaders/ReSTIRViewport.slang` | sections 3/4 call the BSDF; alpha-mask shadow rays; bindings 12/13/14/15 (vertices, indices, GGX energy LUT, LTC table) |
| `Engine/Shaders/SurfaceResolve.slang` | alpha-mask discard for primary visibility (re-sample texture at the raster hit); new debug views **Roughness / Metalness / ShadingNormal** (F3 cycle) |
| `Engine/Shaders/VisibilityRaster.frag.slang` | `discard` on alpha-mask materials (needs UV → bind Materials + Textures in the raster set, binding 12/13 there) |
| `Engine/DeviceExchange/SwapchainExchange.{h,cpp}` | `kComputeBindingCount` 12 → 16; LUT upload (two R16F images, generated on CPU at start-up, ~64 KB) ; vertex/index buffers shared with VisibilityExchange (no duplicate upload) |
| `Engine/DeviceExchange/VisibilityExchange.{h,cpp}` | raster set gains Materials + Textures; `DebugViewCategory` + 3 |
| `Engine/DisplayPresentation/ShadingTableCodec.{h,cpp}` (new) | CPU bake of Kulla–Conty E(μ,α) (importance-sampled, 32×32×256 samples) and the LTC sheen fit table (coefficients from Zeltner et al. supplemental); deterministic, unit-tested |
| `Engine/DisplayPresentation/DiagnosticInspector.cpp` | popup shows material complexity histogram + LOD stats |
| `Projects/Project-Zero/Source/RayTracingSolver.cpp` | Cornell unchanged; a **Shader Ball** scene generator (`--scene shaderball`) : 6 × 6 grid of spheres, roughness × metalness, plus one row each for coat / fuzz / thin-film / haziness, lit by one area light — the standard furnace layout |

No CMake / ps1 changes beyond the two new TUs. No new config keys (`slab_limit` already selects Tier A).

## 4. Proofs (CPU harness + shader-port unit test, like R3)

1. **Cornell identity**: EON at roughness 0 ⇒ Lambert; `RoughnessValue` in `RayTracingSolver` is currently ignored,
   so Cornell WILL change once GGX is on (walls get a dielectric specular lobe). To keep the R3 reference valid the
   Cornell materials get `specular_weight = 0` explicitly (byte-identical glTF is then no longer possible — the file
   gains `KHR_materials_specular {specularFactor: 0}`; I will re-export and record the new reference hash). ⚠️ Flagging
   this: alternative is leaving Cornell as-is and accepting a slightly glossier reference. **Your call — default: pin
   specular 0 so the R0→R3 image stays the comparison baseline.**
2. **White furnace** (OpenPBR §3.11): C++ port of the GLSL BSDF (`Scratchpad/MaterialEvaluationTest.cpp`, same 1:1
   port discipline as `TraversalShaderPortTest`) integrated over the hemisphere with 1 M samples at μ ∈ {0.05…1},
   α ∈ {0.05…1}: EON albedo = base_color ± 0.5 %; compensated GGX = 1.0 ± 1 % for white F0; uncompensated shown next
   to it (the classic 30–40 % loss at α = 1); fuzz LTC ≤ 1; coat + base stack ≤ 1.
3. **Reciprocity**: `f(ωi,ωo) = f(ωo,ωi)` on 10 k random pairs for every lobe (EON is reciprocal by construction).
4. **Sampling consistency**: `E[f cosθ / pdf]` matches the furnace integral within 1 % for VNDF + CLTC + MIS.
5. **Ray-cone LOD**: analytic checkerboard at known distance → chosen LOD vs. expected (±0.5 level).
6. **Alpha mask**: Sponza foliage triangles — shadow-ray hit count with and without the mask (numbers in note).
7. Shader eyeball only (unchanged sandbox limit) — the C++ port is the executable proof of the math.

Hardware acceptance (yours): Cornell still matches the pinned reference; Shader Ball screenshot (roughness ×
metalness grid reads correctly, no dark ring at grazing, no energy loss at α = 1); Sponza with normal maps, textured
roughness/metal, foliage cut-outs in both view and shadows, F3 Roughness / Metalness / ShadingNormal views; F3 popup
shows `kernel` ms — budget ≤ +25 % over R4a at 1080p on the 1060 for Sponza (Simple/Single materials dominate).

## 5. Order of work (one commit per row)

1. `ShadingTableCodec` + `MaterialEvaluation.slang` + C++ port harness (furnace, reciprocity, sampling) — math first.
2. Bindings 12–15, `ResolveMaterial` (interpolated normals, normal maps, ray cones, texture channels), kernel wiring.
3. Alpha mask in raster + resolve + shadow rays; debug views; Shader Ball scene; Cornell specular pin + re-export.
4. Phase note `RestirPhaseR4b-Shading.md`.

## 6. Open decision for you

* Cornell baseline: **pin `specular_weight = 0`** (recommended) or let the reference image change?
