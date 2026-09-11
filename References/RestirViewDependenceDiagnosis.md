# RESTIR view-dependence + missing-sky diagnosis — Project-Zero

Date: 2026-09-11. Branch: `arena/01a08d16-frontier` (at `3efcb10`).
Method: code-read of the exact GPU path (`ReSTIRViewport.slang` + host frame loop).
Not executed here — this sandbox has no GPU/Vulkan runtime. Every claim below cites its file and line.

Your four complaints, in short: **two are working as designed (but the design contradicts what you
asked for), one is a design contradiction between host and shader, and the missing sun/clouds are
structurally absent — there is no code that could draw them on this path.**

---

## 1. "Turning around changes the RESTIR" / "going in/out of the box changes the scene"

**Cause: the host throws away all history on ANY camera motion, every frame.**

`ReSTIRIntegrator::ObserveCamera` (`Engine/DisplayPresentation/ReSTIRIntegrator.cpp:31`) resets
`AccumulationIndex` to 0 whenever the camera moved (>1e-5 m), turned (>1e-6 forward delta), or the
viewport resized — and `GameExecution.cpp:1156` calls it every frame. The shader then runs with
`FrameIndex == 0`, which disables, by explicit gates:

- temporal reservoir reuse (`ReSTIRViewport.slang:926`),
- spatial neighbour reuse (`:973`),
- running-mean reprojection (`:704`, `:731` — falls through to a count-1 restart).

So while the camera moves or turns, **every frame is a fresh 1-sample-per-pixel render with cold
reservoirs and maximum denoiser variance** — qualitatively a different image from the converged
still frame. Stopping the camera lets it converge again, which reads as "turning changes the
RESTIR" and "moving in/out changes the scene".

**Design contradiction worth knowing:** the R7a comment block (`ReSTIRViewport.slang:676-684`)
says motion-vector reprojection exists so that *"a pan keeps its samples"* — but the host reset
above makes that unreachable for camera motion. Reprojection currently only ever helps a static
camera with moving *objects*. The shader was written for a host that trusts reprojection; the
host does not.

**Verdict:** by design, but the design defeats its own reprojection machinery — and your stated
want (view-stable results).

---

## 2. "The box goes overly bright when far, proper when near"

**Cause: the median-anchored auto-exposure keys to the background at distance and excludes the
box as an outlier.**

Exposure defaults to Adaptive (`ExposureIntegrator.h:42`), is measured from linear HDR every
frame (`GameExecution.cpp:1151-1153`), and reaches the tone map as the `Exposure` push constant
(`ReSTIRIntegrator.cpp:90`). The meter (`SwapchainExchange.cpp:1301-1327`) anchors to the frame
**median** and averages only bins within ±`kLuminanceMedianStops` of it.

- **Far:** the box covers few pixels. The median lands on the near-black background; the box
  sits ~11 stops above the anchor and is discarded as an outlier — "a light source in shot",
  per the comment. Exposure keys to ~black → the box blows out.
- **Near:** the box fills the frame. The median lands on the box itself → correct exposure.

Same mechanism, second contributor: while moving you are at 1spp (§1), so fireflies + a fully
permissive à-trous input add haze that reads as extra brightness.

**Verdict:** the meter is doing exactly what it was built to do (its comments describe this
outlier rejection); it is the wrong metering for a small-bright-subject-on-black framing.
Not a bug in the light transport — the reservoir math is self-consistent
(`PHatFull` includes 1/d² at `ReSTIRViewport.slang:596-605`, the shade divides by d² once at
`:1047`, and `W = Σw/(M·p̂)` cancels correctly; M-clamp is standard).

---

## 3. "Atmosphere goes white at the wrong time, or black, when I turn or move away"

**Cause: the adaptation lag chasing the new framing — 0.4 s white, 2.2 s black.**

`BrightenSeconds = 0.40`, `DarkenSeconds = 2.20` (`ExposureIntegrator.h`). Turn from a dark wall
to bright sky/outside: the exposure is still keyed to the dark frame for ~half a second →
**white flash**. Turn back (or move away so the frame goes dark): the exposure stays low for
~two seconds → **black hang**. The sky pixels themselves are computed correctly per direction —
each moving frame restarts them at 1spp (§1), so there is no stale-sky smear; what you see
swinging is the tone-map scalar, plus 1spp noise.

