# Frontier — Atmospherics Lab

A small, self-contained WebGPU study of buoyant fire, smoke, and blast events. The interactive app is served from the repository root and has no build step or runtime dependencies.

The lab now includes a **high-resolution baked plume**. Choose **Read baked plume** in the source panel to load `assets/baked-plume.json` and its three-frame `96 × 144 × 96` RGBA32F volume. Baked mode only fetches and samples the cached frames; it does not dispatch the fluid solver or advance a live simulation. **Use live** returns to the editable 60 Hz solver. The active volume is drawn with a thick world-space cage: amber for the live source window and mint for the baked field.

## Run locally

Serve the repository over HTTP or HTTPS (WebGPU is unavailable from most `file://` URLs):

```sh
python3 -m http.server 8080 --bind 0.0.0.0
```

Open `http://localhost:8080` in a current browser with WebGPU enabled. In the Arena preview, serve this directory on `0.0.0.0`.

## Simulation notes

- The solver uses adaptive grid layouts: near is 80 × 64 × 80 cells at a base 0.4 m spacing, mid is 48 × 40 × 48 at 0.75 m, and far is 40 × 32 × 40 at 1.2 m. Their base fields are 32 × 25.6 × 32 m, 36 × 30 × 36 m, and 48 × 38.4 × 48 m, respectively. Near adds detail at 409,600 active cells; mid is 92,160 cells and far reduces the active grid to 51,200.
- The active emitters—not the camera—define the horizontal simulation window. Nearby sources share one grid; as their combined bounds or wind margin outgrow it, the solver recenters and increases cell spacing to cover them without adding cells or per-step work at that LOD. The tradeoff is lower spatial detail across a wider source spread. If bounds shrink but still fit, the old window lingers for six simulation seconds so residual smoke can fade before recentering; with no active emitters, the last window is held. **Clear field** resets to the tier's base spacing.
- On a tier, source-window, or cell-spacing change, a one-off GPU resample remaps velocity and gas in world space, preserving the overlapping volume. Gas beyond the new bounds is naturally clipped. The solver retains its fixed 1/60-second step at every tier; near costs more compute in exchange for finer smoke structure.
- Gas transport uses semi-Lagrangian advection with a MacCormack correction and neighborhood limiter, followed by vorticity confinement, buoyancy, divergence calculation, Jacobi pressure projection, and velocity projection.
- Fuel, temperature, smoke density, and soot fraction are evolved independently. The **Fuel feed** control scales source injection; **Reaction rate** governs fuel-to-heat conversion. Up to 20 emitters can run concurrently, and the **Start stress test** button clears the field and places 20 sustained plumes at different points.
- The solver allocates its largest grid buffers up front. A 250 MiB app-resource budget includes those buffers, a reserve, an estimate for three presentation surfaces, and two render targets; output resolution is reduced if needed to stay within the estimate. Driver/browser allocations are not exposed by WebGPU, so the telemetry reports a conservative estimate rather than total physical VRAM usage.
- While a source is active, a smooth source-relative wake envelope dissipates escaped gas outside its local footprint. This keeps long-running plumes from coating the full world volume; smoke can still drift and spread naturally inside the envelope.
- Distance LOD changes active grid dimensions, field coverage, ray-march samples, and render scale—not the fixed 60 Hz simulation step. Every tier presents at 60 Hz; the far tier keeps its smaller target and ray budget while reducing shadow taps independently.
- A smoothstep edge sponge absorbs gas gradually at the open sides and top. It spans about 5 m, or at least two cells when the widened grid is very coarse; its width maps to 2–12 cells and its damping is softer than the former three-cell layer. Rendering follows the volume-data path used by Unreal's Niagara Fluids: sub-cell primary ray steps, explicit density transfer gain/cutoff/curve controls, a Kelvin black-body temperature map for emission, and a separate density shadow ray. The properties panel exposes cool/hot fire gradient stops and light/dense smoke gradient stops; those controls shape the transferred volume while preserving temperature-driven fire response. Primary-ray transmittance provides view occlusion, and the separate density/thermal shadow ray plus local ambient occlusion darken the volume core. Far LOD keeps a smaller render target and ray budget for efficiency, but stays at 60 Hz with unquantized fragment rays; the sky, ground, and volume remain smooth rather than using censor/mosaic blocks or procedural world-space breakup noise.

Drag to orbit, use the wheel to zoom (up to 160 m), and choose **Place emitter on field** to set the next event location. **Extinguish sources** stops injection but leaves suspended smoke to drift; **Clear field** resets the volume.

## Baked volume

`assets/baked-plume.json` is the manifest for the read-only asset. Its volume bounds are `X −19.2…19.2 m`, `Y 0…57.6 m`, `Z −19.2…19.2 m`; those bounds are displayed in the active simulation readout and are independent from the live solver's source window. The `.bin` file is ordered as x-fastest cells with four 32-bit float channels: smoke density, temperature, fuel residue, and soot fraction. The file is authored offline and never generated in the browser.
