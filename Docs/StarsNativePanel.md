# Stars — native inspector

Target: `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`, reconstructed by the existing full-panel proof runner. Design references: the Stars branch of `extra-inspectors.jsx` and the `TwinkleSignal` / `CelestialRotation` components in `celestial-controls.jsx`.

## UI and ownership

A wide Star field card is followed by Twinkle and Celestial rotation cards (stacked at narrow widths). The signal retains the reference's four-second trace, sample and instantaneous percentage. The pole dial retains its schematic constellation and rings; pointer dragging, arrow keys, Shift steps and Home change the actual authored rotation. Brightness and point size remain available in a separate renderer-scale card.

The field chart uses **8,920 real catalogue records**, not the reference's illustrative 90-point sequence. These are retained unbinned by `StarCatalogueIndex`, and borrowed read-only through `EditorStarPreview` for the editor frame. The catalogue owner must outlive the frame; rebuilding the sheet clears old borrowed metadata. No Project-Zero header is included by the editor panel. Retaining the source records adds 285,440 heap bytes, not a large stack allocation.

The rectangular preview is an equatorial chart, not a scene-camera render: declination vertically, right ascension horizontally, with authored rotation applied as the inverse of the renderer's ray-to-equatorial transform. Dot radius and opacity are editorial display mappings. No bloom/glow is applied to the field. The pole constellation is explicitly schematic. Missing catalogue data produces an unavailable message and zero stars, never synthetic replacements.

Star field and Twinkle are real green/red quick toggles. **Bake / Use baked are disabled**: the engine has no baked-star playback path. Their presence does not claim a bake, upload, persistence or replay implementation.

## Real bindings

- Limiting magnitude 0–8 filters catalogue luminance using `10^(-0.4 × magnitude)`.
- Twinkle depth 0–100% and rate 0.2–3 Hz use the reference's normalized two-frequency signal. The monitor is the zero-phase example; each real star adds a stable direction-derived phase, so duplicated spatial-bin records never disagree.
- Twinkle time advances from simulation delta seconds, independently of the day-cycle speed. Negative/nonfinite deltas do not advance it. The uploaded time is reduced by the signal's common period to bound float precision loss. Disabled/depth-zero twinkle is steady; disabled stars gate the GPU brightness and time.
- Celestial rotation 0–360° offsets solved local sidereal time in both CPU and GPU paths without changing the observation/ephemeris itself.
- Brightness and point size retain the existing engine controls. All six scalar edits reject nonfinite values and clamp to declared domains.

Both `VisibilityRaster.cpp` and `PostRecords.slang` apply the magnitude cutoff and twinkle multiplier to the existing point-source profile. Daylight extinction, cloud transmission, spatial bins and the existing energy normalization remain intact.

## ABI

The post record grows from 192 to **208 bytes**, appending `PostStarEffects` at offset **192**: minimum luminance, modulation depth, frequency, time. Original star, lens, rainbow and cloud-shadow offsets remain unchanged; `PostLayers` remains at 128. The shader block and swapchain allocation/upload size change together. Cloud-shadow spare rows are **not** reused.

## Reproduce and evidence

```sh
python3 Exhibits/Workbench/Stars/RunNativePanel.py
python3 Exhibits/Workbench/Stars/RunNativePanel.py --debug
python3 Exhibits/Workbench/Stars/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/Stars/CheckShaders.py --compiler .cache/stars-glslang-build/StandAlone/glslang
python3 Exhibits/Workbench/Stars/ServeNativePanel.py --port 5182
```

The runner rebuilds and regresses Sun, Lens, Atmosphere and Moon first. Stars checks cover catalogue ownership/failure, scalar validation, packed controls and offsets, signal bounds, native pointer/keyboard interactions, narrow scrolling, 2× output and teardown. It also compiles the actual CPU raster implementation. Captures are headless ImGui draw lists rasterized by the existing CPU proof harness; they are not browser widgets or full scene-camera images.

Measured results are saved in `Exhibits/Gallery/StarsNative/{Release,Debug,Sanitized}{Proof.txt,Stack.json,Hashes.json}`. Non-sanitized runs enforce a 256 KiB Linux process stack and an 8 KiB per-entry-point frame budget. This is **not a Windows stack certification**. Shader compilation/reflection, if successful, proves compilation and layout only—not device execution, GPU residency, runtime descriptor correctness or full-application parity. Native UI tests do not claim rendered CPU scene-camera star pixels.

## Recorded results — 2026-09-23

| Mode | Stars | Moon | Sun | Lens | Atmosphere |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release | 157 PASS | 146 PASS | 326 PASS | 74 PASS | 62 PASS |
| Debug / O0 | 157 PASS | 146 PASS | 326 PASS | 74 PASS | 62 PASS |
| ASan + UBSan | 157 PASS | 146 PASS | 326 PASS | 74 PASS | 62 PASS |

**18/18 shaders compiled**. Actual viewport reflection confirms the 208-byte post block at binding 24, the new row at 192, and all previous offsets unchanged. All three Stars and Moon source/image manifests match the final files.

Selected release / debug frames: Stars recorder **640 / 624 bytes**; dedicated Stars sheet builder **400 / 2,832 bytes**; CPU staging (`ApplyTo`) **624 / 624 bytes**. The first Debug gate exposed a 20,688-byte shared `BuildOtherSheet` frame. Stars now routes directly to its own builder, avoiding that frame; this does not claim the legacy generic builders for other entities were optimized.

The proof additionally checks the actual `ApplyTo` CPU settings against the GPU record for catalogue, rotation, scales and all four effects lanes. These are binding checks, not a rendered CPU scene-camera comparison.