**Verdict:** by design (deliberately asymmetric, human-vision-motivated) — but it makes every
camera move a 2-second brightness event.

---

## 4. "I don't see atmosphere / sun / sky / clouds on the RESTIR path"

Four separate answers:

**Sky (the air itself): EXISTS, but is nearly impossible to see from inside the box.**
`SkyAlong` runs on primary miss (`ReSTIRViewport.slang:813`) and escaped bounce (`:1081`), and
the record is pushed every frame (`GameExecution.cpp:1367`). But the Cornell box is sealed, so
indoors there are (almost) no misses and no escapes — the only environment term indoors is the
flat `MoonAmbient()` fill (`:1144`). Sky additionally requires: outliner `CELESTIAL` row visible
(`GameExecution.cpp:1077` drives `Enabled`; disabled packs `SunRadiance.w = 0` and `SkyAlong`
returns pure black — `SkyRecords.slang:227`), and it is default day (15.5 h,
`CelestialSequence.cpp:263`), so the sun is up unless moved.

**Sun: STRUCTURALLY ABSENT — no sun-disc term exists on the GPU path.** `SkyAlong` =
single-scattering integral + twilight glow + moons. There is no disc, no limb darkening, no
"staring at the sun" pixel anywhere in `Engine/Shaders/` (only hit for "disc" is the moon
limb in `MoonRecords.slang:84`). The metering comments discuss a sun in frame; it cannot be —
nothing draws it. (Hiding the SUN outliner row additionally zeroes the whole daylight integral,
leaving only twilight + moons: `CelestialSequence.cpp:403-404` → `PackSkyConstants` gain rule.)

**Clouds: STRUCTURALLY ABSENT.** The only "cloud" on the GPU path is the comment reserving
bindings 23-24 *"if they are ever built"* (`ReSTIRViewport.slang:121-124`). `VolumetricMedia` /
`Precipitation` feed the CPU `VisibilityRaster` and the headless proofs only — no consumer in
the kernel, no descriptor, no upload.

**Stars (bonus, same class):** the catalogue feeds the CPU raster (`CelestialSequence.cpp:370+`
→ `Raster.AssignCelestial`); the kernel has no star term.

**Verdict:** sky = present-but-gated-and-miss-only; sun/clouds/stars = do not exist on this
path. No setting turns them on.

---

## 5. Check-first list (on your PC, before concluding anything is broken)

1. Outliner: `CELESTIAL` folder visible? (`Enabled` — hidden = black sky, no moons.)
2. Outliner: `SUN` row visible? (Hidden = black daylight integral, twilight/moons only.)
3. Exposure mode: switch Adaptive → **Manual** (the slider is the identity switch,
   `ExposureIntegrator.h`) — if the far/near and white/black swings vanish, §2/§3 are
   confirmed as the sole mechanisms.
4. Time of day: still 15.5 h? Near/below-horizon sun = single scattering goes black by
   physics and only twilight remains (`SkyRecords.slang` twilight note).
5. Standpoint: sky is miss-only — from *inside* the sealed box you should expect to see no
   sky at all. Step outside (or open the box) to judge the atmosphere.

## 6. Fix directions

DONE marks what the follow-up commit implements; the rest stays scoped, not started.

- DONE — **F2-default (Manual exposure in the engine build).** `GameExecution.cpp` now seats
  `ExposureMode::Manual` (slider value 1.05) right after constructing the integrator. The frame
  is a pure function of scene + camera: no median metering (§2 gone), no 0.4/2.2 s lags (§3
  gone). The struct default stays Adaptive so the proofs are untouched, and the F3 Exposure
  slider now visibly works (in Adaptive it wrote a value nothing read). Adaptive remains
  available — one assignment flips it back.
- DONE — **F3 (sun disc).** `SkyRecords.slang:SkyAlong` gains the panel-transcribed analytic
  disc (0.53° diameter, 0.25 softness, 12× disc radiance, limb-darkened, horizon-gated,
  reddened by the integral's own transmittance). A hidden sun still kills it via the zeroed
  radiance. The panel's extra analytic sun-glow was deliberately not transcribed (the Mie lobe
  already provides it).

