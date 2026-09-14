# Sun & Sky parity review — Project Zero (C++) vs the celestial panel (HTML)

Date: 2026-09-14. Question: why does the sun/sky in
`https://sultanaladin.github.io/Frontier-/celestial/` still look significantly more
realistic than in Frontier C++ (Project Zero), and how do we close the gap?

## TL;DR

**The sun/sky mathematics in C++ is already the HTML mathematics.** The vendored
`CelestialIntegrator` is md5-identical to the reference port, and the port is a
faithful transcription of the fragment shader — verified function by function
below. The visible gap comes from **what is rendered around that math**:

1. **Framing** — the panel shows open sky full-frame; Project Zero shows a Cornell-box
   room with the sky visible only through an aperture (~a third of the pixels).
2. **The thin cloud slab is ON in the C++ beauty frames but hard-forced OFF in the
   HTML** (`f1('uCloudOn',0)` every frame). It is a beige veil over the whole sky —
   the single biggest reason the C++ sky reads overcast-beige instead of blue.
3. **Different sky state** — 07:36 / az−79° / coverage .52 / rain off (C++) vs the
   panel defaults 06:24 / az 0° / coverage .45 / rain 12 mm/h (HTML). Different sun
   (21.4° white vs 5.4° golden), denser clouds, no rainbow (needs rain).
4. **One genuine small bug**: the flare `InFrame` gate is missing the aspect
   correction (flare-only, roughly half brightness at the left/right frame edge).
5. Small items: checker moiré rings on the ground, missing `luna` moon texture
   (moons fall back to flat grey), low GI sample counts in the beauty run (noisy walls).

To look like the panel, Project Zero does **not** need new sky shaders. It needs a
sky-viewport render mode, a panel-defaults preset, the slab switched back off, and
the small fixes in §5. Proof: `Tools/SkyReference/` renders the exact GLSL sun+sky
path on the CPU — blue noon, golden dawn — from the same equations the C++ runs.

## 1. What was compared

| Side | Source |
|---|---|
| HTML truth | `sultanaladin/Frontier-` @ `arena/01a08c57-frontier`, `docs/celestial/index.html` (3657 lines; fragment shader = lines 767–1266) |
| C++ port | `sultanaladin/Frontier-` @ `arena/01a09644-frontier`, `Projects/Project-Zero/Source/*` |
| Vendored copy under review | `eosclient0001-rgb/Frontier` @ `arena/01a0a041-frontier` (`28f7f74`), `Projects/Project-Zero/*` |
| Oracle renders | `Tools/SkyReference/skyref.cpp` (this branch) — fresh CPU transcription of the GLSL sun+sky path only |

Vendored vs reference-port check (md5): `CelestialIntegrator.{cpp,h}`,
`CelestialSpecification.h`, `CelestialStage.{cpp,h}`, `RendererHost.{cpp,h}`,
`RayTracingSolver.cpp`, `FlyThroughSolver.cpp`, `PrecipitationSolver.cpp` —
**all identical**. So every finding below applies equally to the reference port
itself, except the `GameExecution.cpp` framing/state choices, which are the
vendoring session's own.

## 2. What is already identical (no work needed)

| System | GLSL | C++ | Verdict |
|---|---|---|---|
| Solar ephemeris | `sunDirAt()` (double, lat −26°) | `SolveSunDirection()` — same formula, panel-precision doubles | exact |
| Sun colour | `kelvinRGB()` (Tanner Helland) | `KelvinColour()` | exact |
| Atmosphere | `atmosphere()`: 20×8, quadratic spacing, βR=(5.8,13.5,33.1)e-6, βM=21e-6 ×1.1, βO=(0.65,1.881,0.085)e-6, HG phase | `IntegrateAtmosphere()` | exact |
| Twilight band | `dawnGlow()`: 5-colour log ramp, az envelopes, white line −5.5°…0°, earth-shadow dome | `TwilightGlow()` | exact |
| Sun disc | angular radius `sun_ang·D2R/2`, low-sun softening ×2.2, limb ×0.55, air-mass reddening `max(ext,trans²)`, boost `sun_disc` | `SampleSkyRadiance()` sun block | exact |
| Sun aureole | `e^(−ang·40)·.35 + e^(−ang·9)·.03 + e^(−ang·2.5)·.004`, ×bloom×intensity×.6 | same line | exact |
| Sky probe | 4×1 target, zenith/sun-side/anti-sun | `SolveFrame()` 3 probes, weights .4/.35/.25 | exact |
| Exposure/tonemap | `autoEV` ramp, EV, ACES/Reinhard/Filmic/AgX, vignette, γ 1/2.2, grain | `ResolveDisplay()` | exact, incl. order |
| Flare kernel | `lensFlare()` ghosts/halo/streak/burst | `LensFlare()` (prior session: 1e-7 over 468 samples) | exact |
| Ground/checker | analytic box-filtered checker, sun+hemispherical ambient, fog `(1−e^(−t·7e-5))^1.6` | `SampleGround()` | exact in formula (see §4.3 for the footprint approximation) |
| Rain | 2D screen-space overlay *after* tonemap | screen-space splats after composition | same design |

