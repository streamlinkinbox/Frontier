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

## Appendix — build-script fix committed alongside

Your pasted link errors were real on this branch too: `ToolchainSequence.ps1`'s
`$EngineRelative` omitted `ControlPanel.cpp` (13 unresolved `Query*`/widget symbols from the
panels), `CelestialSolver.cpp` (`Solve`/`AirMass` from `CelestialSequence.obj`), plus
`ShadeTick.cpp` (referenced by `EditorHost.h`) and `StarCatalogueIndex.cpp` (`Catalogue.Load`
from `CelestialSequence.cpp:283`). All four are proven-required and added in this commit;
matches your snippet's placement.