- **F1 — trust reprojection:** stop resetting accumulation on camera motion (reset only on
  cut/teleport/resize/scene change). This alone makes turning stable and revives R7a +
  temporal/spatial reuse while moving. Risk: motion-vector quality becomes load-bearing —
  validate it under rotation before committing.
- **F2 — framing-independent metering:** for look-dev, default the Cornell scene to Manual
  exposure (exists today); longer-term, a spot/ROI meter or a floor on how far the anchor
  may sit below the subject. Fixes §2 and §3 without touching transport.
- **F3 — sun disc:** an analytic disc + limb darkening inside `SkyAlong` (~15 lines, needs
  angular radius + limb coefficients in the sky block). Fixes "no sun".
- **F4 — clouds/stars on GPU:** the real project — needs the reserved bindings, a model
  (layer + coverage, or the CPU media ported), and proofs. Nothing smaller puts a cloud
  on the RESTIR path.
- **Do not touch:** the RIS/reservoir algebra (§2 verdict) — it is sound; the variance you
  see is accumulation restarts + exposure, not transport bias.

---

## 7. Follow-up findings (same commit — from the user's screenshots)

- **Moon "not proper": tuning + exposure, not a moon bug.** The disc path verifies end to end
  (degrees→radians + diameter→radius at `CelestialSequence.cpp:185`, reference-phase pack,
  textured phase-lit disc in `MoonRecords.slang`). Screenshot values (Glow 2.07, Size 1.60,
  Phase 0.49) are inspector edits, not defaults (panel-parity defaults: Glow 0.8, Size 0.9 —
  `CelestialSequence.h:97-100`); 0.49 engine ≈ full moon, correctly bright. The giant soft blob
  is the 2.6×-overtuned glow under adaptive exposure. Fix: Manual exposure (done above) +
  return Glow toward 0.8. No moon code changed.
- **Viewport placeholder ("renders here in the engine build"): current behaviour, unwired,**
  predates this branch's work. `AssignViewTexture` has zero engine callers
  (`ViewportPanel.cpp:457`; only the headless proof seats an image). Seating the Vulkan target
  needs sampled-view + descriptor lifecycle work that cannot be validated without a GPU — scoped,
  not attempted blind.
- **GI toggle: works as labelled.** The tile flips `kFeatureGlobalIllumination`
  (`GameExecution.cpp:603`), which skips the bounce only; the UI itself promises "Direct only"
  (`ControlCentreHost.cpp:1545`). No raster-presentation path exists (`DebugViewCategory` holds
  buffer visualisations, not a shaded raster) — a GI-off raster fallback is a feature request.
- **Stale vs negligence: neither.** The screenshots show the restyled outliner (this branch's
  work, built in), and the placeholder string is in current source (`ViewportPanel.cpp:973`).

## §8 — Celestial sliders landed only when the camera moved (fixed)

**Symptom (your words): slider changes update only after a camera move.** Mechanism,
verified line by line, no guessing: the inspector write-back runs every tick
(`Celestial.ApplySheet`, `GameExecution.cpp:1083`), the state re-solves and re-packs every
tick (`PackSkyRecord`/`PackMoonRecord`), and the upload is a bare `memcpy` with no reset
(`SwapchainExchange.cpp:2356-2379`). The kernel renders the new sky — but the running mean
absorbs it at 1/n on a converged frame, so it is invisible until `ObserveCamera` restarts the
history. The Control Centre path never had this bug (every `Integrator.Assign*` self-resets,
`ReSTIRIntegrator.h:84-87`); the scene-editor path was simply missing the same edge.

**Fix (this commit):** the packed bytes are compared after each push and a change restarts the
accumulation the tick it lands (`GameExecution.cpp:600-601,1389-1407`) — sliders, presets,
visibility toggles and the animating sun all show at once. Cost is 416 bytes of `memcmp` per
frame, silent while the scene idles. While the sun animates the history restarts every tick:
noisy while moving, exactly like the camera, instead of a smeared trail. Non-celestial sheet
sliders (materials on static rows) never re-upload at all — a separate gap, noted, not touched.

