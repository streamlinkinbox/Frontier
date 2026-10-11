# Clouds — global and local volumetrics together

Target pin: `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`. Both existing outliner entities now use the same native C++ inspector component, with distinct global and local layouts and independent settings. Fog is not rerouted or redesigned in this phase.

## Important renderer boundary

The inspected target has a real **CPU volumetric march for global clouds, local clouds and local fog**, with shared sun-shadow sampling. Its GPU viewport shader has **cloud-shadow weather**, but not the corresponding full global/local cloud-volume march. This stage binds the native inspectors to the existing CPU volume settings and preserves the separate GPU shadow controls. It does **not** add or claim a GPU cloud-body renderer, scene-camera preview, cloud bake or GPU residency proof.

## Coverage replacement selected by the user

The repeated-cloud drawing is replaced with **one continuous top-down density mask**. This is not a collection of icons or cloud silhouettes. `CloudDensityPreview.h` evaluates the engine's actual `VolumetricMedia::CloudDensity` and `LocalDensity` functions:

- Global view: a 6 km-wide window centred at the world origin; the north–south span follows the display aspect ratio.
- Local view: aspect-correct framing around the actual box bounds, sampling world-anchored noise and the engine's soft ellipsoidal interior mask.
- Eight midpoint samples through the vertical extent form a mean-density projection. A separate X–Z slice supplies the cross-section.
- The display mapping is `1 - exp(-3 × density)` into a blue-grey palette. This is **not** photometric radiance, integrated optical opacity or a measured percentage of sky covered.
- The percentage control edits the existing 0–1 noise coverage parameter. Higher values monotonically fill density gaps; it is not labelled as an exact clear-sky area measurement.
- Sampling is static at time zero. Follow wind still controls the real CPU cloud model; the editor diagnostic is not an animated wind simulation.
- Two 192×96 RGBA managed textures use 147,456 heap bytes per ImGui context. A value/aspect cache avoids resampling unchanged settings, and context shutdown unregisters both textures.

This intentionally replaces the illustrative `CloudCoverage` drawing in the native port. The remaining card organisation follows `atmosphere-shared-controls.jsx` and `property-graphics.jsx`, adjusted for real engine semantics.

## Global volumetric clouds

- Enabled and Follow wind quick tiles.
- Continuous coverage field and percentage input.
- Draggable cloud-base diagram, 100 m keyboard steps, native numeric/slider input.
- Layer thickness with the engine's vertical density profile and effective ceiling-clipped top.
- Density, feature scale, anisotropy, six cloud types, anvil and ceiling.
- Anvil affects **Cumulonimbus**; anisotropy affects directional scattering, not the density diagnostic.
- Existing GPU cloud-shadow and shadow-clock settings remain in a collapsed supplementary section. They are a **separate field**, not represented as the full volume renderer.

Altitude is labelled **world Z**, not terrain-relative AGL: the underlying model samples world-space Z and does not query terrain height. Authored thickness is retained when the ceiling clips the effective slab.

## Local volumetric clouds

- The same coverage, density, feature-scale, anisotropy and wind controls, independent of the global layer.
- World-space centre and half extents through real native X/Y/Z controls.
- A proportional bounds diagram, full dimensions, an actual density cross-section, and base/top world-Z readouts.
- Existing volume-marker collection and movement remain connected to those same coordinates.
- Half extents are clamped to 0.1–100,000 m; centres to ±1,000,000 m. Nonfinite edits retain previous values. The local cloud remains the engine's existing single local-cloud slot; this stage does not invent a multi-volume collection.

Both scopes clamp scalar edits to declared domains. Changing the global ceiling also bounds the base. Fog's existing generic property route remains intact for its own phase.

## Architecture and verification

`CloudsInspectorPanel.cpp` includes only engine/editor interfaces, not Project-Zero headers. Project settings and application stay in `CelestialSequence`, delivered through `CloudsInspector.patch`. Dedicated cloud and supplementary-shadow sheet builders avoid adding more properties to the large legacy generic builder. No GPU uniform layout or shader algorithm changes are introduced by this phase.

```sh
python3 Exhibits/Workbench/Clouds/RunNativePanel.py
python3 Exhibits/Workbench/Clouds/RunNativePanel.py --debug
python3 Exhibits/Workbench/Clouds/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/Clouds/ServeNativePanel.py --port 5183
```

The proof covers both native routes, independent state, finite/range validation, bounds and marker movement, actual CPU staging, real density response and monotonicity, and combined volumetric marching. It also drives the native coverage slider, base diagram, keyboard input, local-axis drag and enable tile, then captures narrow/scroll/2× output and destroys the context under sanitizers.

Captured panels are actual headless ImGui draw lists rasterized by the existing CPU proof backend, with lossless PNG compression only. They are **not interactive browser inspectors**. The gallery on port 5183 provides both scopes together.

Release and Debug use a 256 KiB Linux process stack, with an 8 KiB per-selected-function frame gate. Sanitizers use the default process stack and leak checking. These are GCC/Linux measurements, not Windows/MSVC certification. A shader compilation pass does not prove GPU device execution or imply the missing cloud-volume GPU path exists.

## Recorded results — 2026-09-23

| Mode | Clouds | Stars | Moon | Sun | Lens | Atmosphere |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Release | 582 PASS | 157 PASS | 146 PASS | 326 PASS | 74 PASS | 62 PASS |
| Debug / O0 | 582 PASS | 157 PASS | 146 PASS | 326 PASS | 74 PASS | 62 PASS |
| ASan + UBSan | 582 PASS | 157 PASS | 146 PASS | 326 PASS | 74 PASS | 62 PASS |

The Cloud count includes 512 individual pixel monotonicity assertions. All nine Cloud/Stars/Moon native source-and-image manifests match the final files. The unchanged GPU shader table also passes **18/18 compilation**; this is a regression check, not evidence of GPU cloud-volume support.

Measured release / debug frames: cloud recorder **704 / 1,072 bytes**, cloud sheet builder **416 / 5,856 bytes**, supplementary shadow builder **320 / 3,312 bytes**. The two sheet-builder frames can be nested; the 8 KiB limit is per function, not a claim that their combined call chain fits 8 KiB. The complete proof passes the 256 KiB Linux process-stack limit.
