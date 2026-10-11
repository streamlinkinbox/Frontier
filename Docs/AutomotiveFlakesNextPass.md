# Automotive metallic flakes — reference and implementation audit

Status: initial research/audit for the resumed automotive task. **Superseded by the implemented standalone shader and exhibit described in `Docs/AutomotiveFlakePaint.md`.** The findings below record the pre-implementation baseline. Environment/Camera inspector work is not changed by this task. Water/Fluids remain deferred.

## User reference

https://www.shadertoy.com/view/mdSyWd — *Iridescent Car Paint*, piyushslayer, 2024-06-24.

The page describes an approximation of goniochromatic Fresnel reflectance and explicitly points to Buffer B and Belcour/Barla 2017. The retrieved page exposed the description and part of the Image/postprocessing pass, **not the full Buffer B implementation**. Do not describe this audit as an exact port of that shader. Source reuse/licensing has not been established.

Separate three effects rather than treating all of them as coloured noise:

1. Pigmented base paint.
2. Discrete reflective metallic flakes, producing light- and view-dependent glints.
3. A smooth dielectric clearcoat above the flakes.

Pearlescent/iridescent flake Fresnel is an optional fourth effect. Conventional metallic paint must work without a rainbow colour shift. No emissive sparkle dots or time-randomized pixel noise.

## Existing code found in the pinned reconstruction

- `Engine/Shaders/AutomotiveMaterialProfiles.slang`
  - `AutomotiveFlakeSignal`
  - `AutomotiveApplyTriCoatFlakeNormal`
  - `AutomotiveApplyTriCoatFlakes`
  - `AutomotiveFlopColor`
- `Engine/Shaders/ReSTIRViewport.slang`, material resolution: consumes `P14.xy` / `SlateGlintDensity`, `SlateGlintUvScale` through the normal-perturbation helper.
- `Engine/ContentInterchange/ShaderballPreview.cpp`: automotive CPU-preview hook uses the shared Slang helpers.
- `Engine/ContentInterchange/MaterialDescriptor.h`, `MaterialIndex.*`, `MaterialCodec.cpp`: existing stored density/scale fields and serialization routes. Reuse/audit these before adding duplicate parameters.
- `Exhibits/Workbench/Automotive/AutomotiveMaterialPreview.cpp`: existing native CPU material exhibit.
- `Exhibits/Workbench/Materials/RunGlintSheet.sh`: existing glints-on/off image/energy regression.
- `References/AutomotiveMaterials-Plan.md`: prior standalone-before-host review sequence.

These paths currently live under `.cache/cpp-sun-full`, reconstructed from the immutable target. Persist future changes using scoped source/patch delivery; do not edit the immutable target baseline or reconstruct over the completed editor integration unintentionally.

## Defects/limits in the current approximation

- Three sine bands are not a finite, footprint-filtered flake population.
- The independent `h2` term can produce a nonzero signal even when density is zero. The runtime outer `P14.x > 0` branch masks this in one route, but the helper itself lacks the zero-density invariant.
- Coordinates supplied by the GPU path are `hitPos`, not an explicitly stable object/material coordinate system. Movement/scale invariance needs testing.
- The helper's extra grazing multiplier compares the normal to world +Z, not the view/light half-vector.
- The GPU path perturbs the shared base frame **before** deriving the coat frame; the existing comment explicitly says the coat follows the flakes. This is not a smooth clearcoat above embedded facets.
- The preview separately changes base tint and coat roughness from the flake signal, while the inspected GPU hook is a normal-only path. Shared helper source does not imply full CPU/GPU material equivalence.
- No pixel-footprint filtering is present in these helpers. Fine-detail shimmer and distant aliasing cannot be addressed by changing the noise pattern alone.
- Existing labels mention Deliot–Belcour, but the audited helper does not implement the distributed-binomial algorithm. Do not relabel this approximation as a paper implementation.

## Online implementation references

- SideFX Car Paint Shader — separation of base, flakes and coat; flake frequency, size, normal spread, reflectivity and roughness:
  https://www.sidefx.com/docs/houdini/nodes/vop/carpaintshader.html
- Belcour and Barla, SIGGRAPH 2017 — thin-film iridescence integrated into microfacet theory; paper, supplemental code and slides:
  https://belcour.github.io/blog/research/publication/2017/05/01/brdf-thin-film.html
- Deliot and Belcour, 2023 — *Real-Time Rendering of Glinty Appearances using Distributed Binomial Laws on Anisotropic Grids*.
- Kneiphof and Klein, 2024/2025 — area-light and image-based-lighting extensions; source repository:
  https://github.com/tomix1024/IBLGlints-Demo
  Repository revision visible during audit: `c477fc7ea48d0a9969f9240410238c38e3d1558e`.
  Relevant entry points documented by the authors: `EvaluateBSDF`, `EvaluateBSDF_Rect`, `EvaluateBSDF_Env`, and `Runtime/Material/Glints/Glints*.hlsl`.
- 2025 IBL paper:
  https://arxiv.org/html/2507.02674v1

The IBL repository contains modified Unity HDRP code and previous research implementations. Inspect per-file licensing and attribution before importing code; a public repository is not sufficient evidence of an unrestricted license. No external shader code was copied in this audit.

## Next implementation/review gate

1. Preserve an explicit zero-flakes baseline; replace the approximation with a spatially stable finite-flake model and a defined footprint/LOD treatment. Decide explicit-facet close-up versus statistical unresolved population without hiding the distinction.
2. Evaluate flake reflections using view/light directions; preserve a separate smooth coat frame and account for coat transmission rather than adding unattenuated lobes.
3. Expose independently meaningful coverage/density, flake scale, orientation spread and roughness. Keep optional iridescence strength/thickness separate from metallic flake controls; check existing thin-film units before binding them.
4. Share the evaluation path across native CPU reference and shader implementation. Audit evaluation, sampling/PDF and transport together before claiming production integration.
5. Produce a focused automotive exhibit: smooth paint / metallic flakes / optional pearl, plus close/far views and moving-light/camera sequences. Use a curved body-panel sample as well as a sphere; static glitter images alone do not prove glints behave correctly.
6. Validate zero density, finite/nonnegative output, clearcoat independence, object-locked identity, density/scale endpoints, grazing angles, camera-distance filtering, direct/area/environment response and temporal stability. Re-run existing automotive and glint regression gates. GPU/Windows execution must be reported separately from CPU reference rendering.

This work should resume the material shader, not start another inspector design or replace the existing native editor with a web-only effect.
