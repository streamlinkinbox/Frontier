# Automotive flake paint — implemented review material

The shader and its native/live exhibit are implemented. This is the **standalone automotive review stage**, not a claim that the new material has replaced Project-Zero's production material path.

## Delivered

- `Engine/Shaders/AutomotiveFlakePaint.slang`: shared, allocation-free material preparation and BRDF evaluation.
- `Exhibits/Workbench/AutomotiveFlakes/`: native C++ reference renderer, tests, shared studio scene, build/export runner and browser execution tests.
- `Exhibits/Gallery/AutomotiveFlakes/index.html`: live WebGL 2 material exhibit (preview port 5188), with native reference PNGs below it.
- Thirteen native 960×600 renders: smooth / metallic / pearl, close / distant, six changing-light frames, and blue-coat/RGB-flake examples at 4× and 12× density. The native images use four samples per pixel, with lossless PNG compression only.

The live exhibit exposes density (0–16×, with slider and numeric entry), diameter (0.02–2 mm), up to eight weighted flake-colour ranges, orientation spread, individual-flake roughness, clearcoat roughness/tint/strength, pearl strength, film thickness, pigment, light angle and viewing distance. Dragging changes the camera view; **Move light** animates the actual light. The controls change shader uniforms, not image selection.

## Model

### Finite flakes

A deterministic integer hash places oriented elliptical flakes in material-space cells. Full-cell position jitter and a bounded 3×3 search avoid the previous sine-band pattern and tightly centred dot grid. Overlapping flakes select one facet rather than adding unbounded reflectance. The seed is material-owned; no frame/time/screen-pixel input participates in flake identity.

The diameter is authored in millimetres; material UVs and derivatives are supplied in metres. The exhibit projects object-local X/Y onto a curved ellipsoidal swatch. Consequently, the physical surface size stretches at steep curvature: production assets should supply metrically scaled material UVs rather than treating this projection as a globally uniform surface parameterization.

Individual flakes use a Gaussian slope distribution. Light and view directions determine the evaluated half-vector. There is no emissive sparkle term, and density exactly zero produces exactly zero flake coverage in both resolved and filtered paths.

### Density above one and independent flake colours

Density is now an expected population multiplier rather than a probability clamped to one. Sixteen bounded, independently seeded populations are available; the final fractional population uses probability `density - floor(density)`. Existing population IDs stay stable. Increasing density does **not** change flake diameter or multiply brightness. Opaque overlap remains bounded to one resolved facet and the filtered coverage stays in [0,1]. At the 16× cap, further entered values are bounded explicitly.

The measured mean resolved coverage over 2,048 test positions was 0.25879 at 1×, 0.43528 at 2×, 0.68604 at 4×, 0.89307 at 8×, and 0.98682 at 16×. These are coverage measurements, not emitted-light multipliers.

`AutomotiveFlakePalette` supplies up to eight linear-RGB ranges and relative weights. Each flake selects one family and interpolates within its range using hashes of its persistent facet key. Colours are not re-rolled when lighting or camera changes. The distant population uses the weighted endpoint means; averaging these before nonlinear pearl Fresnel remains an approximation. Count zero or all-zero weights use the original silver/`FlakeReflectance` fallback. The UI keeps at least one family and explicitly reports the all-zero fallback.

A red/green/blue native fixture sampled 921 / 947 / 925 visible flakes across the three equally weighted families. Tests also check range membership, disabled shares, eight slots, colour/normal independence, stable identities and a safe palette-count bound.

### Clearcoat and pigment

A separate GGX clearcoat uses the unperturbed material frame. Its added colour control is an **artistic tint**, applied to the reflected coat and attenuating the paint beneath it; it is not a spectral dye/Beer–Lambert coating model. Tint strength zero retains the original untinted dielectric-coat result. Changing flake density, diameter or orientation does not change the coat BRDF. The base consists of pigment plus metallic-flake reflection, attenuated by the coat's incoming and outgoing Fresnel transmission factors.

This is a thin-layer approximation: it does not trace refracted directions through a finite coating, solve multiple internal bounces, or claim a rigorously energy-compensated layered BSDF. Macro masking of tilted flakes uses a bounded Smith approximation. The separate coat fixes the old normal-perturbation model's conceptual coupling without silently modifying existing runtime material records.

### Pearl

The optional pearl term uses the existing engine `ThinFilmReflectance` implementation from `MaterialEvaluation.slang` (its two-term Airy/sensitivity approximation). Nanometres in the new parameters are converted to the engine's micrometre convention. The inherited metal phase approximation remains; this is not a new full spectral conductor model. Conventional metallic flakes work with pearl strength zero. The default high-reflectance metallic substrate gives a subtle pearl response, not the exaggerated rainbow mode of the linked Shadertoy.

### Distance filtering

The largest supplied UV derivative controls a conservative transition from explicit flakes to a Gaussian population lobe. It uses a Poisson approximation for overlapping-flake coverage; this is **not** an exact anisotropic footprint integral or the Deliot–Belcour distributed-binomial algorithm.

For the default normal-incidence NDF test, the finite-cell sample mean was **1.048851**, versus the population approximation **0.982384** (about **6.77%** difference, within the declared 12% gate). This remaining mean bias is disclosed rather than presented as exact energy preservation. Fine unresolved flakes lose their individual sparkle deliberately and converge to a smooth metallic response.

