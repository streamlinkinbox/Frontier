# Ocean simulation research (2024–2026)

What the real-time ocean state of the art looks like right now, and what this
simulator takes from it. Classic foundations (Tessendorf 2001, JONSWAP 1973,
TMA 1985, Dupuy & Bruneton whitecaps) are assumed; this note covers what
changed or got productized in 2024–2026.

## 1. Spectral FFT oceans are the baseline, JONSWAP won

- The 2025 hybrid-ocean paper couples a **global FFT background** with local
  Lagrangian wave-particle patches, all driven by **one unified JONSWAP
  directional spectrum** with matched energy density at patch boundaries, plus
  frequency-bucketed GPU synthesis for real-time rates
  ([arXiv HTML](https://arxiv.org/html/2511.02852v1),
  [abstract](https://arxiv.org/abs/2511.02852)).
  Takeaway: one spectrum everywhere; local effects (wakes, ripples, surf)
  must match the background spectrum instead of being layered decor.
- Three.js Water Pro (WebGPU, current) is FFT-based with a **JONSWAP spectrum
  in 3 cascades (swell / waves / ripples)**, Jacobian breaking detection and
  subsurface scattering
  ([docs](https://docs.threejswaterpro.com/)).
- ABYSSAL (single-file WebGL2 + three.js) runs **3 FFT cascades with a
  Cooley–Tukey butterfly**, JONSWAP+TMA spectrum, Donelan–Banner spreading,
  Jacobian foam accumulated in a ping-pong buffer, and a camera-centred radial
  mesh to the horizon
  ([repo](https://github.com/squall01337/abyssal-ocean)).
- Unity FFT work documents the practical recipe: Tessendorf-style amplitudes,
  **TMA shallow-water correction** of JONSWAP, Hasselmann-type directional
  spreading, high-frequency fade factor
  ([Ocean-Simulation](https://github.com/Mozobo/Ocean-Simulation/)).

**What we built:** an 80-component JONSWAP spectrum (swell / wind-sea / chop
cascades, cos^2s spreading, fetch-limited Hasselmann growth laws, `Hs = 4√m0`
energy normalization) evaluated as a GPU Gerstner sum with analytic normals
and full horizontal-displacement Jacobian. Same oceanography as the FFT
systems, in a form that also gives us a bit-exact CPU mirror for probes,
buoyancy and the interference meter — no readbacks, no butterfly passes to
tune blind.

## 2. Breaking / surf waves went mathematical

- Oceanology NextGen (UE5, actively developed through 2025) ships **hybrid
  FFT + Gerstner spectral waves with formula-driven breaking waves** that
  rise, barrel and collapse, Beaufort-scale sea states, depth-aware
  shallow-water transformation and flow-based foam
  ([Fab listing](https://www.fab.com/listings/87c9af41-62b7-4e70-98e3-fc72eff016ab)).
- Waveor's Quantum Engine (the tech behind Virtual Surfing) is a real-time
  breaking-wave + foam + splash water model with surfboard buoyancy
  ([waveor.com](https://waveor.com/)).

**What we built:** depth-driven surf shaping on top of the spectral field —
Green's-law shoaling gain, wavelength shortening and slowdown in the shallows,
crest-rió jack-up, a shoreward **barrel throw**, and an alongshore-travelling
peel pulse over a skewed reef bar. Clean-up control via per-cascade gain
(kill chop → glassy faces).

## 3. Foam = crest physics + particles + advection

- FumeFX 7.0 (Aug 2025) added **NodeWorks Ocean**: GPU waves driving foam /
  spray / bubble particles from **crest curvature and wave motion**, rendered
  as points with age/velocity/density attributes
  ([Digital Production](https://digitalproduction.com/2025/08/21/fumefx-7-0-on-water-new-version-adds-nodeworks-ocean/)).
- OloEngine's 2025 water work is explicit about the failure mode to avoid:
  whitecaps as a *static function of the surface* pop in place; their fix is
  **advected foam** (stored in a field, drifted by velocity), crest spray
  from the same fold detector, and measured — not guessed — fold thresholds
  ([PR #1053](https://github.com/drsnuggles8/OloEngineBase/pull/1053)).
- Foundational: Dupuy & Bruneton's GPU whitecaps from a wave-deformation
  criterion with linear prefiltering
  ([paper (PDF)](https://inria.hal.science/hal-00967078v1/document)).

**What we built:** two foam layers. (1) Surface foam from the Tessendorf
**Jacobian fold factor** + breaker mask + shoreline swash, broken up by
advected multi-scale noise. (2) A **stateless GPU particle system** (up to
200k: ballistic spray, riding crest foam, shoreline wash, wind-advected
streaks) whose emitters sit on the reef break line, gated by the same peel
pulse that breaks the waves — emission follows physics, not timers.

## 4. Water optics checklist (all implemented)

Absorption by water depth, Schlick Fresnel into a shared sky function,
slope-roughened sun glitter, backlit crest **subsurface scattering**, foam
sparkle, ACES + distance fog matched to the horizon color. The sky (gradient,
sun, procedural clouds) is one GLSL function shared by the dome and the water
reflection so lighting can never disagree with itself.

## 5. Deliberate deviations from the literature

- **No FFT butterfly.** A 512² Cooley–Tukey chain per frame is the fidelity
  king, but the spectral-sum hybrid (cf. Oceanology's FFT+Gerstner stance)
  gives art-directable surf shaping, trivial CPU mirroring and zero
  ping-pong/precision risk in a browser.
- **Stateless particles** instead of a feedback advected-foam buffer: zero
  readbacks, zero frame-history hazards, deterministic scrubbing with the
  pause/time-scale transport controls.
- **Localized wave lab**: two extra trains under a gaussian envelope so
  destructive interference reads as a calm disc with a live
  `var(A+B)/(varA+varB)` meter — a simulator instrument, not a preset.
