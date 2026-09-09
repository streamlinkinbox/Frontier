# 🧩 Material System Research — Realtime Layered Materials, 2023 → 2026

Research report requested after R2 (2026-09-04). Scope: what a **complex, Unreal-class material model** looks like today,
which parts have realtime-grade implementations (GTX 1060 Tier A → RTX Tier B), and which channel set Slate should adopt.
No code in this document — it feeds the phase plans (R2b Materials, and the interchange layer).

---

## 1. Executive summary

1. **The industry converged on a single über-shader vocabulary in 2023–2026: OpenPBR Surface** (Academy Software
   Foundation, v1.0 2024, **v1.1.1 2026-04-17**), a merger of Autodesk Standard Surface and Adobe Standard Material.
   It defines **49 parameters in 9 groups** (base, specular, transmission, subsurface, coat, fuzz, emission, thin-film,
   geometry). MaterialX ships a reference implementation; glTF is adopting it piecemeal through `KHR_materials_*`
   extensions. This — not Unreal's node list — is the correct *authoring/interchange* contract for Slate.
2. **Unreal 5.3–5.8 "Substrate"** is the realtime realisation of the same idea: a *Slab* (interface + medium) with
   ~20 inputs, composed with vertical-layer / horizontal-mix / weight operators, packed into an adaptive G-buffer, and
   lit through **complexity tiers** (Simple → Single → Complex → Complex Special) so that cost scales with what a pixel
   actually uses. Substrate is the *runtime architecture* to imitate; OpenPBR is the *parameter contract*.
