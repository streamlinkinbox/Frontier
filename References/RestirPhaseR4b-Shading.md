# 🧩 Phase R4b — Shading: the OpenPBR lobe set in the Tier A kernel

Plan v2.2 phase R4, second half (shading). Plan approved in `RestirPhaseR4b-Shading-Plan.md` with the
recommended choice (Cornell `specular_weight = 0` pin); four rows, one commit each on
`arena/01a0683c-slate`: row 1 (BSDF + LUT bake + furnace) · row 2 (kernel wiring + resolve proof) ·
row 3 (`9b0f6e2`: alpha mask, debug views, shader ball, Cornell pin) · this note (row 4).
R4a (`d05e91f`) delivered the data; R4b makes the kernel evaluate it. No new data formats, no UI.

## 1. What it does

| Piece | File | Role |
|---|---|---|
| BSDF library | `Engine/Shaders/MaterialEvaluation.slang` (607 lines, include-only) | `ShadingRecord`; Fresnel (exact dielectric, OpenPBR `specular_weight` IOR modulation, Schlick, F82-tint metal, hemispherical average); Belcour–Barla thin film (2-term Airy, XYZ→Rec.709); anisotropic GGX D/Λ/G1/G2 + Dupuy–Benyoub spherical-cap VNDF; Kulla–Conty multiple-scatter term (reciprocal, split-sum LUT); EON diffuse + CLTC sampling (paper Listings 1–3 verbatim maths); Zeltner LTC fuzz; `ResolveLayers` (coat IOR w/ TIR inversion, roughening, darkening Δ, view-dependent absorption, lobe probabilities); `EvaluateBsdf` / `PdfBsdf` / `SampleBsdf`. Bindings 13/14 (`EnergyLut`/`SheenLut`) behind `FRONTIER_CPU_PORT`. |
| LUT bake | `Engine/DisplayPresentation/ShadingTableCodec.{h,cpp}` + `LtcSheenTable.h` | Deterministic Hammersley bake of the GGX energy table (32×32 RGBA32F: A, B, E_avg — `E_ss(μ,α) = F0·A + B`, height-correlated Smith, 4096 VNDF samples/cell) and the LTC sheen upload (`aInv, bInv, R` by [α][cosθ], 1024 entries verbatim from `tizian/ltc-sheen`, Apache-2.0). Added to `CMakeLists.txt` + `ToolchainSequence.ps1`. |
| Kernel resolve | `Engine/Shaders/ReSTIRViewport.slang` (+~290 lines) | `ResolveMaterial(mat, instance, primitive, hitPos, viewDir, coneWidth)` → `ShadingRecord` + `SurfaceFrame`: barycentrics rebuilt from the hit position → interpolated vertex N/T/UV from `Vertices[]`/`Indices[]`; Gram-Schmidt frame with `tangent.w` handedness; UV-derived tangents when none authored; tangent-space normal map × `NormalScale` (rejected below the geometric plane); viewer-facing flip. All slab fields mapped; per-channel textures (base RGB, metalness B, roughness G, opacity A, occlusion R, others RGB). Ray cones: `w = t·(2·tan(fov/2)/H)` primary, widened by bounce distance; `λ = ½log2(uvArea/worldArea) + log2(w/\|n·d\|) + ½log2(texW·texH)`. DI now `f·L·cos·W/d²` from `EvaluateBsdf`; the bounce is BSDF-sampled (`f·cos/pdf`) with NEE at the second vertex through the BSDF; emission from the resolved slab. |
| Bindings | `SwapchainExchange.{h,cpp}` (`kComputeBindingCount` 12→16) | 11 vertices, 12 indices (shared with `VisibilityExchange`, no duplicate upload), 13 energy LUT, 14 sheen LUT (RGBA32F 32² ×2, clamp sampler, `UploadShadingTables`), 15 bindless `Textures[]` (**moved from 11** — the variable-count binding must stay last). Pool/layout/writes updated; without descriptor indexing the table is 1 slot and materials stay constant (one log line, no crash). |
| Alpha mask | `VisibilityRaster.{vert,frag}.slang`, `ReSTIRViewport.slang`, `GameExecution.cpp` | One rule in three places: `geometry_opacity × baseColor.a < AlphaCutoff` ⇒ cut-out. Raster: VS passes `Texcoord` (location 3); FS discards; raster set gains bindings 5 (materials), 6 (slabs), 7 (bindless table, variable-count, partially bound) — without descriptor indexing the layout is unchanged (0–3) and the mask is off. Kernel: `AlphaMaskCutOut()`; `TraceScene` re-traces past cut-outs (≤ 4 layers); `TraceShadow` keeps the any-hit fast path when the count is 0, otherwise a closest-hit walk through cut-outs. Push constant: unused `TriangleCount` slot repurposed as `AlphaMaskedMaterialCount` (96-byte layout unchanged); counted from material flags in `GameExecution.cpp`. |
| Debug views | `SurfaceResolve.slang`, `VisibilityExchange.h` | Roughness (8, `Albedo.w`), Metalness (9, `Emissive.w`), ShadingNormal (10, barycentric-interpolated vertex normal). ⚠️ ShadingNormal excludes the normal map (would need slab + texture table in the resolve set). |
| Shader ball | `ContentInterchange/ShaderBallStructure.{h,cpp}`, `GameExecution.cpp`, `SceneCodec` | 24 spheres (24 rings × 48 segments, smooth corner normals, UVs) on a 6×6 m EON plane under a 2×2 m / 120-nit luminaire: dielectric roughness ramp ×6, 5 metals + gold-with-film, blue ±coat ×3, velvet fuzz ×2, soap film, emitter sphere, alpha card (opacity 0.3, no texture), haziness ×2, EON r=0/1 pair. `SceneEncodeConfiguration` (name / smooth normals / `TEXCOORD_0`) added to `Encode` with defaults that keep Cornell bytes identical. `--scene shaderball` exports once to `Projects/Project-Zero/Content/Scenes/ShaderBall.gltf` (git-ignored, regenerated on first run). Camera pin: (0, −6.2, 2.6) pitched −22°. |
| Cornell pin | `DisplayPresentation/ReSTIRIntegrator.cpp` (`BuildMaterialDescriptors`) | `SpecularWeight = 0` on all analytical Cornell materials (approved choice): at roughness 0 EON is bit-exact Lambert and the lobe pick draws one extra RNG number, so the converged image is unchanged (noise pattern is not). Re-exported `CornellBox.gltf`: **9067 B, sha256 `93c67d50…`**; geometry buffers byte-identical to the 8463 B R4a file — only `KHR_materials_specular {specularFactor: 0}` blocks added. |
| Extras fix | `ContentInterchange/MaterialCodec.cpp` | Found while pinning: extras only round-tripped `slate_*`; `base_diffuse_roughness`, `coat_ior`, `coat_darkening`, `coat_color`, … were silently dropped on export. Now every OpenPBR scalar glTF cannot carry rides in extras via the `SLATE_PARAM` table. |