Two more things I checked and **cleared** (initially suspicious, actually fine):

- **Vignette.** The C++ omits the `1/aspect` factor, but `ExportPpmImage` passes
  ±1-space coordinates instead of the shader's ±aspect-space `uv`, which cancels
  exactly. Reference and C++ vignette agree to the pixel grid.
- **Exterior ground "blowout".** Measured, not eyeballed: near ground mean 0.54,
  horizon mean 0.56 (sRGB) — bright light-grey, not clipped. It is the analytic
  checker + fog-into-bright-sky, i.e. reference behaviour. GI is skipped for
  non-room pixels (`CelestialStage.cpp` phase 3), so there is no double-lighting.
- **Flare file labelling.** `Cinematic` sun-region mean 0.552 vs `Off` 0.518 —
  the flare-on file is the brighter one, so the labels are correct.

## 3. The differences that matter, ranked

### D1 — Framing: a room with a sky patch vs open sky (dominant)

The panel camera (yaw 35°, pitch −4°, height 2 m, FOV 72°) looks at open sky and
checker ground to the horizon — every pixel is atmosphere. The C++ beauty camera
stands outside the Cornell box at (0, −1.4, 1.2), pitched +16°, and the frame is
mostly red/green walls and grey boxes; the sky is a bright patch through the
window/ceiling aperture. Any sky algorithm looks less realistic reduced to a
backlight for a room interior. There is currently **no sky-only render path** in
Project Zero, so a like-for-like look-dev comparison cannot even be produced.

### D2 — The thin cloud slab is ON in C++ but forced OFF in HTML (biggest single state bug)

The live panel uploads `f1('uCloudOn',0)` unconditionally (like `uFogOn`, `uTerrOn`).
The C++ spec default matches (`CloudLayer.Visible=false`), but the beauty
`GameExecution.cpp` sets `SkyCriteria.CloudLayer.Visible=true` with coverage .46 /
density .62. `CloudSlab()` is a bright warm-grey veil
(`mix(shade,tint,·)×(trans·sun·.9+sky·.9)×.6` + silver-lining term) draped over the
whole sky — this is where the beige overcast cast comes from. At 07:36 the same
math without the slab gives blue sky + white sun (see oracle render below).

### D3 — The sky state is not the panel state

| Parameter | HTML default | C++ beauty run | Effect |
|---|---|---|---|
| `sun_time` | 6.4 h (elev **5.39°**) | 7.6 h (elev **21.44°**) | golden low sun → white high sun |
| `sun_az` | 0° | −79° (swung onto the window axis) | sun visible through aperture at all |
| vcloud coverage | .45 Cumulus | .52 | denser deck |
| thin cloud slab | **forced off** | **on** | beige veil (§D2) |
| precipitation | rain 12 mm/h | off | no streaks/splashes, and no rainbow (rainbow needs `uRBRain`) |
| local cloud | (40, 120, −160) · (90, 35, 70) | (120, 260, −420) · (180, 70, 140) | different placed cumulus |
| moons | `luna` texture | flat 0.62 grey (no texture vendored) | §D6 |

The azimuth swing was load-bearing for the flare demo (the gate correctly needs the
sun in frame), but it means the C++ frames were never attempting the panel's look.

### D4 — The sun disc is buried (consequence of D1–D3, not a disc bug)

Same disc math, but: the disc is ~5 px wide at 960×540/FOV72; in C++ it sits inside
a bright cloud blob seen through a small aperture, so the white-hot core + aureole
cannot read. Note the disc *always* clips to white at defaults (`sun_disc` 12 →
disc radiance ≈ 115/61/12 even at dawn); the "sun look" in the panel **is** the
aureole + Mie forward lobe + twilight band *around* the clipped core — all of which
the clouds + aperture hide in the C++ frames. The oracle dawn render
(`htmlsky_0640_sun.png`) shows the same equations giving a glowing golden sun.

### D5 — Genuine bug: flare `InFrame` gate misses the aspect correction (flare-only)

- GLSL: `inFrame = 1−smoothstep(1.25, 2.2, length(sunUV·vec2(1/aspect,1)))`
- C++ (`AddLensFlare`): `1−smoothstep(1.25, 2.2, length(sunUV))` — no correction.