**Reference-branch check:** `arena/01a08682-frontier` @ `6c8b1a8` was fetched and searched. It
carries no GPU sun disc (no limb/0.53/disc-boost anywhere in `.comp` or integrators) and its
tree is renamed end to end (`.comp` shaders, `PhotometricIllumination/*Integrator.*`), so a
file-level port is invalid — the panel disc already transcribed (§6) *is* the reference's
sun model. Its engine-execution/module layout likewise has no celestial-record edge to copy;
the compare-and-reset above follows this tree's own `Assign*` pattern instead.

## §9 — The moon was broken three ways at once (all fixed); lens flare is model-only (scoped)

**Moon cause 1 — the solver mirrored the moon east↔west (real bug, fixed).** The moon's azimuth
quadrant rule was inverted relative to the sun's (`CelestialSolver.cpp:184` vs `:145`, the only two
such rules in the tree). Measured with the repo's own solver: on Sept 10 15:30 the new moon stood
**108° from the sun instead of 6°** — elongation says it must sit with the sun, so this was provably
wrong, not approximately wrong. Fix is a one-line branch swap; post-fix the new moon sits 6.3° off
the sun and the full moon opposes it at 176.5°, both now pinned as regression checks in
`CelestialSolverProof.cpp` (they fail on the old code). Why it survived: the proof checked ranges
and unit length only — a unit vector in the wrong quarter of the sky passed everything.

**Moon cause 2 — the default date was new-moon day (fixed).** Sept 10 2026: Luna 0.3% lit, a sliver
lost in daylight and below the horizon at night — the default sky could never show a moon. Default
is now Sept 19 (first quarter: half-lit, elevation +51° in the default afternoon, up all evening).
The ephemeris itself is untouched; only the picked date changed. Proofs set their own dates and are
unaffected.

**Moon cause 3 — Luna was 70% oversized next to the real-size sun (fixed).** Atlas default 0.9° vs
the real 0.52° — glaring beside the 0.53° sun disc. Now 0.52, per the reference panel's own hint
('real moon ≈ 0.5°'); the fantasy moons keep their stylised sizes. `MoonPackProof`'s size assert is
now symbolic off the atlas instead of a 0.9 literal.

**This turn's validation, no longer blind:** the sandbox fetched glslang, so the sun disc now
compiles to real SPIR-V (`CheckSkyKernel` green, "all 23 bindings match"); the parity proof gained
disc-formula pins (full centre, zero off-disc, 0.35 horizon survival, dead on hidden sun); and with
vendored headers fetched, all three gates pass with compiled proofs enabled — Sky, CelestialSky
(solver + renders), MoonKernel (pack + render + SPIR-V moon row), zero failures.

