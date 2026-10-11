# Native automotive showcase — 20 × 20

**Update:** actual CPU ReSTIR DI/GI scene captures are now included in `Exhibits/Gallery/AutomotiveShowcase/ReSTIR/`. See “Executed ReSTIR viewport captures” below. The earlier standalone studio swatches are not those captures.

The default C++ `ShowcaseStructure` now constructs **400 spheres**, with **20 material families × 20 parameter variations**. Families 0–14 remain their original kinds; their sweeps now have 20 positions instead of 15. The five additional families occupy rows 15–19 (zero-based). Scene revision **6** invalidates an exported revision-5 showcase.

The complete scene has 415 materials, 405,026 triangles and 849 spans, including plinths, the original scattered field, studio fixtures and interface housing. The scatter ring was moved outside the wider grid while retaining the 80 m floor.

## Five added native families

| Family | Main sweep | Distinction |
|---|---|---|
| Candy Ruby | Density 4–12; coat roughness .08–.12; dye strength | Neutral dielectric reflection, red absorption over reflective silver flakes |
| RGB Glitter | Density .5–16; diameter .45–1.10 mm | Stable red/green/blue colour families beneath a blue-absorbing coat |
| Iridescent Pearl | Thin-film thickness 220–850 nm | Low-F0 dielectric flakes with a high-index interference coating (IOR 2.3); not emissive rainbow noise |
| Metallic Cobalt | Density 1–8; diameter .08–.30 mm | Blue pigmented base, silver flakes, neutral clearcoat |
| Metallic Copper | Density 2–12; diameter .08–.30 mm | Warm pigment and copper-coloured flake reflectance, neutral clearcoat |

These are **artist-authored look-development recipes**, not measured manufacturer formulations. Metal/glitter diameters deliberately span coarse custom finishes. Linear Rec.709 colour values are used throughout; do not paste sRGB picker bytes directly into these fields.

All recipes start with clearcoat weight 1 and IOR 1.5. The outer coating is a dielectric, not a fully metallic surface. `CoatColor` means normal-incidence round-trip transmittance. The adapter applies an angle-dependent absorption path through the layer while keeping the outer reflection neutral. It is a thin-layer approximation: no refracted substrate direction or multiple internal reflections are solved.

### References consulted

