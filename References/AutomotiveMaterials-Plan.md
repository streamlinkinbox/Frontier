# Automotive materials — standalone preview plan

## Goal

Build and review the automotive material suite in a separate scene before touching Project-Zero's scene construction,
material serialization, or runtime UI. The preview is the acceptance gate: the material profile source and the CPU
reference render must use the same Slang text that the GPU material evaluator includes.

## Current milestone — standalone profile and render gate

Implemented in this branch:

- `Engine/Shaders/AutomotiveMaterialProfiles.slang`
  - shared constructors for the automotive profiles
  - included by `Engine/Shaders/MaterialEvaluation.slang`, so CPU and GPU see the same source
  - no duplicate CPU-only BSDF implementation
- `Exhibits/Workbench/Automotive/AutomotiveMaterialPreview.cpp`
  - procedural UV-parameterized sphere, torus, and cylinder geometry
  - standalone UV-detail review scene for carbon dual-weave and tire tread
  - separate rack scene, not Project-Zero
  - renders through the exact `MaterialEvaluation.slang` OpenPBR evaluator compiled as C++
- `Exhibits/Workbench/Automotive/RunAutomotiveMaterialPreview.sh`
  - reproducible build/render driver
- `Exhibits/Gallery/Automotive/`
  - review images and the scene README

### Profiles in the preview

| Profile | Shared evaluator features exercised |
|---|---|
| Tri-coat paint | metallic base, GGX, high-IOR clearcoat, coat roughness and layer darkening |
| Carbon/resin | dark metallic base, orthogonal anisotropy, clear resin coat |
| Clear headlight glass | solid transmission, IOR, Fresnel, Beer volume and medium traversal |
| Red tail-lens glass | red Beer-Lambert absorption, solid transmission and Fresnel |
| Brushed alloy / brake rotor | metallic F82 Fresnel, anisotropy and roughness |
| Heat-tinted titanium | metallic GGX plus the shared thin-film Airy model |
| Tire rubber | high roughness, low dielectric specular, high diffuse roughness |
| Alcantara | shared cloth/fuzz sheen lobe |

## Render outputs

- `Exhibits/Gallery/Automotive/AutomotiveMaterialSuite.png`
- `Exhibits/Gallery/Automotive/AutomotiveOpticsAndCoatings.png`
- `Exhibits/Gallery/Automotive/AutomotiveUvSurfaceDetail.png`
- `Exhibits/Gallery/Automotive/AutomotiveDispersionAndTir.png`
- `Exhibits/Gallery/Automotive/AutomotiveLightingOptics.png`

These are reference images, not GPU captures. They are rendered from the shipped evaluator compiled as C++ so the
material math is identical to the Slang evaluator. GPU lowering/runtime validation remains a separate machine gate.

## Next implementation stages

### A. Procedural flakes and flop — preview complete

Implemented in the shared profile source and standalone preview:

1. `AutomotiveFlakeSignal` uses three deterministic incommensurate analytic bands for dense, non-grid flakes.
2. `AutomotiveApplyTriCoatFlakes` modulates the shared base/coat record and micro-roughness; it does not add a fake
   light source or bypass the OpenPBR evaluator.
3. `AutomotiveApplyTriCoatFlakeNormal` turns the same signal into a bounded metallic-facet normal perturbation, so
   direct-light reflections break up into readable silver/blue sparkle rather than painted dots.
4. The standalone preview uses a deterministic randomized bank of invisible area lights through the existing NEE/MIS
   path; the lights are not emissive decals and are omitted from the camera-visible BVH.
5. `AutomotiveFlopColor` shifts face tint toward a grazing tint with a canonical fifth-power Fresnel-style curve.
6. The CPU preview hook is compile-time isolated to the standalone automotive scene; the profile functions remain in the
   common Slang include for the eventual GPU material-record integration.
7. `AutomotivePaintFlakeFlopComparison.png` renders face-on versus grazing views.

The GPU host does not serialize the new automotive parameters yet. That is intentional and remains part of the later
Project-Zero integration gate.

### B. UV-backed surface detail — preview complete

Implemented in the shared profile source and standalone preview:

1. `Tri` carries per-vertex UVs, the OBJ loader preserves `vt` coordinates, and procedural sphere/torus/cylinder meshes
   emit UVs that are barycentrically interpolated at the hit.
2. `AutomotiveCarbonWeave` and `AutomotiveTireTread` are deterministic UV test fields shared by the CPU and GPU source;
   they modulate the base/roughness lobes and feed a bounded UV-frame normal detail pass.
3. `AutomotiveUvSurfaceDetail.png` provides a close standalone review of the carbon dual-weave and wrapped tire tread.
4. These analytic fields are the texture-binding-independent review fallback. Authored texture assets remain outside
   Project-Zero until the preview acceptance sheet is approved.

The next phase is host integration, but it remains gated on review of all standalone sheets.

### C. Lighting optics — preview complete

Implemented in the shared profile source and standalone preview:

1. `AutomotiveCauchyIor` evaluates the Cauchy A/B/C curve around the 589.3 nm reference line. The CPU preview traces
   red, green, and blue channels with wavelength-specific IORs through the existing Fresnel, Snell, Beer, and TIR path.
2. `AutomotiveDispersionAndTir.png` compares solid dielectric samples at nD 1.33, 1.52, and 1.72. The runtime gate
   checks blue > green > red Cauchy indices plus both a TIR and a refracting case.
3. `AddFacetedReflector` creates a real shallow faceted bowl; `AutomotiveLightingOptics.png` pairs it with red and
   amber solid lenses using authored attenuation distances of 0.32 m and 0.45 m.
4. No reflector or lens effect is emissive geometry; direct-light reflections and solid-medium traversal remain the
   transport mechanism.

### D. Host integration — intentionally not started

Only after the standalone images are accepted:

1. add material serialization fields and decode/build parity checks;
2. map profiles into Project-Zero's material records;
3. integrate car meshes and UV assets in a separate Project-Zero automotive scene;
4. add UI controls and GPU SPIR-V captures;
5. preserve existing ReSTIR, GI, reflection controls and the deferred sky reservoir.

## Acceptance gate before integration

- CPU and Slang compile the same profile source.
- All profile renders have zero non-finite/out-of-range samples.
- Clear and red solid glass show transmission, Fresnel reflection, Beer tint, and TIR behavior.
- Aluminum/alloy reflections remain opaque and do not inherit glass medium attenuation.
- Thin film shows an angle-dependent tint without changing the metal transport branch.
- No Project-Zero source or scene is changed by the preview milestone.
- GPU capture is run only after the shader toolchain is available.
