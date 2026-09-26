# Sky / sun / cloud timing and resource logs

Added to the existing five-second `ProjectZero_TelemetryReport` reporting cadence. CPU figures are window means; GPU figures are the latest completed timestamp sample available at reporting time, not window averages. Workload flags describe current authoring/visibility state, not measured pixel coverage.

## Added measurements

| Row | Scope |
|---|---|
| CpuCelestialTickMeanMs | Complete CPU celestial tick, including ephemeris, wind and precipitation |
| CpuSunMoonSolveMeanMs | CPU ephemeris solve only; a subset of the tick, not additive to it |
| CpuSkyPackUploadMeanMs | CPU sky/sun record packing and RefreshSky host upload call |
| CpuWeatherPostPackUploadMeanMs | CPU shared post/weather packing and RefreshPost staging; includes non-weather post data |
| SkySunUniformPayloadBytes | Current sky/sun UBO payload: 144 bytes |
| CloudFogUniformPayloadBytes | Weather portion of the post UBO: 336 bytes |
| PostUniformPayloadBytes | Complete shared post UBO: 544 bytes; includes the weather portion, do not add them |
| CelestialBufferPayloadBytes | Current sky, moon, post and star buffer payloads |
| CelestialBufferAllocationBytes | Sum of those buffers' Vulkan memory requirements, cached until their handles change |
| WeatherDedicatedVolumeImageBytes | Zero: procedural cloud/fog density does not allocate its own 3D volume image |
| CloudViewStepBudget / LocalVolumeStepBudget / CloudLightTapBudget | Configured budgets, not actual executed sample counts |
| SunShown / SkyShown / CloudActive / LocalCloudActive / FogActive | Current feature/visibility switches |
| GpuSeparateSkySunCloudTimersAvailable | False on the current architecture; see below |

Existing per-level denoiser and snapshot-copy timings are now included in the periodic report as well as the development CSV.

## GPU scope: no fabricated separate timings

Sky evaluation, direct sunlight and visible cloud/fog integration execute inside the shared ReSTIR compute dispatch. Sun lighting can also execute in the GI-off shadow-map resolve. There is no independently timestampable sky/sun/cloud dispatch in those paths.

The log now explicitly prints **sky/sun/cloud N/A (shared stages)** and explains that `GpuReSTIRMs` includes geometry/material/lighting/weather work. Legacy reserved `GpuSkyMs`/`GpuVolumeMs` rows remain for compatibility; zero there does NOT mean no clouds or free rendering. The availability row is false. CPU packing/upload duration is never labelled GPU rendering time.

Separate GPU attribution would require a pass split or a deliberately designed GPU profiling/ablation mode. That renderer refactor is not part of this logging change. A/B feature disabling changes workload and is not an exact independent per-feature timer.

## Corrected measured GPU span

`VisibilityTelemetry::FrameMilliseconds` reads timestamp 0 through 11: visibility-front-end start through trailing compute completion. The periodic GPU total and application's timing summary use this measured span instead of a stale sum that assumed KernelMilliseconds still included denoising. This span **excludes later editor overlays, blits/presentation and CPU work**; it is not total board utilization or an FPS prediction.

Workload RenderPixels now uses actual rendered dimensions rather than full output dimensions, so reduced-resolution tiers report their true shading pixel count.

## Resource-accounting boundaries

The buffer allocation figure is not full VRAM usage. These buffers can use host-visible/coherent memory. It excludes driver overhead, residency, sky/moon textures, sky-dome bakes, scene textures, shared render targets/history, shadow maps, and unrelated renderer resources. Existing allocation logs and process RSS still provide broader context. No bytes are invented for an isolated Sun or Cloud GPU allocation that does not exist.

The cache is read from current Vulkan buffer memory requirements when resource handles change; it does not query those requirements on every stable frame. Runtime device values still need validation on the user's GPU.

## Validation

- `bash Tools/Build/CheckPerformanceTelemetry.sh`: **51 checks PASS**, using the real reporter and parsing its saved diagnostic rows, including CPU means, allocation/payload rows and measured-span behavior. This uses synthetic input to test logging, not synthetic performance evidence.
- Real-header C++ syntax checks pass for SwapchainExchange, VisibilityExchange, CelestialSequence, PerformanceTelemetrySequence and GameExecution.
- No Windows application/GPU timing or resource-residency measurement was performed here.

Rebuild normally, run the relevant sky/cloud scene, and retain `ProjectZero_TelemetryReport` plus the existing development `Diagnostics/ProjectZero_TelemetryProbe_Frames.csv`. Compare matching resolution, quality, camera, exposure and feature states.