## 2. Proofs (CPU harnesses in `Scratchpad/`, same 1:1 port discipline as R3 — the slang text itself compiled as C++)

### 2.1 `MaterialEvaluationTest.cpp` + `.log` — 67/67 PASS

Bake header: `E_ss(0.98, 0.02) = 0.9997`, `E_ss(0.5, 1) = 0.4574` (vs raw slang GGX quadrature 0.4507 ±0.01),
`E_ss(0.1, 1) = 0.7643`, `E_avg(1) = 0.4162`.

* EON: white base = 1.000 ±0.05% at r = 0/0.5/1, μ = 0.95/0.5/0.15; base 0.8 matches the paper closed-form `E_EON` ±0.4%.
* GGX white furnace (F0 = 1): compensated 0.992–1.0005 vs raw single-scatter 0.317 (α=1, μ=0.95) / 0.451 / 0.694 — the 30–70% loss shown side by side. Aniso via `α = √(αx·αy)`: 0.965 at anisotropy 0.5, 0.845 at 0.8.
* Dielectric+EON / thin-film / haziness / fuzz (matches table R ±1%) / clear + tinted coat over metal and diffuse: all ≤ 1 (white dielectric+diffuse 1.0165 ±0.03 — the known albedo-scaling overshoot at grazing, within tolerance).
* Reciprocity (10 k pairs): EON worst 1.2e-7, metal GGX 2.2e-6 — exact; fuzz-LTC and full-stack non-reciprocal by design (table fit / OpenPBR albedo scaling), reported as info.
* Importance sampling (200 k samples): `E[f·cos/pdf]` = quadrature albedo within 1% for all 8 configurations; pdf integrates to 0.93–1.00 (0.932 worst: aniso VNDF at 6.8% below-hemisphere failures).

### 2.2 `MaterialResolveTest.cpp` + `.log` — 48/48 PASS

Frame orthonormal/handed, flip when viewed from behind, 30° vertex-normal blend exact, UV-derived tangent = +X,
`tangent.w = −1` flips B. All 21 slab fields + 4 texture channels land in the right slot (metalness from B = 0.4,
roughness from G = 0.6, opacity 0.75×A(0.8) = 0.6, occlusion `lerp(1, 0.4, 0.5) = 0.7`, normal-map N = (0.6, 0, 0.8)
re-orthogonalised). Ray-cone LOD: 2048² map at 4 m → level 3.13 (analytic 8.8 texels), 256² = −3 levels, 60° grazing
= +1, doubled cone = +1 — all within ±0.5. Resolved full-stack record through the row-1 BSDF: finite, albedo ≤ 1.

