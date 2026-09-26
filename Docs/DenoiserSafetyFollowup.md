# V8 safety fixes and GPU measurement follow-through

This implements the first three steps of the [V8 review](V8DenoiserReview.md) and instruments the fourth. GPU measurement and performance/quality tuning remain **pending hardware execution**. No working Vulkan device/ICD is available in this sandbox; CPU checks are not substitutes for GPU timings or images.

## Changes

### Albedo quantization

Clamp the finite authored albedo into `[0.02,1]` before quantizing to RGBA8. Use the resulting value for both division and storage. Dark channels now divide and remultiply by the same `5/255` value, eliminating the old approximately 1.96% attenuation. Variance's scalar denominator uses the luminance of that same quantized value.

This does NOT solve the review's broader whole-radiance demodulation/colored-variance limitations. Metallic reflections and deterministic fog/flare still need dedicated visual checks.

### Immutable previous history

Bindings 3, 18 and 19 are now fixed two-element storage-image arrays: element 0 is current output, element 1 is an immutable previous-frame snapshot. Binding 31 remains the highest, variable-count texture binding. The compute set needs **10 storage-image descriptors**, checked against the device limits at pipeline creation. Executable and shaders must be rebuilt together.

Before a history-consuming compute dispatch, queue-ordered image copies snapshot mean, normal/depth and moments/identity. Source and destination barriers cover prior shader reads/writes, transfer accesses, subsequent snapshot reads and current-output writes. Every shader history read uses the snapshot; current images remain the outputs used by denoising/exposure. There are no per-frame history descriptor rewrites.

The first/reset frame skips copying undefined history. Resize/resource recreation, render-extent changes, debug frames and map-only frames force fresh accumulation on the next compute frame. Snapshots share the existing storage-image creation/destruction lifecycle and fixed full-target extent.

**Cost, not a free optimization:** snapshots add 40 bytes per allocated pixel (16-byte color, 8-byte surface, 16-byte moments), approximately **79.1 MiB at 1920×1080** or **316.4 MiB at 3840×2160**, excluding allocation overhead. Each copied frame transfers a logical 40 bytes read + 40 bytes written per rendered pixel. Copies cover the rendered region, not the entire full-size allocation when using reduced resolution. Device compression/cache behavior and actual milliseconds are unmeasured. Fixed descriptors and simple copies were chosen for a conservative first implementation; a future ping-pong design may avoid the copies but requires its own descriptor/lifetime plan.

### Presentation dither

`PresentationDither.slang` supplies the same deterministic, screen-position hash to the denoiser, the denoiser-off ReSTIR resolve and the map-only ShadowResolve's shaded/sky/emissive exits. It is bounded to half an 8-bit LSB and does not accumulate into history. Diagnostic/debug outputs are unchanged. Shader include dependencies were added to CMake and the Windows build script.

### Measurement instrumentation

New timestamp pairs:

- 20–29: denoise levels 0–4, including each level's synchronization.
- 30–31: history snapshot copy and barriers.

The development telemetry CSV appends:

`GpuDenoiseL0Ms,GpuDenoiseL1Ms,GpuDenoiseL2Ms,GpuDenoiseL3Ms,GpuDenoiseL4Ms,GpuHistorySnapshotMs`

Missing/unexecuted timestamp pairs yield zero, not uninitialized values. `GpuPostMs` now excludes the separately measured snapshot cost, but still includes denoise/luminance and other unowned trailing work. Across old/new builds compare **Post + Snapshot**, not Post alone. Timestamp intervals include synchronization and need not be strictly additive. Added timestamp instrumentation can itself affect measured scheduling.

The implementation deliberately leaves the sigma schedule, four-level Minimal tier and filtered-history feedback unchanged. No unmeasured "performance-neutral" tuning is claimed.

## Validation performed here

Evidence: `Docs/DenoiserSafetyEvidence/`.

- M9 denoise gate: **97/97 PASS** against the actual staged filter.
- TemporalIdentity fast regression: **GREEN**, including moving shadow/history and static-scene control. CPU mirror, not GPU dispatch.
- Safety suite under **ASan/UBSan**: **102,429 shader-expression/dither arithmetic checks**. Uses the extracted production albedo expression, shared production dither helper and an explicit RGBA8 store/readback model; covers dark/saturated channels and out-of-range finite inputs.
- Real telemetry CSV exporter round-trip, including invalid-frame zeroing.
- 200 randomized invocation-order history models; immutable snapshots are order-independent and the unsafe in-place control fails. This models the dependency, not Vulkan scheduling.
- Source guards for snapshot ownership, copy-before-dispatch, resets, usage flags and descriptors.
- **22/22 production shaders compile to Vulkan SPIR-V.** Reflection verifies two-element history arrays at bindings 3/18/19 and the unchanged 544-byte post ABI. Weather composition/history guardrails pass.
- Real-header C++ syntax checks: SwapchainExchange, VisibilityExchange, TelemetryProbe, DiagnosticInspector and GameExecution.

The existing M9 transform was updated to retain shared includes and remove the GLSL-only include extension. Its source reconstruction check remains active.

Reproduce:

```sh
python3 Tools/Tests/TestDenoiseSafety.py
MATERIAL_SCENES_EXT="$PWD/.cache/denoiser" bash Exhibits/Workbench/Materials/CheckMaterialDenoise.sh
GLSLANG="$PWD/.cache/denoiser" MATERIAL_SCENES_EXT="$PWD/.cache/denoiser" bash Exhibits/Workbench/Materials/CheckTemporalIdentity.sh fast
python3 Exhibits/Workbench/WeatherWiring/CheckGpuWeather.py --compiler .cache/denoiser/glslang-build/StandAlone/glslang
```

The optional header/compiler paths above describe this sandbox, not required runtime dependencies.

## GPU acceptance and tuning gate — outstanding

1. Rebuild the Windows executable and shaders together:
   ` .\Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild `
2. Run a development build and close normally so `Diagnostics/ProjectZero_TelemetryProbe_Frames.csv` is written relative to its working directory. Save separate baseline/candidate CSVs. For a baseline use the previously built V8 executable; this session's branch must remain unchanged.
3. Match GPU/driver, resolution, tier, scene, exposure, denoiser state and camera motion. Warm up, then collect at least 300 comparable frames; repeat each condition at least three times. Disable frame caps/VSync when assessing throughput, but compare GPU milliseconds rather than FPS alone.
4. Test denoise ON/OFF; GI compute and GI-off map-only; Minimal and Standard; resize/render-scale switches; camera cuts and subpixel pans; crossing/occluding objects; dark metals and saturated checkerboards; fog/flare over textures. Capture matched screenshots and motion clips. Still PSNR is not evidence of temporal stability.
5. Enable Vulkan validation/synchronization validation through the available SDK/layer tooling. Check snapshot copy/layout/descriptor errors and all mode transitions. No validation-layer run was possible here.
6. Compare CSVs:

```sh
python3 Tools/Build/CompareDenoiseGpu.py baseline.csv candidate.csv --warmup 120 > comparison.md
```

The tool excludes invalid and non-compute frames, refuses too few usable samples, and reports median/p95. It cannot verify matched scene settings or image quality. It does not benchmark the map-only path, which needs a separate shadow-stage comparison.

**Next decision depends on these measurements:** if copying dominates, evaluate immutable ping-pong histories; if early denoise levels dominate, test shared-memory tiling; if late levels dominate, test conservative adaptive work. Do not enable an extra Minimal-tier pass or history feedback solely because CPU gates passed.