3. **The research that matters for realtime since 2023** (all with GLSL or LTC-ready formulations):
   EON energy-preserving rough diffuse (JCGT 2025, in OpenPBR), Zeltner LTC multiple-scattering sheen (2022, in
   OpenPBR fuzz), Dupuy–Benyoub spherical-cap VNDF sampling (HPG 2023), Deliot–Belcour real-time glints (HPG 2023,
   what Substrate's glint channel uses), Belcour–Barla thin-film iridescence (2017, in Substrate + glTF), F82-tint
   metal Fresnel (Kutz 2021, in OpenPBR/Adobe), Kulla–Conty multiple-scattering compensation (2017, now standard).
4. **Recommendation:** one `MaterialStructure` record that *is* OpenPBR (all 49 parameters, OpenPBR identifiers,
   ACEScg colours, metres), plus a small **Slate-specific runtime block** (complexity class, texture slots, flags).
   Layering follows Substrate: a material is a small graph of ≤4 slabs with vertical/horizontal operators, *flattened
   at import* to a single parameter-blended slab for Tier A and kept as up-to-N slabs for Tier B. That gives you the
   "~20 channels incl. Fuzz, IOR, refraction, clearcoat, EON, specular, Fresnel, anisotropy" you asked for, with an
   exact interchange story (glTF / MaterialX / USD) and no invented parameters.

---

## 2. The reference models, side by side

### 2.1 OpenPBR Surface v1.1.1 — the parameter contract (49 params)

Source: academysoftwarefoundation.github.io/OpenPBR (spec dated 2026-04-17), §5 Parameter reference.

| Group | Parameters (identifier · type · default) | Physical meaning |
|---|---|---|
| **Base** | `base_weight` f 1 · `base_color` c (0.8) · `base_metalness` f 0 · `base_diffuse_roughness` f 0 | Metal ↔ glossy-diffuse mix; diffuse roughness drives **EON** |
| **Specular** | `specular_weight` f 1 · `specular_color` c (1) · `specular_roughness` f 0.3 · `specular_roughness_anisotropy` f 0 · `specular_ior` f 1.5 | GGX dielectric interface; for metals `specular_color` is the **F82-tint** edge tint |
| **Transmission** | `transmission_weight` f 0 · `transmission_color` c (1) · `transmission_depth` f 0 [m] · `transmission_scatter` c (0) · `transmission_scatter_anisotropy` f 0 · `transmission_dispersion_scale` f 0 · `transmission_dispersion_abbe_number` f 20 | Refraction (rough GGX BTDF), Beer–Lambert absorption, volume scatter, Abbe dispersion |
| **Subsurface** | `subsurface_weight` f 0 · `subsurface_color` c (0.8) · `subsurface_radius` f 1 [m] · `subsurface_radius_scale` c (1, .5, .25) · `subsurface_scatter_anisotropy` f 0 | Diffusion / random-walk SSS (radius per channel = MFP) |
| **Coat** | `coat_weight` f 0 · `coat_color` c (1) · `coat_roughness` f 0 · `coat_roughness_anisotropy` f 0 · `coat_ior` f 1.6 · `coat_darkening` f 1 | Dielectric clear-coat with absorption, base darkening, roughening and TIR |
| **Fuzz** | `fuzz_weight` f 0 · `fuzz_color` c (1) · `fuzz_roughness` f 0.5 | Zeltner LTC sheen (cloth, velvet, dust) |
| **Emission** | `emission_luminance` f 0 [nit] · `emission_color` c (1) | Photometric emission |
| **Thin-film** | `thin_film_weight` f 0 · `thin_film_thickness` f 0.5 [µm] · `thin_film_ior` f 1.4 | Belcour–Barla iridescence |
| **Geometry** | `geometry_opacity` f 1 · `geometry_thin_walled` b · `geometry_normal` v · `geometry_tangent` v · `geometry_coat_normal` v · `geometry_coat_tangent` v | Coverage, thin-sheet mode, two normal/tangent frames |

Structure: **slabs** (fuzz / coat / thin-film / dielectric-or-metal / diffuse / subsurface / translucent) composed by
`layer()` (vertical) and `mix()` (horizontal). §3.10 gives the closed-form reduction to a weighted lobe mixture —
exactly what a realtime shader evaluates. §3.11 lists the **white-furnace configurations** the model must pass; we
adopt them as unit tests.

### 2.2 Unreal Engine 5 Substrate (5.3 experimental → 5.7/5.8 with Adaptive G-buffer) — the runtime architecture

Slab BSDF inputs (Epic docs): Diffuse Albedo · F0 · F90 · Roughness · Anisotropy · Normal · Tangent · SSS MFP ·
SSS MFP Scale · SSS Phase Anisotropy · Emissive Color · Second Roughness · Second Roughness Weight · Fuzz Roughness ·
Fuzz Amount · Fuzz Color · Glint Density · Glint UVs · Specular Profile · Thin-film (via helper → F0/F90) · Coverage.
Dedicated BSDFs: Eye, Hair, Simple Clear Coat, Single-Layer Water, Unlit, Volumetric. Operators: Vertical Layer,
Horizontal Mix, Weight, Add, Coverage Weight, Select. Helpers: IOR→F0, Metalness→DiffuseAlbedo/F0,
Haziness→SecondRoughness, Transmittance→MFP, Rotation→Tangent, Thin-Film, View-Dependent Coverage.

Key design facts to copy:
* **F0/F90 + DiffuseAlbedo parameterisation** instead of BaseColor/Metallic/Specular (metalness is a helper node).
  Energy-preserving by construction; F90 gives coloured grazing tint.
* **Complexity classes** drive the lighting cost: *Simple* (diffuse + spec + rough) → *Single* (+F90, fuzz, SSS, coat)
  → *Complex* (+anisotropy, specular profile, eye/hair) → *Complex Special* (glints). Adaptive G-buffer packs per
  pixel only what the class needs (Simple = 4 bytes header/AO + R7G7B6 dithered colours; Single/Complex grow).
* Second roughness = **haziness** (Barla et al. 2018 two-lobe "hazy gloss").
* Glints = Deliot–Belcour 2023; Specular Profile = measured LUT (view/light angle) for iridescent-like measured data.
* Perf reality (community profiling, 2026): single-slab ≈ legacy DefaultLit (+2–3 %); 4 slabs + full SSS ≈ 2.8×;
  SSS alone +40–80 %. → hero/standard/background slab budgets.

### 2.3 glTF 2.0 — the interchange surface (what SceneCodec will actually read)

Ratified `KHR_materials_*` (2025): anisotropy, clearcoat, dispersion, emissive_strength, ior, iridescence, sheen,
specular, transmission, volume, unlit, variants. In flight: **diffuse_transmission** (RC), **subsurface** (draft),
`volume_scatter`, and the OpenPBR-alignment set now appearing in loaders — **`KHR_materials_fuzz`, `KHR_materials_coat`,
`KHR_materials_diffuse_roughness`** (Babylon.js already lists them). Vendor: `ADOBE_materials_clearcoat_specular /
_tint`. Mapping glTF → OpenPBR is 1:1 for every ratified extension (table in §5).

---

## 3. Research 2023–2026 that changes what "realistic realtime" means

| Year | Work | What it gives Slate | Tier A cost |
|---|---|---|---|
| 2023 | Dupuy & Benyoub, *Sampling Visible GGX Normals with Spherical Caps* (HPG) | Cheapest exact VNDF sampler; use in ReSTIR candidate generation for every GGX lobe | ~free |
| 2023 | Deliot & Belcour, *Real-Time Glints via Distributed Binomial Laws on Anisotropic Grids* (HPG/CGF 42-8) | Car-paint flakes, snow, sand; correct statistics under any footprint; 1.5–5× faster than prior | Complex-Special only |
| 2023 | d'Eon, Bitterli, Weidlich, Zeltner, *Microfacet Theory for Non-Uniform Heightfields* (SIGGRAPH) | Sound basis for mixing NDFs (haziness / second roughness) | n/a (theory) |
| 2024 | d'Eon & Weidlich, *VMF Diffuse: A unified rough diffuse BRDF* (CGF 43) | Alternative to EON with a physically-derived roughness; more expensive | reference only |
| 2024→25 | Portsmouth, Kutz, Hill, **EON** (arXiv 2410.18026, JCGT 14-1 2025; v3 Dec 2025) | Energy-preserving Oren–Nayar, reciprocal, no dark ring, **self-contained GLSL** + CLTC importance sampling (7× faster, >100× lower variance at grazing). Adopted by OpenPBR for `base_diffuse_roughness` | ≈ Lambert + a few FMA |
| 2022 (used 2024+) | Zeltner, Burley, Chiang, *Practical Multiple-Scattering Sheen using LTC* (SIGGRAPH Talks) | OpenPBR fuzz; LTC table → cheap, energy-conserving, importance-sampleable | 1 LUT fetch |
| 2021 (in OpenPBR) | Kutz, Hašan, Edmondson, *F82-tint* metallic Fresnel | Artist-friendly coloured metals without complex IOR tables | free |
| 2017 (still SOTA) | Kulla & Conty multiple-scattering compensation; Turquin 2019 practical fit | White-furnace-passing GGX at high roughness | 1 LUT fetch |
| 2017 (in glTF+UE) | Belcour & Barla thin-film iridescence | Soap bubbles, oil, coated optics | 1 small fit |
| 2024 | Cocco, Zanni, Chermain, *Anisotropic Specular IBL via BRDF Major-Axis Sampling* (EG) | Correct anisotropic environment/spec lookups | cheap |
| 2018 | Barla, Pacanowski, Vangorp, *Hazy gloss* | Second-roughness / haziness parameterisation used by Substrate | 2nd GGX eval |
| 2025 | Liu et al., *Reservoir Splatting* (SIGGRAPH) | Not material — but keeps ReSTIR reuse valid under all these lobes | R7 |

Conclusion: every lobe in OpenPBR now has a **published, energy-conserving, importance-sampleable realtime form**.
Nothing here requires hardware RT; all are BSDF-side and run identically in Tier A compute and Tier B rayQuery.

---

## 4. Recommended Slate material model

### 4.1 Channel set (authoring record = OpenPBR, verbatim)

Adopt all 49 OpenPBR parameters with their identifiers as the on-disk / in-memory authoring record
(`MaterialDescriptor`). Units: metres, nits, linear Rec.709 (ACEScg converted at the codec boundary — see §7.4).
This satisfies your list and more:

* Fuzz → `fuzz_*` (Zeltner LTC) · IOR → `specular_ior`, `coat_ior`, `thin_film_ior` · refraction/reflection →
  `transmission_*` + `specular_*` · clear coat → `coat_*` (with darkening, roughening, TIR) · EON →
  `base_diffuse_roughness` · specular colour/Fresnel → `specular_color` (F82-tint for metals, F0 scale for
  dielectrics) · anisotropy → `specular_roughness_anisotropy`, `coat_roughness_anisotropy` + tangent frames ·
  SSS → `subsurface_*` · dispersion → Abbe number · iridescence → `thin_film_*` · emission · opacity · thin-walled.

### 4.2 Slate-specific runtime block (not in OpenPBR; needed for the engine)

| Field | Purpose |
|---|---|
| `ComplexityClass` (Simple / Single / Complex / ComplexSpecial) | Derived at import from which weights ≠ 0 → selects the shading permutation, Substrate-style |
| `HazinessWeight`, `HazinessRoughness` | Second specular lobe (Substrate "Second Roughness"); OpenPBR has no haziness — **flagged as an extension**, stored under `slate_*` |
| `GlintDensity`, `GlintUVScale` | Deliot–Belcour flakes — **extension**, `slate_*` |
| `TextureSlots[]` | Bindless indices per channel (R2b), UV set, transform, channel swizzle |
| `Flags` | double-sided, alpha mode/cutoff, cast-shadow, receive-decals, unlit |
| `LuminaireCandidate` | emission_luminance > 0 → feeds the luminaire list (already in SceneStructure) |

### 4.3 Layering = Substrate operators, evaluated as OpenPBR lobes

* Authoring graph: ≤ 4 slabs joined by `VerticalLayer(top, bottom)`, `HorizontalMix(a, b, t)`, `Weight(slab, w)`,
  `CoverageWeight`. Each slab is a full OpenPBR record.
* Import flattens to **one parameter-blended slab** (Substrate's "parameter blending" path) for Tier A and keeps up
  to 3 lobes-sets for Tier B. Flattening rules follow OpenPBR §3.10 (albedo-scaling layering).
* Lighting evaluates the §3.10 lobe mixture: fuzz → coat → thin-film-modified dielectric/metal GGX (×2 with haziness)
  → EON diffuse | subsurface | translucent. Multiple-scattering compensation on all GGX lobes.

### 4.4 GPU record and cost budget

Target ≤ **128 B per material** (current `MaterialRecord` is 32 B): colours as RGB16F, scalars as UNORM8/16 where
OpenPBR gives a [0,1] range, IORs as half. With ~2 000 materials in Sponza-class scenes that is 256 KB — trivially
resident. Per-pixel shading permutations by `ComplexityClass` keep Simple pixels at today's cost.

### 4.5 Validation (from OpenPBR §3.11)

White-furnace tests for: white metal; white dielectric diffuse; SSS; translucent (depth 0 and > 0); each with coat;
each with fuzz; invariant under thin-film, roughness/anisotropy, volume anisotropy, normal maps, opacity. Also the
**Standard Shader Ball** (Mazzone & Rydalch 2023, USD-WG) as the visual proof scene once bindless textures land.

---

## 5. Interchange mapping (for the importer layer)

| glTF / KHR | OpenPBR field | Note |
|---|---|---|
| `pbrMetallicRoughness.baseColorFactor/Texture` | `base_color` | sRGB → ACEScg |
| `metallicFactor` | `base_metalness` | |
| `roughnessFactor` | `specular_roughness` | |
| `KHR_materials_specular` specularFactor / specularColor | `specular_weight`, `specular_color` | |
| `KHR_materials_ior` | `specular_ior` | default 1.5 both sides |
| `KHR_materials_anisotropy` strength / rotation | `specular_roughness_anisotropy`, `geometry_tangent` | |
| `KHR_materials_clearcoat` (+ADOBE ior/tint, future `KHR_materials_coat`) | `coat_weight/roughness/ior/color` | coat normal → `geometry_coat_normal` |
| `KHR_materials_sheen` (future `KHR_materials_fuzz`) | `fuzz_color/roughness` (weight = luminance) | |
| `KHR_materials_transmission` + `KHR_materials_volume` | `transmission_weight/color/depth` | attenuation → depth+color |
| `KHR_materials_dispersion` | `transmission_dispersion_abbe_number` | |
| `KHR_materials_iridescence` | `thin_film_weight/ior/thickness` | thickness nm → µm |
| `KHR_materials_diffuse_transmission` / `_subsurface` (draft) | thin-walled translucency / `subsurface_*` | |
| `KHR_materials_diffuse_roughness` (draft) | `base_diffuse_roughness` | EON |
| `KHR_materials_emissive_strength` | `emission_luminance` | |
| `alphaMode/alphaCutoff/doubleSided` | `geometry_opacity` + runtime flags | |

FBX/OBJ have no physically-defined superset; they map to the *Simple* class (base colour, spec-gloss → roughness,
Ks → specular_color, Ni → ior, d → opacity). MaterialX `open_pbr_surface` nodes import losslessly. USD
`UsdPreviewSurface` ≈ glTF core.

---

## 6. Interchange layer — `ContentInterchange` (replaces the old "AssetInterchange"; no "Asset")

`Engine/ContentInterchange/` with per-format codecs `SceneCodec` (glTF/GLB — exists, moves here), `FbxCodec`
(ufbx, already a submodule), `ObjCodec` (fast_obj, submodule), later `UsdCodec` (tinyusdz) and `MaterialXCodec`.
All produce one `SceneStructure` + `MaterialDescriptor[]` + the scene-graph rows:

* `PlacementRecord { Name, Ancestor, FirstDescendant, NextPeer, LocalTransform, WorldTransform, Instance, Camera,
  Luminaire }` — flat arrays, stable IDs, no UI (the outliner comes later and reads this directly).
* `SceneStructure` keeps the flat GPU-facing arrays; placements reference instances by index so the culling/raster
  path is unchanged.

---

## 7. Decisions (taken 2026-09-04)

1. **Naming** — module **`ContentInterchange`** (user). Material naming starts with *Material* (user): **`MaterialDescriptor`** = one material (slab list +
   operations, OpenPBR identifiers), **`MaterialIndex`** = the resident table of descriptors addressed by material id
   (CLAUDE.md role 15, direct addressing — this is what the GPU record layout lives in), **`MaterialCodec`** = glTF /
   MaterialX ↔ descriptor (role 2). The R2 `RadianceStructure` GPU record stays until R4 replaces it. (Surface*/
   MaterialStructure rejected by user; `Substrate` is a banned word so Unreal's term is never used in code). Per-slab GPU record `MaterialSlabRecord`; the ≤4-slab
   scene-graph rows `PlacementRecord` (Parent/Child/Sibling/Node/Hierarchy are
   banned → fields `Ancestor`, `FirstDescendant`, `NextPeer`).
2. **Beyond-OpenPBR extras** — include **haziness (second roughness)** and **glints** from the start, stored under a
   `slate_` prefix in the record and serialised as glTF `extras.slate_*`; **specular-profile LUT deferred** (needs
   measured data we do not have). Rationale: haziness and glints are cheap, fully specified (Barla 2018,
   Deliot–Belcour 2023) and are what separates "PBR" from "car paint / brushed metal / snow" visually; a LUT channel
   without content is dead weight.
3. **Slab count — variable, capped per tier, never baked into the format.** See §7.1.
4. **Colour space — linear Rec.709/sRGB primaries internally**, ACEScg only at import/export boundaries.
   Rationale: every texture we will ever sample (glTF, FBX, PNG/KTX) is sRGB-primaries; converting all of them to
   ACEScg on upload costs a 3×3 per texel and buys nothing until we have wide-gamut output. OpenPBR permits any
   working space as long as it is declared; we declare `lin_rec709` in metadata. Switching later is one matrix at
   the codec boundary, not a shader change.
5. **Phase placement** — R3 CWBVH stays next (the kernel is still O(N); nothing material-side is visible until
   shading is fast enough to show it). Then **R4 = ContentInterchange** (MaterialDescriptor + layering,
   bindless textures, PlacementRecord scene graph, FBX/OBJ codecs) as one phase, because materials, textures
   and the importer are one data contract and splitting them would ship a half-usable importer twice. Previous
   R4 (rayQuery AS) shifts to R5, ReSTIR DI/GI to R6/R7, H-PLOC/LOD/reservoir work to R8.

### 7.1 Slab design — flexible by construction (texture painting + complex car paint are the driving cases)

A *slab* is one complete surface layer (own normal, roughness, Fresnel, coat, fuzz, glints). Real objects are
stacks: dust **on** lacquer **on** paint. The user plans a **layered texture-painting system** and **complex car
paint**, so the model must not hard-wire a slab count anywhere. Design:

* **Record = slab list, not fixed fields.** `MaterialDescriptor` holds `Slabs[]` (each a full OpenPBR parameter set +
  `slate_` haziness/glints) and `Operations[]` (`VerticalLayer`, `HorizontalMix(mask)`, `Weight`, `Coverage`) as a
  small post-order expression. Serialised as-is (glTF `extras`, later MaterialX). Nothing in the file format caps N.
* **Runtime cap is a configuration, per tier**: `[render] slab_limit` — default **1 on Tier A** (everything
  flattened, OpenPBR §3.10 albedo-scaling rules), **4 on Tier B/C**, hard ceiling 8 in the GPU record layout. The
  importer reduces any graph to ≤ limit *losslessly where possible* (horizontal mixes fold into their slab;
  verticals beyond the limit fold by albedo scaling) and reports what it folded. Raising the cap later is a config
  change plus shader permutation, not a data migration.
* **Texture painting** = authoring-time *horizontal* layers per texel (paint, dirt, wear masks). They flatten into
  the slab they touch at bake time — cost-free at runtime regardless of how many paint layers exist. Only when a
  paint layer changes *physical structure* (a decal of clear resin, a metallic flake pass) does it become a vertical
  slab.
* **Car paint** = 3 verticals + glints: body metallic (F82-tint) → flake slab (`slate_glint_density`, Deliot–Belcour)
  → clear coat (coat IOR/roughness/darkening, optional thin-film for pearlescent). Fits Tier B default 4 with room
  for a top film (dust/rain). On Tier A it flattens to one slab with glints kept as a lobe modifier, so the look
  degrades gracefully instead of disappearing.
* **Per-pixel cost stays "pay for what you use"**: the shading permutation is chosen by the *resolved* slab count
  and complexity class of the material, so a plain wall pixel never pays for the car's four slabs.

Why not fix 3 (my earlier proposal): correct for today's hero cases but it would have made the painting pipeline
and future materials fight the format. The cap-as-config keeps the same performance guarantee with none of the
stiffness.

## 8. Sources

* OpenPBR Surface specification v1.1.1 (2026-04-17) — https://academysoftwarefoundation.github.io/OpenPBR/
* Epic Games, *Substrate Materials Overview* (UE 5.3 → 5.8) — dev.epicgames.com/documentation/…/overview-of-substrate-materials-in-unreal-engine
* Epic Games Japan, *GCC2026 — Substrate internals & performance* (2026-04) — docswell.com/s/EpicGamesJapan/K8N2G7
* Portsmouth, Kutz, Hill, *EON: A practical energy-preserving rough diffuse BRDF*, JCGT 14(1) 2025 — arXiv 2410.18026
* d'Eon & Weidlich, *VMF Diffuse*, CGF 43 (2024)
* Dupuy & Benyoub, *Sampling Visible GGX Normals with Spherical Caps*, HPG 2023
* Deliot & Belcour, *Real-Time Rendering of Glinty Appearances using Distributed Binomial Laws on Anisotropic Grids*, HPG 2023 / CGF 42(8)
* Zeltner, Burley, Chiang, *Practical Multiple-Scattering Sheen Using LTC*, SIGGRAPH Talks 2022
* Kutz, Hašan, Edmondson, *Novel aspects of the Adobe Standard Material* (2021)
* Belcour & Barla, *A Practical Extension to Microfacet Theory for the Modeling of Varying Iridescence*, TOG 2017
* Kulla & Conty, *Revisiting PBS at Imageworks*, SIGGRAPH 2017; Turquin, ILM 2019
* Cocco, Zanni, Chermain, *Anisotropic Specular IBL*, Eurographics 2024
* Khronos glTF extension registry (ratified + in-progress list, 2025) — github.com/KhronosGroup/glTF/tree/main/extensions
* Babylon.js `GLTFLoaderExtensionOptions` (lists `KHR_materials_fuzz/coat/diffuse_roughness`, 2025)
* Mazzone & Rydalch, *Standard Shader Ball*, SIGGRAPH Asia 2023