At 16:9 with the sun at the left/right frame edge, C++ yields InFrame ≈ 0.41 where
the reference yields 1.0 — the flare renders at roughly **40% strength** there.
(Center-frame, where the demo sun sits, is unaffected — which is why the 1e-7
kernel check didn't catch it: the kernel is exact, this is the call-site gate.)

### D6 — Small items

- **Checker moiré rings** on the exterior ground at grazing angles (concentric
  ripples, visible in both C++ render sets): the CPU pixel-footprint approximation
  of the `fwidth` box filter breaks down there. Ground-only, cosmetic.
- **Moons are flat grey**: the reference `GameExecution` loads
  `EngineContent/CelestialTextures/luna_1k.ppm`; the vendored copy ships no
  textures and `MoonAlbedoSurface::Sample` falls back to 0.62 grey. Cheap fix.
- **Noisy room, clean sky**: the beauty run uses 4 indirect rays / 1 spatial pass /
  12 sky taps vs the reference proof's 24/3/48. Sky pixels are single-tap analytic
  (clean either way), but the walls carry visible ReSTIR noise that drags down the
  whole frame's perceived realism.
- **Documented upstream departures** (keep, just know them): GI bounces see no
  point stars; local fog/cloud are placed for room scale, not the panel's 600 m
  plane defaults.

## 4. Oracle evidence

`Tools/SkyReference/skyref.cpp` is an independent, minimal CPU transcription of the
GLSL **sun+sky path only** (atmosphere, `dawnGlow`, disc + aureole, planet-ground
shade, autoEV, ACES, vignette, γ, grain — no stars/moons/clouds/fog/flare),
compiled with `g++ -O2` (≈2 s for all frames). Panels defaults throughout.

| File | Content |
|---|---|
| `htmlsky_0640_sun.png` | 06:24, aimed at the sun (elev 5.39°): golden disc + orange horizon band + blue-grey dome — the panel's default sun |
| `htmlsky_0760_sun.png` | 07:36, aimed at the sun (elev 21.44°): white sun, blue sky, broad Mie aureole — what §D2's slab is hiding |
| `htmlsky_0640_lookup.png` / `htmlsky_1200_lookup.png` | fixed camera (yaw 35°, pitch +18°) at dawn and noon |

Caveat: below-horizon pixels use the simplified dirt-albedo planet branch, not the
panel's checkered plane + fog, so only the **sky half** is reference-exact. That is
the half under review.

## 5. Improvement plan

**P0 — produce the panel look from the existing math (no shader work)**

1. Add a sky-viewport mode to Project Zero: full-frame `SampleSkyRadiance` +
   `SampleGround` + glyphs + rainbow + flare + `ResolveDisplay` at the panel's
   camera defaults, no room. This one change should reproduce the HTML sun/sky,
   since the equations are already identical.
2. Add a `PanelDefaults` criteria preset (sun 6.4, az 0, coverage .45, rain 12,
   slab/fog/terrain OFF, observer height 2) and use it for parity renders; keep
   artistic overrides (azimuth swing, slab on, precip off) as explicit named
   deviations, not the default.
3. Add a parity gate: render the sky viewport at 4 panel times (06:20 / 12:00 /
   17:50 / 23:30, mirroring `CelestialSceneProof`'s moments) and diff against the
   `Tools/SkyReference` oracle within tolerance (clouds/stars/moons excluded at
   first, added once their oracles exist).

**P1 — small fixes**

4. Fix the `InFrame` aspect bug in `AddLensFlare` (one-line; mirror the GLSL).
5. Vendor `luna_1k.ppm` + `AssignMoonSurface(0, …)` in `GameExecution` (as the
   reference does).
6. Raise beauty-run samples toward the reference proof (24/3/48) or gate wall
   noise in the checker.
7. Clamp/repair the checker footprint at grazing angles (or document as known).

**P2 — look-dev workflow**

8. Time-sweep harness (dawn→night) with ΔE report vs the oracle, run in CI.
9. Optional: expose `sun_disc`/`sun_ang` in the beauty runner — the panel's sun
   character control — rather than hard-coding.

## 6. Repo note

The work under review lives on `eosclient0001-rgb/Frontier` branch
`arena/01a0a041-frontier` (`28f7f74`); this session is on `streamlinkinbox/Frontier`
branch `arena/01a0a07b-frontier`, which currently holds only this review plus the
oracle. Implementing §5 means bringing the Project Zero tree onto this branch
(same vendor + entry-point pattern as before) and then landing P0–P1 on top.

## Addendum 2026-09-14 — implemented in `.slang`, lit strictly by ReSTIR

The build specified above is done (`Projects/Project-Zero/`):

- `Shaders/CelestialCore.slang` — panel-exact sun + atmosphere (verified
  against the freshly re-rendered oracle: max 1 LSB full-frame on all 6
  cases, ground and sun disc included).
- `Shaders/CelestialReSTIR.slang` — the only lighting there is: DI + GI
  reservoirs, temporal + spatial reuse, T2/T3/T4 green against independent
  brute-force references on an analytic corner scene (48 frames × 12 runs).
- Second truth: the upstream C++ transcription vendored under
  `Host/CpuPort/` agrees with the core at max 1 LSB on all above-limb rays;
  its beauty path omits the planet branch (documented in
  `Host/CpuPort/PROVENANCE.md`), so the ground is gated by the oracle.
- Gates: `Diagnostics/RunCelestialParity.py` (green end-to-end, exit 0),
  `Diagnostics/CheckCelestialSlang.py` (dual-compile contract),
  `Diagnostics/Proof/` (proof renders). `CelestialParity.txt` holds the last
  report. Vulkan integration guide: `Shaders/README.md`.
