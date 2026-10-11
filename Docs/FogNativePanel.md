# Native Fog + Local Volumetric Fog

## Scope and delivery

Three dedicated C++/ImGui inspector routes replace the generic Height Fog, Atmospheric Fog and Local Volumetric Fog sheets. This extends the pinned native target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`, after the Clouds integration. It does not substitute an HTML inspector for engine controls.

- Height: enabled, density, exponential falloff height, sun scatter and linear RGB tint.
- Aerial perspective: enabled, density, start distance and Rayleigh/Mie blend. The diagnostic reads the current atmosphere's strength-scaled scattering coefficients and scale heights.
- Local: enabled, follow wind, density, coverage, feature scale, anisotropy, world position and half-extents. Existing scene markers remain connected to the same authored volume.
- Distance-versus-transmission graph, vertical height-density curve, RGB aerial curves and local wire bounds replace decorative fog banks/bands. The graph supports pointer and keyboard probe movement without altering authored density.
- Native colour and vector controls; responsive card layout, scrolling and framebuffer scaling.

`Exhibits/Gallery/FogNative/index.html` is a read-only viewer of losslessly encoded native captures. It is not an interactive browser implementation of these settings. Run `python3 Exhibits/Workbench/Fog/ServeNativePanel.py` to view it on port 5184.

## Engine connection

`Tools/Build/Patches/FogInspector.patch` carries the target CMake, editor routing, project builder and CPU raster changes. Standalone source files are `Engine/Editor/FogInspectorPanel.{h,cpp}` and `Engine/DisplayPresentation/FogModel.h`. The shared reconstruction runner copies these files and applies the patch after Clouds.

The audit found that local fog already reached `VolumetricMedia::March`, while height/aerial settings were not passed to the main CPU raster. This change adds that missing settings transfer and consumption. Authored enabled state, outliner visibility and the global celestial enable gate remain independent. The project owns settings and applies edits; the editor consumes the sheet and its optical metadata. Sheet reuse resets preview metadata.

- Finite geometry receives height then aerial composition after the existing cloud/local-volume composite and before colour encoding. Distance and endpoint altitude come from the camera and reconstructed world-space triangle hit.
- CPU sky/planet misses receive height fog only. Aerial is intentionally omitted there: the physical sky already integrates atmosphere.
- Height transmission preserves the legacy reference's `exp(-opticalDepth²)` law, with an exponential altitude profile. This is an artistic layer, not a claim of a unified physically coupled transport solver.
- Aerial attenuation integrates the remaining segment after start distance, using separate exponential Rayleigh/Mie columns and per-channel Beer transmission. Mie blend interpolates spectral Rayleigh versus grey Mie extinction.
- Analytic distance is capped at 100 km; altitude uses world Z relative to zero, not terrain AGL. Negative endpoint altitudes are clamped to the world datum.
- Tint and sun scatter change height in-scattering, not extinction. Aerial uses sky ambient for its source term.
- Local extinction, scattering, anisotropy and shadow marches use the existing shared CPU volumetric path.

No GPU fog-volume integration, GPU execution, image baking/playback or Windows runtime certification is claimed. The unchanged shader table is compiled only as a regression gate.

## Diagnostic contract

These are selected-medium diagnostics, not a camera preview or a combined-media image. Height/aerial probes are horizontal at world Z = 2 m; their graph spans are 500 m and 20 km. The local probe traverses +Y through the volume centre from its near face, spanning twice its Y half-extent. It uses the actual march with fixed light, ambient, budget, wind and time zero. Follow-wind still affects real rendering; the diagnostic is deliberately static. Independent marches at different lengths can show small sampling variation, not an exact cumulative optical-depth solution.

The 65-sample profile is cached by the complete optical/local key in per-ImGui-context heap storage and released by a context shutdown hook. The neutral-light swatch composites a fixed neutral target; its label explicitly distinguishes it from scene-camera output.

Invalid numeric edits are rejected or clamped before reaching authored settings: finite tint 0–1, positive bounds, finite position and the declared scalar ranges. Zero-density metadata reports clear rather than an arbitrary saturated distance.

## Verification

Commands, outputs, hashes and measured frames are under `Exhibits/Gallery/FogNative/`.

| Gate | Result |
| --- | --- |
| Fog Release | 127 checks passed |
| Fog Debug | 127 checks passed |
| Fog ASan + UBSan | 127 checks passed; leak detection enabled |
| Prior regressions, each of those three builds | Sun 326, Lens 74, Atmosphere 62, Moon 146, Stars 157, Clouds 582 passed |
| GPU shader compilation | 18/18 lowered to SPIR-V; compilation only |
| Source/capture manifests | 12 Moon/Stars/Clouds/Fog manifests matched |

The Fog proof assembles an actual emissive mesh with `GeometryStructure` and `SceneStructure`, finalises it and calls `VisibilityRaster::Render` at 8×8. It checks height and aerial attenuation in rendered output, aerial start distance beyond geometry, local-volume changes and disabled-state recovery. This is actual geometry-pixel testing, not just settings staging or helper math. It does not certify every camera direction, sky-miss path or GPU backend.

Additional assertions cover scalar round trips, finite-value rejection and bounds, optical laws, altitude response, spectral versus grey extinction, shared local marches, independent visibility staging, native slider/vector interactions, probe input isolation, narrow scrolling, 2× capture and teardown. Captures were inspected for the height and local layouts.

### Stack measurements

GCC `-fstack-usage`, bytes per function (not total nested call-chain usage):

| Entry | Release | Debug | ASan/UBSan |
| --- | ---: | ---: | ---: |
| Native Fog recorder | 848 | 1120 | 5792 |
| Dedicated Fog sheet builder | 168 | 4720 | 5904 |
| CPU settings transfer | 656 | 656 | 1600 |

The recorded selected entries are below the 8192-byte frame gate in all three modes. Release and Debug proofs run with a 256 KiB Linux process stack. Sanitizers use their default stack. These are Linux/GCC measurements, not Windows/MSVC guarantees.

## Reproduce

```sh
python3 Exhibits/Workbench/Fog/RunNativePanel.py
python3 Exhibits/Workbench/Fog/RunNativePanel.py --debug
python3 Exhibits/Workbench/Fog/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/Fog/CheckShaders.py
python3 Exhibits/Workbench/Fog/ServeNativePanel.py
```

Without `--reuse-build`, the runner reconstructs the immutable target and reruns the prior inspector chain before Fog. Do not run reconstructions concurrently: they share `.cache/cpp-sun-full`. `--reuse-build` is only for an already reconstructed matching mode. Compiler commands and source hashes accompany each proof; binary caches remain outside the deliverable.
