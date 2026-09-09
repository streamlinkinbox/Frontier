# Sun / Sky / Atmosphere REDO — decoupled from ReSTIR

Companion to `skydemo.html` (same file as Slate `Demo/SkyDemo/index.html` on branch
`arena/01a07be4-slate`, which this plan was built against — HEAD `905f0e9`).

Open the demo with any static server, e.g. `python3 -m http.server 8080` in this directory,
then visit `/skydemo.html`. It opens at 06:10 facing the sunrise.

---

## 1. What was wrong (diagnosis on `arena/01a07be4-slate`)

The old sky was sampled *inside* the path tracer, so camera- and resolution-dependent
quantities leaked into the light itself. Five mechanisms, all in the shipped code:

1. **Binary sun/moon discs.** `SunDiscTabulated` and the moon block in
   `Engine/Shaders/ReSTIRViewport.slang` are per-pixel in/out tests (`dot > cos(radius)`)
   with radiance = illuminance ÷ solidAngle and no pixel-footprint coverage. How many
   pixels catch a 0.53° disc — and with what partial coverage — changes with frame size
   and with where the disc lands on the pixel grid. The sun's total frame energy therefore
   moved with resolution and with the camera. (This is the "something about pixel
   resolutions" from the report.)
2. **Per-pixel sky march on the miss path.** Fixed `ViewSteps` with step length =
   Distance ÷ Steps, where Distance runs ~60 km at zenith to ~1000+ km at the horizon.
   Integration error is direction- and altitude-dependent, so horizon brightness shifted
   as the camera moved; rays near the horizon flip between `HitsGround` and sky — a
   discontinuity that shimmers with the camera.
3. **A second, noisier estimator for bounces.** A5 gathers skylight with the same march
   at *half* steps, so direct misses and bounced skylight disagreed, and both depended
   on the quality tier.
4. **Sun as a reservoir slot.** A4 put the sun at index `LightTriangleCount` with a fixed
   50% pick share, so sun samples ride temporal/spatial reuse and its normal/depth
   validation. Reuse success depends on what is on screen, so the effective sun
   contribution at any finite frame count moves with the camera. Accumulation also
   restarts on any camera motion, so a moving camera never sees the converged value.
5. **No sky-view surface** (`References/Deferred/SkyViewSurface.md`). Horizon detail had
   nowhere to live except whatever the frame happened to resolve — resolution-dependent
   by definition.

Removing the sky "fixed" the symptom by deleting the patient. The redo keeps the sky and
removes the *coupling* instead.

## 2. The redo — three rules

- **R1. ReSTIR reservoirs carry emissive TRIANGLES ONLY.** The alias table, `PickLight`,
  temporal/spatial reuse, and the R6 identity proofs are untouched — the sun slot is
  deleted, not reworked.
- **R2. The sky has ONE resolution: surface 3.** A per-frame sky-view LUT (256×128,
  quadratic-angle horizon mapping, Hillaire 2020 §5.3). The miss path becomes a LUT
  lookup. The dawn line is baked into it.
- **R3. Lights are per-frame CONSTANTS.** Sun colour = table 1 along the sun direction;
  sky light = the top mip of surface 3 (the *probe*, one RGB triple). Both feed direct
  lighting analytically, outside any reservoir.

## 3. What each old phase becomes

| Old | Fate in the redo |
|---|---|
| A1 clock (`CelestialSolver`, `SkyRecord`) | KEEP as-is |
| A2 LUTs (Bruneton ①, full-range ②) | KEEP; add the ③ builder (per frame, per observer world) |
| A3 miss-path march | REPLACE with surface-3 lookup + footprint-AA analytic discs + stars |
| A4 sun-as-emitter | DELETE the reservoir slot; sun NEE becomes analytic (shadow ray + fixed penumbra taps over the 0.268° cap), added directly |
| A5 bounce march | DELETE; escaped bounces sample the probe (diffuse: albedo × probe) or surface 3 (specular) |
| A6 aerial froxels | Design stands; single-blend approximation until then |
| A7 night (stars, moon) | KEEP, moved onto the LUT path with AA edges; moon is the analytic night light (~0.25 lx × phase × transmittance) |
| Incident-anchor exposure | KEEP; source it from the probe, so exposure and scene lighting can never disagree |

## 4. The dawn line (new — the whitish band other implementations lack)

Baked into surface 3 in the LUT builder (so its fidelity comes from the LUT, never the frame).
What it stands in for: forward-scattered Mie glow plus the lit air below the observer's
horizon, which the march under-resolves exactly where the path is longest.

- `gate = (1 − smoothstep(0, 0.21 rad, |sunElev + 0.035|))²` — sun crossing the horizon, gone by day and deep night
- `band = exp(−((lat° − 0.7)/1.6)²)` — ~1.6° wide, centred just above the geometric horizon
- `azi = 0.25 + 0.75·((cos(Δaz)/2 + 1/2))²` — brightest toward the sun, present all around
- `L = mix(L, paperwhite·(lum·2.1), clamp(dl,0,0.9)) + paperwhite·(dl·lum·0.6)`, `dl = band·gate·azi·uDawn`

Relative to the local sky luminance, so it scales with the exposure instead of fighting
it. Strength uniform `uDawn` (0–2, default 1). Colour paperwhite (1.0, 0.94, 0.84).

## 5. How the demo proves it (the executable spec)

- **Surface-3 inset** (top left) — E1/R2: the entire sky at 256×128, the only sky the
  engine will draw or light from.
- **Probe** — E3/E4/R3: the top mip of surface 3; diffuse sky light and exposure anchor.
- **Lighting stub** (bottom left) — E2+E3: a sphere on a plane lit the way ReSTIR will
  be — analytic sun + probe, no reservoirs, no noise, nothing to converge.
- **Pixel-resolution slider** (25–100% render scale) + **zoom** + drag look — the proof:
  the probe and screen meters must not move. The old failure mode is structurally
  impossible because no camera- or resolution-dependent quantity feeds any light.
- **Frame-metered mode** is kept so the old exposure pumping is visible, not asserted.

## 6. Integration ladder (engine, not started)

1. **E1** — Surface-3 builder + miss-path lookup. Proof: resolution sweep (25/50/100%) → miss pixels identical.
2. **E2** — Sun out of the reservoir (analytic NEE). Proof: R6 proofs green; camera sweep → sun brightness fixed.
3. **E3** — Probe lighting for escaped bounces; delete the bounce march. Proof: shadowed-wall colour stable vs camera.
4. **E4** — Exposure sourced from the probe. Proof: look-around → exposure moves <1%.
5. **E5** — Dawn-line port into the LUT builder. Proof: eyeball at 06:10, both horizons.
6. **E6** — Full suite + A/B against pre-removal images for light LEVEL (not noise).

## 7. Verification of the demo (this session)

- JS parses (`node --check`); all 9 GLSL translation units parse (`@shaderfrog/glsl-parser`).
- Every uniform set is declared and vice versa; `vUV` varyings match their vertex shaders.
- Every identifier resolves in every translation unit (custom check, negative-tested).
- No reversed-`smoothstep` (undefined behaviour the old demo had twice), no negative
  `pow` base, no legacy GLSL tokens.
- NOT done: machine-checked rendering (no headless GL in the sandbox — the browser and
  the drive-by CDN are both unreachable). The demo needs one eyeball pass in a real
  browser; the meters + insets make failures obvious.