### 2.3 `ShaderBallExportTest.cpp` + `.log`

```
[1] Cornell: 6 materials, specular_weight = 0 0 0 0 0 0 — export ok
[2] ShaderBall: 50790 triangles, 152370 corner normals, 26 materials — export ok
[3] decode Cornell: 36 tris, 6 instances, 6 clusters, 7 materials (6 + fallback), 2 luminaires,
    bounds [-1 0 0]..[1 2 2], alpha-masked 0; eon_r1 decodes with BaseDiffuseRoughness = 1.0
[3] decode ShaderBall: 50790 tris, 26 instances, 417 clusters, 27 materials (26 + fallback),
    2210 luminaires (emissive sphere tessellation + quad), bounds [-4 -3 0]..[4 4 4], alpha-masked 1
    (the texture-less alpha card, flags 3, opacity 0.3 — proves the path, not a foliage look)
```

### 2.4 Static checks

All four touched C++ TUs pass `-fsyntax-only` against the overlay (`SwapchainExchange`, `VisibilityExchange`,
`ReSTIRIntegrator`, `GameExecution`); both build lists carry the two new TUs (`ShadingTableCodec`,
`ShaderBallStructure`; PS 5.1-safe). No glslang in this sandbox (the `/home/user/fr` overlay was wiped mid-phase,
so the `chk.sh` shader check could not run) — the shader sections compiled as C++ verbatim are the syntax proof;
GPU compile is on hardware.

## 3. Hardware acceptance (yours, GTX 1060)

1. Cornell matches the pinned reference (converged; per-frame noise differs by one RNG draw — expected).
2. `--scene shaderball`: roughness ramp reads 0→1 left-to-right, 5 metals + gold-film tint at grazing, coat/fuzz/film/haze rows distinct, EON r=0 vs r=1 pair, emitter + luminaire bloom, alpha card fully cut out; camera frames all four rows at 55° FoV.
3. Log lines: `Materials: …` (Cornell: 6→6, 0 folded; ShaderBall: 26→26 + fallback), `Textures: …`, `[SwapchainExchange] Textures: … in the bindless table`, `[Scene] Exported the shader-ball level …` on first run.
4. Sponza: normal-mapped shading, textured metal/rough, foliage cut-outs in view **and** shadows; F3 Roughness / Metalness / ShadingNormal views; kernel ms in the F3 popup — budget ≤ +25% over R4a at 1080p (Simple/Single materials dominate; gated lobes cost ~Lambert + GGX on plain walls).
5. Foliage shadow-hit counts with/without the mask and the Sponza texture-LOD eyeball (no Sponza in the sandbox — numbers from your run go in the R5 plan's baseline table).

## 4. Deviations / notes

* ⚠️ Plan said Turquin `1+F0(1−E)/E` compensation with an R16F single-channel LUT; the furnace showed it under-closes at r = 0.7 (0.76). Swapped to full reciprocal Kulla–Conty — the LUT became 3-channel (A, B, E_avg), same 32×32 footprint.
* ⚠️ Bindless texture binding moved 11→15 (Vulkan requires the variable-count binding last). Vertex/index took 11/12, LUTs 13/14.
* ⚠️ Shadow-ray alpha path is a closest-hit walk with a 4-layer limit (the CWBVH traversal has no any-hit callback), not the plan's "any-hit with alpha test". `AlphaMaskedMaterialCount == 0` keeps the fast path.
* ⚠️ Bounce cone has no curvature term (flat-surface spread only); bounce-hit instance lookup is a linear scan over `Instances[]` (≤ 105 on Sponza, once per bounce) — R5's rayQuery path returns the instance id directly; kernel budget in §3 will tell us if either matters.
* ⚠️ Normals use `mat3(World)` directly — correct for rigid/uniform scale; no inverse-transpose (no non-uniform instances in Cornell/Sponza/shaderball).
* ⚠️ Anisotropic compensation uses the `√(αx·αy)` lookup: exact to 3.5% at anisotropy 0.5, 15% loss at 0.8 (tolerances reflect this; a 3D LUT would fix it — R8 polish).
* Thin film on metal uses the F82 curve as base reflectance (no complex-IOR inversion); `specular_color` tints dielectric Fresnel directly; no MIS uniform lobe for EON (the paper's recommendation) — the sampling proof still passes.
* EON fact recorded: for ρ < 1 and r > 0 the albedo is below ρ by design (multiple-scatter absorption, `ρ_ms ~ ρ²`), so the plan's "= base_color ±0.5%" criterion was reworded to "= closed-form E_EON".
* Plan's `DiagnosticInspector` complexity-histogram + LOD-stats popup did **not** land (no UI in this phase after all) — carried to R5 if still wanted.
* Plan proof §4.6 (Sponza foliage shadow-hit counts) and the kernel-budget number need the 1060 (§3.4–5) — no Sponza / GPU here.