## Verification actually performed

| Gate | Result |
|---|---|
| Native Release | 58,693 checks PASS |
| Native Debug | 58,693 checks PASS |
| Native ASan + UBSan | 58,693 checks PASS |
| Native renders | Thirteen PNGs; zero nonfinite rendered samples |
| GLSL compile, link and execution | PASS in Chromium WebGL 2 |
| CPU/GLSL numerical comparison | 64 RGB probes / 192 channels; max relative error **0.00068603** (0.068603%) |
| Browser controls | Presets, density, size, orientation spread, flake/coat roughness, distance, film thickness, light movement and camera orbit change rendered pixels; reset is deterministic |
| Browser page errors | None |

Native tests cover exact zero-density behavior, deterministic material coordinates, finite/nonnegative output, reciprocity, coat independence, LOD endpoints, distant seed independence, backface rejection, pearl response, scalar-parameter limits and population/furnace sanity.

The sampled default-material furnace RGB integrals at view cosines 1.0 / 0.5 / 0.1 were respectively:

- `(0.17541, 0.20601, 0.27470)`
- `(0.19190, 0.21909, 0.28242)`
- `(0.41267, 0.42404, 0.45242)`

These are sampled sanity checks for the default material, **not a proof over the entire parameter domain**.

### Stack and platform

GCC builds use `-Wall -Wextra -Werror` and record stack usage. The selected shader/scene/proof entry points pass an 8 KiB per-frame gate. Maximum measured frame among those entry points: Release **4288 B**, Debug **4784 B**, Sanitized **6912 B**. Release and Debug execute with a 256 KiB Linux stack limit; sanitizers use their default stack. Large images and containers are heap-backed.

Browser execution used ANGLE / **SwiftShader**, a software graphics backend. This is genuine GLSL execution, not a screenshot-only comparison, but it is **not physical-GPU performance measurement, Project-Zero Vulkan execution or Windows validation**.

Exact commands, individual stack records, probe values, image/source hashes and browser renderer identification are in the gallery JSON reports.

## Integration boundary / remaining work

The new source provides:

```cpp
AutomotivePaintParameters p = AutomotivePaintDefaults();
AutomotiveFlakeSurface s = AutomotivePrepareFlakes(p, materialUvMetres, duvdx, duvdy);
vec3 f = AutomotiveEvaluatePaint(p, s, viewInMaterialFrame, lightInMaterialFrame);
// Accumulate f * incidentRadiance * max(lightInMaterialFrame.z, 0).
```

For multiple flake colours, apply a palette after preparation:

```cpp
AutomotiveFlakePalette palette = AutomotiveRgbPalette(); // editable ranges and shares
p.Density = 4.0;
s = AutomotivePrepareFlakes(p, materialUvMetres, duvdx, duvdy);
s = AutomotiveApplyFlakePalette(s, palette);
p.CoatTint = vec3(0.03, 0.2, 0.9);
p.CoatTintStrength = 0.35;
```

The native port defines `FRONTIER_NATIVE_FLAKES` to pass parameter/palette records by const reference while preserving identical function bodies for GLSL. This avoids large per-light struct copies and keeps measured entry-point frames below 8 KiB.

Directions must be normalized, finite and expressed in the **unperturbed** material frame. Pigment/reflectance inputs must be finite. Scalar authoring parameters are bounded in the evaluator. The preparation must be done once per surface sample, not once per light.

The existing `AutomotiveMaterialProfiles.slang` fallback, `ReSTIRViewport.slang` path, material slab layout, serialization and native editor inspectors are **unchanged**. The new code is opt-in for the standalone review; it does not silently reinterpret `SlateGlintDensity` or install a second shader-normal perturbation on top of the old one.

Production adoption still needs a matched BSDF sampling/PDF strategy, host property bindings and material-record migration, proper ray/texture footprints, HDR environment-light sampling, and actual GPU/Windows regression. The exhibit currently uses two quadrature-sampled rectangular softboxes plus an explicitly labelled diffuse ambient fill. Very sharp coat settings can expose finite light-quadrature resolution; no HDR IBL or full global illumination is claimed.

No shader code was imported from Shadertoy or the research repositories. The supplied Shadertoy is a visual/technical reference, not an exact-copy claim. No other deferred inspectors or Water/Fluids were implemented.

## Reproduce

With the existing pinned reconstruction available:

```sh
python3 Exhibits/Workbench/AutomotiveFlakes/RunProof.py
python3 -m http.server 5188 --bind 0.0.0.0 --directory Exhibits/Gallery/AutomotiveFlakes
```

The first command compiles the same material source as C++, runs all three modes, renders the native images and emits flattened GLSL for the gallery. `--skip-render` reruns the checks without regenerating the native images; only use it when the existing image provenance is still appropriate.

For the browser execution checks (build-time dependencies only):

```sh
npm install --prefix .cache/native-icon-browser --no-audit --no-fund playwright-core@1.63.0 @sparticuz/chromium@153.0.0
node Exhibits/Workbench/AutomotiveFlakes/TestBrowser.mjs
```

The new material does not change the immutable baseline. The proof uses the reconstructed target's existing math shim, thin-film implementation and PNG writer; their hashes are recorded alongside the new sources.
