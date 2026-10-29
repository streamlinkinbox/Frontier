# Material reuse and glint rewrite audit

**Current direction:** cellular selected for automotive paint. Latest upstream audit after energy correction: **448/455 identical** at
`a76dbd34d12d5e0dbfafe24dd5aa9d4d5b414c73`; all material archives match. Earlier counts/proposals below are historical.
[Executed native material scene](../LightCompute/MaterialScene.md).

## Requested source — checked 2026-10-25

User source: `SultanAladin/Frontier`, branch `arena/5e581235-frontier`.
The live branch resolved to **be1d0a8e45d39738f37ccf711522a745e326e149** during this audit.
This is newer than the earlier restoration reference; the comparison is pinned to this exact commit.

| Compared scope | Identical / compared | Finding |
|----------------|----------------------|---------|
| Material archives, including library definitions/textures | 399 / 399 | Already present byte-for-byte |
| Material-prefixed ContentInterchange sources | 6 / 6 | Already present byte-for-byte |
| Entire Engine/Render subtree | 47 / 50 | Three vector-gradient differences, detailed below |
| Total | **452 / 455** | No redundant material import needed |

The layered material shader, paint shader, SurfaceMaterial codec/compiler and AutomotivePaint definitions
match the source. The audit includes the complete Render subtree for dependency visibility, not because all
its vector/browser files are material evaluators. Exact per-file Git blob identities are retained in
[Results/MaterialReuse.json](Results/MaterialReuse.json). This establishes source identity, not material
physical correctness, completed vehicle-renderer integration or newly executed material tests.

## Three differences — do not copy in isolation

- `Engine/Render/VectorCoverage.h`
- `Engine/Render/Shader/VectorCoverage.slang`
- `Engine/Render/Shader/VectorSurface.slang`

The linked branch adds up to four gradient stops and an explicit gradient axis. Its instruction structure
is **176 bytes**, versus **112 bytes** here. CPU structures, shaders and authoring/editor consumers must change
together. This audit did not replace those files or claim the newer gradient feature is available locally.
These differences are not a new optical-material model or a glint rewrite.

## Glint research already exists

- [Research and integration constraints](../../Documentation/Plans/Ongoing/MaterialGlintResearch.md)
- [Executed comparison, images and raw-result links](GlintComparison.md)

| Approach | Existing evidence | Rewrite direction |
|----------|-------------------|-------------------|
| Cellular flakes | Existing production property generator; lowest local benchmark medians | Retain old-material compatibility, not the desired new glint model |
| Glinty 2025 | Research plus an isolated third-party GGX adaptation | Leading candidate; authentic-reference, licensing and integration checks remain |
| Chermain 2020 | MIT reference adaptation; Beckmann/dictionary approach | Useful comparison, with stated lighting limitations |
| Loubet 2020 | Slope-space/specular-transport research, not executed in that benchmark | Not a drop-in fragment-shader speed comparison |

The benchmark used **WebGL2 through ANGLE/Vulkan on SwiftShader**, not the current native Slang vehicle
renderer or a physical GPU. The methods were not image-matched/equal-error full materials. Cellular's low
cost does not prove it is the appropriate physical glint model. No new speed comparison was run here.
The tested third-party Glinty adaptation was GPL-3.0 scratch material; do not copy it into production without
resolving licensing. Research names are not evidence that their authors' complete methods were reproduced.

## Reuse boundary and next implementation work

- Reuse the source material library, layering, textures and authoring interpretation; do not redesign them
  merely to make the vehicle fixture easier to shade.
- Treat glints/flakes/glitter as an explicit rewrite. Preserve existing material loading and legacy cellular
  compatibility, but do not silently retain cellular as the completed replacement.
- Validate BRDF evaluation against its sampling PDF, energy, layer attenuation, footprint/angle filtering,
  stable seeds, distant aggregate behaviour and moving-camera temporal stability.
- Integrate the reused layered material evaluation into scene lighting. The current vehicle compute facet
  stores a compact albedo/roughness/optical/emissive representation; it is not the full material-preview model.
- Run material checks under Sun/sky, analytic lights and emissive illumination. Source identity is not a
  substitute for white-furnace, reciprocity or actual scene captures.

This audit changes no material shader, preset or glint default. The user's rewrite direction is recorded;
it is not relabelled as implemented. Existing research's production-default hold remains until the replacement
is implemented and verified; it does not prevent the requested material reuse/evaluation work.

## Reproduction

```bash
python3 Exhibits/TextureStudio/Code/MaterialReuseAudit.py
```

Requires the existing GitHub connection and local checkout. The script reads the pinned complete GitHub tree
and compares local Git blob hashes, without checking out another branch or importing remote source files.

## Native evaluation resumed — 2026-10-25

[MaterialEnergy.md](MaterialEnergy.md) directly exercises the retained native scattering sequence:
24 fixtures are finite/nonnegative/reciprocal, but three exceed white-furnace reflectance 1.02
(maximum 1.36977433). No production shader/preset was altered. This is an existing-model finding,
not full material acceptance. [GlintPair.md](GlintPair.md) separately supplies the requested two matched
Glinty-derived/cellular renders, software timings and resource scope; cellular remains legacy.

## Cellular selected and scene bindings executed — 2026-10-25

The user has now selected **cellular** for metallic car paint; the prior Glinty rewrite direction is
superseded for this work. Glinty remains a possible fine-detail research path, not a selected engine default.
[MaterialScene](../LightCompute/MaterialScene.md) records the green flaked clearcoat, baked rubber,
thin transmitting glass, BVH mirrors, depth-screen plastic reflections and reused C++ shaderball/cloth renders.
This is a native scene variant, not full editor/material-library acceptance.

The linked branch advanced to **a76dbd34d12d5e0dbfafe24dd5aa9d4d5b414c73** (Baking Studio AO/curvature and UI).
The audit is now pinned there: **451/455 identical**; all **399/399 material archive files still match**.
The fourth difference is our `AutomotivePaint.slang` explicit compute-footprint path. Its ordinary production
fragment compiles to byte-identical SPIR-V against the original source; the other three differences remain
the unrelated vector ABI changes above. No blind merge/import was required for the optical materials.
The newer AO/curvature Baking Studio work was inspected by change inventory, not imported or fully validated here.

## Scoped energy correction — 2026-10-25

The latest rerun is **448/455 identical**, with **399/399 material archive files unchanged**.
In addition to the three vector ABI differences and compute paint extension above,
`ScatteringSequence.h` / `ScatteringEvaluation.slang` intentionally change diffuse/sheen allocation.
`MaterialSurface.slang` groups reciprocal coat entrance/exit factors before multiplying incoming scattering.
Production material SPIR-V therefore intentionally differs from the pre-energy checkpoint; the earlier
byte-identity statement applies only to the paint-only comparison at `b6bc49e`.

[Native/Vulkan energy acceptance and actual before/after renders](MaterialEnergyAcceptance.md).
No replacement material library, archival asset edits, vector ABI import or Baking Studio merge.