**Lens flare — your "non-existent" is exactly right, with one cruel detail:** the model exists
(`AtmosphericOptics::LensFlare`, four lens mixes), the CPU proof exists, and the panel round-trips
fully (Type/Intensity/Ghosts/Halo/Blades build AND apply back to the struct) — but nothing consumes
it in the engine. No record, no binding, no shader code. The panel knobs are live controls over a
disconnected effect. Wiring it up means: a small flare record at binding 23 (the gates currently
forbid declaring 23/24, so they move with it), a slang transcription of the model, per-frame SunUv
projected on the CPU, and a SunVisibility occlusion feed (one ray per frame — CPU traversal or a GPU
probe; the model's own spec demands occlusion, no flare through walls). That is a ~200-line blind
change across five files plus descriptor layout — specified, not started. Say the word and it gets
built; it will need your GPU to validate.

**Found but untouched:** the GPU kernel draws no stars at all (catalogue + star loop are CPU-raster
only) — the live night sky is atmosphere + moon. Same class of CPU/GPU gap as the flare.

## §10 — Stars, flare and rainbow now run on the GPU (wired end to end); kernel sun-direct stays absent by design

**What was built:** the three effects §4/§9 found CPU-only now render on the RESTIR path through
one seam. `PostConstantRecord.h` (128 B, 8 offset asserts) mirrors a std140 block at binding 24;
the star catalogue rides a std430 SSBO at binding 23
(`StarCells[1024]` + unsized `StarStars[]`, 32 B records); `PostRecords.slang` transcribes the
three models (star octahedral-cell walk, flare ghosts/halo/streaks, Descartes bow with baked
angles); the kernel hooks them in four places (stars into `SkyAlong` pre-twilight, bow on miss
and on shaded hit with the hit's own column, flare onto the linear mean in the resolve);
`SwapchainExchange` brings both buffers before the descriptor set, writes them, and retires
without unbinding; `GameExecution` uploads the tables once the catalogue loads and packs +
pushes the record every frame — including the single CPU sun-occlusion ray the flare's spec
demands (`Traversal.TraceClosest`, eye→sun, per frame). A second pack lives nowhere: pack,
push and record-compare sit beside the sky/moon frame logic already there.

**The full panel→record→binding→shader→frame chain, all six effects, verified (not believed):**

| Effect | Panel | Record | Binding | Shader | Frame |
|---|---|---|---|---|---|
| Sky | Celestial sheet (§8) | `PackSkyRecord` | sky block | `SkyAlong`, miss + escape | pushed + compared per frame |
| Sun disc | Sun row (ang/soft/boost) | sky block gain rule | sky block | disc in `SkyAlong` (§6) | SPIR-V + parity pins |
| Moons | Atlas rows, 4 at once | `PackMoonRecord` | moon block + atlas | `MoonRecords` textured disc | MoonKernel gate, 67 PASS |
| Stars | Size/Brightness + clock | post row 0 (LST/lat/size/bright) | 24 + tables at 23 | `StarAlong` in `SkyAlong` | upload-once, pack per frame |
| Flare | Type/Intensity/Ghosts/Halo/Blades | post rows 1-2 + FlareUv | 24 | `FlareAlong` in resolve | per-frame SunUv + occlusion ray |
| Rainbow | Intensity/Width/Secondary/Rain | post rows 3-4 + rain vis | 24 | `RainbowAlong`, miss + hit | per-frame precip mapping |

Every link has a gate: `CheckPostKernel` (pack proof, 28-angle Descartes match, shared 0.09
ceiling, hook presence, no push-constant reach, mirror offsets, layout/pool/write/retire pins,
single-caller pins), `CheckSkyKernel` (25-binding SPIR-V table incl. 23/24), `CheckShaderCompile`
(all 7 shaders lower, viewport at 65 511 words), plus full `GameExecution`/`SwapchainExchange`
syntax closure. Where a transcription could fork (bow angles, star ceiling), the gate diffs the
shader literal against the model every run instead of trusting it.

**Found scope, documented, NOT redesigned — the kernel sun gives no direct light or shadows.**
The kernel's direct-light set is emissive mesh triangles only (`Luminaires[]`,
`LightTriangleCount`, `PickLight` over the luminaire buffer, `SampleLightPoint` +
`TraceShadow` occlusion — `ReSTIRViewport.slang:338,546-585`). The sun is not a luminaire:
`SkySunDirection` feeds the sky integral, the disc and the bow, and nothing else. Sunlight
reaches surfaces only through the sky integral on miss/escape rays plus the flat `MoonAmbient`
indoor fill (`:1154`) — GI-skylight, no sun NEE, no sun shadows. The shadow machinery exists
(`TraceShadow`, any-hit + cut-out walk) but has no sun target to aim at. Promoting the sun into
the light set is a transport change (luminaire slot, NEE target, MIS weight against the mesh
lights), not a wiring fix — out of this turn's scope by decision, recorded here so it stops
being mistaken for a bug.

## §11 — The sun now lights and shadows the kernel (direct NEE); three estimator biases found and fixed

**What was built:** the sun is a first-class light in the SAME RIS reservoir as the mesh lamps — not a side
estimator — so temporal/spatial reuse, M-clamp and W apply to sunlight unchanged. Each candidate flips a pinned
0.5 coin (sun-only and lamps-only scenes skip it); sun samples draw a uniform direction in the disc cone
(0.53° shared body with the drawn disc, pdf 1/Ω in half-angle form), mesh samples draw as before; both answer
w = p̂/p exactly and shadow through the same `TraceShadow`. The reservoir stores a sun win as a far point
(10 km) under a sentinel light index the host can never read (it only sizes the buffer). Both DI gates run
when EITHER light kind exists — a lamp-less outdoor scene used to skip direct light entirely. No feature flag:
the sun switch is the record's direct row, so night, a hidden sun and a disabled sky take the lamps-only path,
and the outliner SUN row is the kill switch.

**The direct scale is the reference panel's, not an invention.** The panel lights its previews with
`trans·colour·intensity·0.11·ndl·sh` (`CelestialPanel.html:1162,1182`); the packer packs exactly that factor
(transmittance marched by the same `Integrate` the raster calls, zero below the horizon) and the kernel divides
by Ω and multiplies back in the estimator — so the converged NEE EQUALS the panel's formula while sampling the
real disc (soft shadows) and shadowing through the BVH (the panel's `sh`). Proven by value, not by reading: the
new Monte Carlo proof converges to `f·Q·cos` with Q = 0.11·22·0.8. One deliberate difference: the panel fades
direct sun over −2°..+12° elevation, which extinction already does physically here. New SUN-row **Direct slider**
(0..5, default 1) scales the row only — the sky keeps its own brightness — and lands live through the §8
compare-and-reset like every other celestial edit.

**Three pre-existing biases came out with it (all fixed, all proven):**

1. **The kernel forgot the area pdf** — weights divided by the pick alone, paying every lamp's contribution
   divided by its area. The Cornell lamp (0.4 m² triangles) rendered **2.5× too bright**, and the kernel
   disagreed with the CPU raster, which always carried the area (`Tap.Weight`). The proof pins the diagnosis:
   the old weights converge to exactly C/A.
2. **The kernel forgot the emitter cosine** — lamp quads lit from both sides with no falloff, so the room's
   edges caught light the fixture never threw. The target, shade and bounce now pay the CPU's `LdotL`.
3. **The bounce divided by the slot count instead of multiplying** — a plain MC sample divides by its pdf
   (1/slots·1/area), i.e. multiplies by slots·area; the old line did the opposite, defended in-comment, paying
   the whole second bounce divided by **N²** (4× too dim for the Cornell's 2 triangles). The proof caught this
   in MY first draft (0.467 vs 0.917) before it shipped — the reason the proof exists.

**Expect the light balance to move — that is the correction, not a regression.** Lamp pools dim 2.5× (they
were wrong), the second bounce rises 4× (it was wrong), and the sun adds its shaft on top. `Sky Brightness`
scales sky + sun together (panel line 1149 does the same), `Direct` scales the sun alone, exposure absorbs
the rest. The estimator proof (24 checks: samplers, quadrature, mesh/sun/mixed reservoirs uniform + alias,
bounce, panel parity, old-weights C/A) plus the structural gate (pdf lines, single drawer/target, sentinel
discipline, shared body, panel gain, slider wiring, both gates) pin the new scale; all seven gates pass.

**Found but untouched:** glTF punctual/directional lights are still parsed and stored only
(`PunctualLuminaireRecord` — the kernel does not light from them yet); the disc's 12× boost stays a literal;
the d² floors (+0.01 shade vs +0.001 target) still differ by a negligible epsilon, as before.

## Appendix — build-script fix committed alongside

Your pasted link errors were real on this branch too: `ToolchainSequence.ps1`'s
`$EngineRelative` omitted `ControlPanel.cpp` (13 unresolved `Query*`/widget symbols from the
panels), `CelestialSolver.cpp` (`Solve`/`AirMass` from `CelestialSequence.obj`), plus
`ShadeTick.cpp` (referenced by `EditorHost.h`) and `StarCatalogueIndex.cpp` (`Catalogue.Load`
from `CelestialSequence.cpp:283`). All four are proven-required and added in this commit;
matches your snippet's placement.

## §12. The flare was accumulating — the white sun, the missing disc, the breathing size (2026‑09‑11)

**Reported:** the sun renders blinding white with no visible disc, and the lens flare keeps changing size.
Screenshots were linked but never arrived in the workspace (`/home/user/uploads` absent), so this proceeds
from the code — which confessed immediately.

**Root cause, one line:** the resolve added the flare to the running MEAN, after the temporal update, and
then stored that mean into the history:

```glsl
vec3 mean = ...; mean = mix(mean, radiance, 1/n);   // the temporal update — correct
...
mean += FlareAlong(...);                             // ← the bug: fresh flare on top of the update…
StoreHistory(pixel, vec4(mean, ...));                // …and then stored, so next frame's mean starts with it
```

A constant per-frame addition `c` after an incremental-mean update does not converge: `μₙ = μₙ₋₁ + (c − δ)/n
+ c` grows as **c·(n+1)/2** — the flare doubled every two frames. That is all three symptoms from one line:
the white flood (any flare drowns the disc in seconds), the instability (brighter reads as bigger, and every
reset restarts the climb, so the size breathes), and the buried disc (the 0.53° Duke core sits under an
ever-growing white sheet). The moon and the stars were buried by the same flood, not broken themselves.

**Fix:** the flare is a same-frame light term — scatter in the lens over this frame's photons — so it joins
the SAMPLE before the update, where the sky, the bow and the stars already stand:

```glsl
radiance += FlareAlong(...);   // before the running-mean update
```

The temporal mean then converges to sky + flare honestly, the variance accounting is untouched
(`Var(X+c) = Var(X)`), and the disc, the panel tuning and the Duke transcriptions needed no changes at all.
`CheckPostKernel.sh` pins the fix three ways: the flare lands on `radiance`, `mean += FlareAlong` never
returns, and the flare line precedes the mean update.

**Checked while in there, and cleared:**

1. **Miss-direction reconstruction** — the kernel rebuilds the primary direction from pixel centres as
   `Forward + Right·(ndc.x·tanHalf·aspect) − Up·(ndc.y·tanHalf)`, which inverts `RayGeneration.slang`
   exactly (`screenX`, `(1−2v)` Vulkan row, same aspect side). The disc, the moon and the star field all
   stand where the lens points; nothing is misplaced.
2. **Flare sun-UV** — `PackPostRecord`'s `SunU/SunV` inverts the same mapping (verified term by term), and
   `FlareAlong` works in aspect-corrected UV about the frame centre, so the ghosts march the sun–centre
   line at fixed radii. The flare MODEL has no view-dependent size term: ghosts fixed per index, halo at
   the panel radius, streak/starburst/bloom fixed exponentials. The breathing was the accumulation alone.
3. **Stars** — proved end to end, not trusted: the night sheet carries 598 star pixels the stars-off
   re-render lacks, while the noon pair differs by exactly 0 pixels (the 0.09 daylight gate holds). The
   night sheet in `Diagnostics/` shows the field the catalogue promises.
4. **Moon** — `MoonRenderProof` still green (disc centre moon-bright, sane pixel band, moonlit ground above
   baseline); the wire-up was never the problem, the flood was.

**Proof fidelity (§17).** The report also questioned whether proof images show what the project does. Audit
verdict: every image-emitting proof already renders through shipping translation units — real raster, real
solver, real codecs, zero mock identifiers — with exactly one bypass: `CelestialSkyProof` hand-built its
`CelestialSettings`, so its sheets showed a sky without stars, moons or sequence twilight. That bypass is
closed: the moments and the dawn stages now run `Prepare → Tick → ApplyTo` through
`CelestialTier::BudgetFor`, with the shipping catalogue (8 920 stars, asserted) and the shipping atlas, and
the dawn hours are SOLVED per elevation with the camera facing the true dawn azimuth. The dawn hairline now
peaks at 75.0 sharpness in the same −2° stage (the project's twilight, not struct defaults). The one
deviation that cannot close headless is declared, not hidden: the sheets render the GI-off raster (the
project's own fallback path) because the displayed kernel needs a GPU; the kernel is held to the same
models by the parity proofs and the gates. `CheckProofFidelity.sh` (10 pins) plus CLAUDE.md §17 keep it so.

**Expect the viewport to change a lot, and all of it is the fix:** the white flood drains over the first
second after this ships (the history must wash out), the disc reappears at its tuned size, the flare holds
still, and night brings the star field. Found but untouched: the disc's 12× literal, the glTF punctual
lights (still stored-only), the d² floor epsilon.

## §13. The screenshots arrived; the build they show predates its own fixes (2026‑09‑11)

**Reported, with screenshots this time:** no sun disc (white blur instead), textureless moon, no stars,
and sun/sky/atmosphere sliders that only land when the camera moves.

**Three of the four are a stale build — dated, not guessed.** The screenshots' clock reads 21:01 SAST:
the white flood is the §12 accumulation bug, fixed in `7d4d3fa` hours earlier; the frozen sliders are the
pre-§8 behaviour, fixed by the per-frame record memcmp in `6ea26dc` (16:33 UTC). And the "no stars" frames
are 15:30 daylight, where the 0.09 gate CORRECTLY shows nothing — the dusk frame in the same batch shows
the star field working. The fix for all three is pull + full rebuild; there is no code left to write.

**The white moon needed an experiment, not a review.** Every link of the texture path checks out on paper
(slots, layout, positional upload, gamma, tracked files, sufficient capacity), so the real Luna was decoded
and sampled through the real view construction headless: maria 0.38 vs highlands 0.62, disc std 0.25. The
shared file→decode→view→UV→sample chain is PROVEN correct — the raster side cannot produce a white moon.
What remains is GPU-side-only (stale .spv, unbound slot, or a driver without descriptor indexing), and it
is now cornered from both ends: the proof permanently asserts CONTENT (maria darker than highlands, giant-
disc variance — dimensions alone would pass a white rectangle), and the app logs a moon census
("N textures resident, moon slots lo..hi", warning when past the table) beside the existing bindless +
resident + star-upload lines. The next report of a white moon comes with the three console lines that
settle it, and they are asked for below.

**Found and fixed while here:** `PostRecords.slang` and `MaterialEvaluation.slang` were missing from the
toolchain's `$ShaderIncludeNames`, so edits to either never re-lowered the kernel on an incremental build —
the developer runs a stale kernel and debugs a ghost. Both are listed now, and `CheckBuildIntegrity.sh`
pins the list against the actual `#include`s of every lowered source (verified green, plus a negative test
on the old list). Note the gate itself stays red in this sandbox for the pre-existing reason — the
`ExternalPackages` submodules are empty here, so its submodule census and its `GameExecution.cpp` parse
cannot run; the new pin passes inside it.

**Ask the user for, after they rebuild:** (1) GPU model + driver version; (2) the `[SwapchainExchange]
bindless ?` capability line; (3) the `Textures: N resident` line; (4) the new `Moons:` census line; (5) the
`Stars:` upload line. If the moon is still white with resident slots and bindless on, the bug is in the one
place no headless proof can reach, and the numbers will say which.

## §14. The increment ate every reset — sliders frozen until the camera moved (2026‑09‑11)

**Reported, on a rebuilt build:** sun/sky/atmosphere sliders still only land when the camera moves. The user's
parenthetical ("or is it the ReSTIR that stops when frame converges") was aimed at the right machinery but the
wrong half of it: nothing stops at 256 (the "Baking complete" toast is notification-only), but no reset ever
took effect either.

**Root cause, three lines apart in the frame loop.** Each frame (1) reads `AccumulationIndex` into the dispatch
(`GameExecution.cpp:1245`), (2) the §8 record comparisons call `ResetAccumulation()` when the sky changes
(:1415/:1431/:1464), and (3) the loop unconditionally calls `IncrementAccumulationIndex()` after presenting
(:1489). So a reset-to-zero was incremented straight back to one before any frame read it: every slider reset
dispatched with `FrameIndex ≥ 1`, the kernel kept blending the old frame at 1/n, and edits only became visible
when a camera move failed reprojection geometrically (motion vectors + the 25°/10% validation rule) — which is
exactly the reported symptom, and why §8's correct detection changed nothing visible.

**Fix:** `ResetAccumulation()` now raises a `ResetPending` flag and the increment spends it instead of counting
(`ReSTIRIntegrator.h`): the frame after a reset dispatches with `FrameIndex 0` and starts genuinely fresh. All
callers route through the same method (F3 popup, §8 sky/moon/post, `ObserveCamera`), so all are fixed together.
`CheckAccumulationReset.sh` pins the flag on both methods plus the read→reset→present→increment order that
demands it.

**Not this bug:** the white giant moon in the same screenshots is unrelated (a converged mean of textured
samples is still textured) and still open pending the console lines asked in §13 — the build in the shots may
still predate the §13 census line, so pull first, then paste bindless/resident/`Moons:`/`Stars:`.