- Blender Principled BSDF manual: layer ordering, metal/dielectric distinction, separate coating and thin-film parameters: https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/principled.html
- Blender 4.0 shading notes: coat tint and coat IOR are separate from metal tint: [1](https://developer.blender.org/docs/release_notes/4.0/shading/)
- Existing thin-film approximation provenance: `Docs/AutomotiveFlakePaint.md` (Belcour/Barla reference). No external shader source was imported for this pass.

## Actual C++ / shader integration

- `Engine/ContentInterchange/AutomotiveShowcasePresets.h`: the five native authoring recipes and family labels.
- `ShowcaseStructure.cpp/.h`: actual 20 × 20 mesh/material creation, descriptive names for new families, revision invalidation, scatter placement.
- `MaterialDescriptor.h`: profile ID and sweep outside the existing 58-float prefix.
- `MaterialCodec.cpp`: `slate_automotive_profile` and `slate_automotive_sweep` in the existing glTF extras encode/decode table.
- `MaterialIndex.cpp/.h`: these fields occupy the two previously reserved floats (`Slate2.zw`); the resident GPU slab remains **304 bytes**. Invalid/fractional IDs fall back to the standard material path. Sweep is sanitized. Discrete IDs follow the dominant slab when flattened, rather than being numerically interpolated.
- `Engine/Shaders/AutomotiveShowcase.slang`: finite-flake colour/density model plus a neutral clearcoat with absorption. Implements a matched sampling/PDF proposal: half cosine hemisphere, half coat GGX VNDF. The proposal has full upper-hemisphere support, but is **not a specialised glint importance sampler**.
- `MaterialEvaluation.slang`: opt-in dispatch through the existing evaluation, sampling and PDF entry points. Existing families retain the standard BSDF.
- `ReSTIRViewport.slang`: resolves persisted profile data, interpolated material UVs and ray-cone footprint. New families bypass the legacy perturbation of the shared normal, so flakes do not distort the coat.
- `Projects/Project-Zero/Host/MaterialLevelViewport.cpp`: matching native binding at primary and bounce surfaces, including the previously missing legacy density/scale vector initialization. Native BSDF inputs use const references to avoid large record copies.

The showcase UV-to-metres mapping is for the generated 1.10 m diameter spheres (circumference 3.45575 m). Reusing a recipe on a different mesh requires an appropriate material-UV metric; arbitrary mesh scale invariance is not certified.

The native RGB sample uses the shared weighted RGB palette. This adds native **presets**, not an eight-row palette editor to the C++ inspector. Other palette families remain available through the shared shader API. Coat anisotropy and independent coat-normal maps are not consumed by this preset evaluator; these samples use isotropic coats and the smooth material frame.

## Verified

- Release, Debug, ASan+UBSan: **39,127 checks each**, including 400 sphere count, 20 members of every new family, preserved legacy dispatch, resident packing, matched sample/PDF values, finite nonnegative BRDFs, distant stability and neutral Candy reflection.
- Filtered white-furnace checks: all five families at view cosines 1, .5 and .1; maximum measured channel **0.80004**. This is a numerical sanity check, not a universal energy-conservation proof for resolved flakes.
- Native entry-point/test/preview frames: Release **4384 B**, Debug **4608 B**, Sanitized **6464 B**, all below the 8 KiB gate. Exact compiler output is in `NativeProof.json`. Linux measurements, not Windows certification.
- Existing default-scene gate: all tests pass (emissive geometry, original lobe census, span coverage and scene bounds).
- Native `MaterialLevelViewport.cpp`: C++20 syntax check passes.
- Complete modified `ReSTIRViewport.slang`: compiled to SPIR-V for Vulkan 1.2 with glslang 16.6.0, source pin `e1b562a8bed273a02f30b59b66a5d499793cede5`.
- Five PNGs are actual C++ renders, using real authored/resident material data. The narrow coat is integrated with VNDF samples to avoid a discrete-softbox-dot artefact. They are reference swatches, **not screenshots of the full application**.

## Not claimed

No complete application link/launch, hardware GPU execution, Windows run, GPU performance measurement, or new native palette-editing UI. The codec table is wired, but a complete glTF export/import round-trip was not executed in this proof. The scene gate uses the upstream codec stub for export; it tests real geometry construction, not file persistence.

## Reproduction and target

This checkout originally did not contain the full C++ application. Modified target files are delivered at their actual engine/project paths, along with `Tools/Build/Patches/AutomotiveShowcase.patch` for the immutable C++ baseline `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b` of `SultanAladin/Frontier-`. The patch is scoped to materials/showcase and does not replace the completed inspector work. Apply-check before applying it to another C++ checkout; do not force it over a diverged source tree.

```sh
python3 Exhibits/Workbench/AutomotiveShowcase/RunProof.py \
  --glslang /path/to/glslang
```

The runner uses the configured `gh` connection to retrieve pinned dependencies, validates their Git blob hashes, and builds an isolated overlay under `.cache/automotive-showcase`. It never changes the immutable dependency baseline. Vulkan declarations in the native geometry proof are type-only stubs. `--skip-render` rejects changed native source inputs; omit it after shader/preset changes.

Review `Exhibits/Gallery/AutomotiveShowcase/index.html`, `Grid.json` and `NativeProof.json` for the five native samples, the complete 400-item material census and exact commands/results.

## Executed ReSTIR viewport captures (follow-up)

The earlier delivery contained only isolated studio renders. This follow-up builds and **executes** `Projects/Project-Zero/Host/MaterialLevelViewport.cpp` with `--restir` against the actual default scene. It does not substitute the standalone `NativeShowcaseProof` studio. The native host uses ReSTIR DI temporal/spatial reuse and its GI vertex pool, real BVH traversal, actual baked shading tables, the scene's sky/sun, emissive geometry and interface proxy.

- `ReSTIR/grid400.png`: 512×512, 32 accumulation frames. The default scene includes all 400 spheres. Physical light-panel backs obscure some spheres from the review camera; they have not been hidden for the capture.
- Five `ReSTIR/paint-*.png` family close-ups plus `paint-glitter-macro.png`: 384×384, 16 frames, first variation of each family. All 400 spheres remain in the scene.
- Every capture retains `*-raw.png` before the native three-level à-trous filter. The PNGs use the engine's ACES/manual-exposure output, with no external retouching or upscaling.
- A missing automotive UV/footprint binding at the GI pool's vertex was corrected, in addition to the existing primary and path-trace surface bindings.
- `RenderProof.json` records the actual compile command, execution arguments, source/binary hashes, durations and output hashes. Per-view logs distinguish this estimator from brute-force path tracing and report invalid sample counts.
- The 768×768 attempt was killed by the sandbox's roughly 3.72 GiB memory limit. The overview was reduced to 512×512. These are finite-sample diagnostic renders, not convergence/performance certification.

**CPU ReSTIR execution is now verified; hardware Vulkan execution and the Windows editor application remain unverified.** `NativeProof.json` at the gallery root is the earlier studio/unit-test report from commit `0a4bc23`, not the execution record for this follow-up. Use `ReSTIR/RenderProof.json` for the new captures.

Reproduce from the repository root (the builder restores and hash-verifies the pinned dependencies):

```sh
python3 Exhibits/Workbench/AutomotiveShowcase/BuildReSTIR.py
python3 Exhibits/Workbench/AutomotiveShowcase/RenderReSTIR.py
```

The capture script resumes only source- and hash-matching results. For a fresh capture after native source changes, move the old `ReSTIR/RenderProof.json` aside before running it. Preserve its associated images/logs if keeping historical evidence.
