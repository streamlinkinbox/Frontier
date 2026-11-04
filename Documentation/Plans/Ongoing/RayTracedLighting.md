# Ray-traced reflections and sun shadows (RTX path) — report

Status: **report with tested options** (2026-11-02, branch `arena/88b72faa-frontier`). Every option is opt-in behind
`RefreshToggles::Refinements`; mask 0 keeps the 1:1 port (direct radiance bit-identical to the demo's shader).
Measurements and method: `Exhibits/RayTracedLighting/`.

## 1. Short answers

| ❓ Question | 💡 Answer |
|---|---|
| 🔥 Where do the reflection fireflies come from? | Two sources. ① **The sun, counted twice**: the direct term shades the sun's highlight toward the shadow ray's *jittered* direction with roughness floored at 0.03 (a GGX peak near 393,000), so smooth surfaces sparkle; and reflection rays add the solar disc at 2,500× solar radiance (clamped only at 200) on surfaces smoother than 0.25. Where the pane mirrors the sun, the demo's glint is 1.7× too large and both terms flicker. ② **The grain you see without the blur**: switching the variance filters off unfilters *both* signals; on the glass pane the diffuse ReSTIR term's grain rises 2.9× and the reflection's 2.1× |
| 🌫️ Why is every reflection blurred? | The reflection is traced at **half resolution** and upsampled ×2. Then the glossy variance filter runs **three 5×5 à-trous iterations** (reach ±8 half-resolution pixels, ±16 on screen). Their edge stops look only at the *receiving* surface's depth, normal and luminance, so a flat mirror is blurred like a rough floor. Its temporal step reprojects with the surface's motion, which smears reflected images when the camera moves |
| 🌗 Why do RTX shadow edges flicker while GTX shadows are clean? | RTX sends **one ray to a random point of the sun's disc per pixel and refresh**, a binary verdict that only the antialiasing (10 % blend, 3×3 clamp) averages. A penumbra a few pixels wide stays noisy. GTX's "SDF" shadows are in fact **three raster cascades with 3×3 PCF**: deterministic, so clean, but a fixed blur with no contact hardening, limited by shadow-map resolution |
| ✅ Which options are better? | 🌞 `SolarDisc` (free) fixes the sun: glint visible errors 572 → 56. 🪞 `MirrorFilter` (13 ms) gives sharp mirrors without the grain: −41 % error on the pane. 🌗 `PenumbraFilter` (34–37 ms, with 🎲 `StratifiedSun`) cleans ray-traced shadow edges: −25 to −34 % visible errors, −30 % flicker, contact hardening kept. 🔍 `MirrorTrace` is the sharpest but costs about 0.5 s on SwiftShader in the close-up; keep it optional |

## 2. What the two paths do today

| | 🟩 GTX (`RayTracing` off) | 🟦 RTX (`RayTracing` on) |
|---|---|---|
| Diffuse GI | distance field + surface cards | ReSTIR path tracing (`MaxBounces` 3) |
| Reflections | distance-field march, card radiance at the hit, roughness ≤ 0.50 | BVH trace, 2 bounces with sun/fire/HMI/light NEE at each hit, roughness ≤ 0.80 |
| Reflection resolution and filter | half resolution, glossy variance filter, ×2 upsample | same |
| Sun shadows | 3 raster cascades, 3×3 PCF | 1 BVH ray per pixel and refresh to a jittered point of the disc |
| Sun highlight | toward the disc's centre | toward the shadow ray's jittered direction |
| Firefly guard | — | GI path samples clamped at luminance 24 (`FireflyFilter`); reflections only at 200 |

## 3. Options tested

Measured against converged references (64 sun rays per pixel; 16 reflection samples per screen pixel). *Visible
errors*: pixels per refresh more than 12 luminance levels from the converged image. Costs are SwiftShader stage times
at 480×270, whose 7.5–9 s refreshes make ratios more meaningful than milliseconds.

| Option | What it changes | Measured effect | Cost | Verdict |
|---|---|---|---|---|
| 🌞 `SolarDisc` | The sun's highlight shaded as a disc toward its centre (representative point, energy-normalised); no disc in reflection rays | Sun glint: visible errors 572 → 56, glint size 100 → 58 px (converged: 58), direct-term flicker −60 %, specular-term flicker −43 % | none | ✅ adopt |
| 🎲 `StratifiedSun` | Shadow-ray sun samples stratified over 4×4 pixel blocks | No change alone (840 vs 766 visible errors); it is what lets the penumbra filter converge in one refresh | none | ✅ with the filter |
| 🌗 `PenumbraFilter` | Sun verdicts averaged over a disc a quarter of the local penumbra wide, plane and normal weights | Pole: 928 → 615 visible errors, error RMS 7.11 → 5.69; tower: 903 → 673, 8.21 → 6.61; flicker −30 % | 34–37 ms (0.4 % of a refresh) | ✅ adopt |
| 🪞 `MirrorFilter` | Surfaces smoother than 0.10 take the unfiltered reflection, fading to the filtered one by 0.20 | Pane: refresh error 2.70 → 1.58, visible errors 57 → 37, no added flicker; grain stays at the converged level | 13 ms | ✅ adopt |
| 🔍 `MirrorTrace` | With `MirrorFilter`: smooth surfaces traced per screen pixel instead of upsampled | Pane: 2.70 → 1.36, edges closest to converged | 561 ms in a view where the pane covers 13 % of the screen | ⚙️ optional |
| ⬜ variance filters off | The demo's own switch (what "without the blur" did) | Pane slightly sharper (2.51) but 37 % more flicker; grain doubles | saves both filters (46 ms of it glossy) | ❌ |
| 🟩 cascades (GTX) | Raster shadow maps instead of rays | Steadiest but furthest from the true shadow: 1,601 / 2,246 visible errors, edges displaced at distance | saves 150–270 ms | ❌ for RTX |

## 4. Recommendation

1. **Turn on 🌞 `SolarDisc`, 🎲 `StratifiedSun`, 🌗 `PenumbraFilter` and 🪞 `MirrorFilter` for the RTX path.** Together
   they add a 13 ms and a 35 ms dispatch on SwiftShader, two small screen-space dispatches on a GPU. They give a
   correct, compact sun glint, clean penumbrae with contact hardening, and mirror-sharp reflections on smooth surfaces.
   Rough surfaces keep the demo's filter, which their lobe needs.
2. **Keep 🔍 `MirrorTrace` as a quality option.** It is the sharpest, but its cost grows with the screen area of smooth
   surfaces: the vehicle's clear coat will make that large.
3. ✅ **Fixed (2026-11-03): the reflections no longer vanish with indirect GI off** (§5): `ReservoirTemporal`'s
   guide-only entry `ProjectResamplingGuide` writes the guide when the resampling does not run (`a051e6a`).

They are levelspace keys today, all off, so the 1:1 port stays the default until you decide (no controls, per your
scope). In the Render settings record of `Projects/Everest/Content/Levels/Default.levelspace`, add `solar_disc = true`,
`stratified_sun = true`, `penumbra_filter = true` and `mirror_filter = true` (and `mirror_trace = true` to try it).

## 5. Other findings

- The shadow cascades are rasterized on the RTX path too. They cannot be skipped: the environment's fog shafts sample
  them (`EnvironmentOverlay` `SceneShadowDepth`).
- The demo's `FireflyFilter` clamps only the indirect path samples (luminance 24) and the ReSTIR spatial weights.
  Reflection samples are clamped only at 200.
- The omitted vehicle's clear coat (roughness 0.03) is the worst case for both sun mechanisms. 🌞 `SolarDisc` matters
  most when the car returns.
- ReSTIR's path vertices shade the sun through the same next-event estimate as the reflection hits, so 🌞 `SolarDisc`
  also removes highlight spikes from indirect light.
- ✅ **Fixed 2026-11-03 (`a051e6a`): with indirect GI off, the RTX reflections vanished.** The glossy variance filter's
  temporal step, its wavelet and
  its screen upsample all take their validity guide from the ReSTIR temporal step (`ReservoirTemporal` writes
  `GuideViews`). Without indirect light that step does not run, the guide goes stale, and every half-resolution
  reflection texel is treated as invalid (zero). This is inherited from the demo's structure. A fix is to write the
  guide when indirect light is off (a guide-only entry of `ReservoirTemporal`), or to give the glossy filter a guide of
  its own. 🔍 `MirrorTrace` is unaffected because it writes the screen directly. The first fix is in: the entry runs only
  with indirect light off, so the default refresh is unchanged (its SPIR-V is byte-identical). Before and after:
  `Exhibits/SceneViewport/Images/ReflectionsIndirectOff.jpg`.
