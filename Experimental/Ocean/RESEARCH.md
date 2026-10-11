# Ocean v3 — research brief

Status: **research, no implementation**. `Ocean.html` v1/v2 was deleted after
review (scored 3/10: waves invisible/unrealistic, foam was a water-shader
effect against an explicit ban). This document exists so v3 does not repeat
those mistakes. Nothing here is a plan-on-a-napkin: every section below names
its sources.

## 1. Post-mortem: why v1/v2 failed

**v1** — 10 hand-placed Gerstner waves + shader foam (run-up band + crest
caps) + 12k round sprite dots.
**v2** — 28-wave seeded spectrum + particles-only foam (30k atlas sprites).

What was wrong, in the order it should have been caught:

1. **No reference targets.** Never gathered real surf photos/video to match.
   Judged every screenshot against imagination. *Rule for v3: reference
   board first (5+ photos: spilling break, plunging lip, whitewater mat,
   swash, whitecaps offshore); every review compares against it.*
2. **Effects before look.** Lighting/camera/palette were tuned last. The sun
   slider defaulted to 0.18 (flat grey) then swung to orange soup (0.6) and
   pink soup (0.35) because the palette stops were never validated against a
   noon-blue default. *Rule: default frame (lighting + camera + water color)
   must read as "ocean" with ZERO foam/particles before any effect lands.*
3. **Violated an explicit constraint hoping it would pass.** "No shader for
   the foam" was stated, and v1 shipped shader foam anyway. *Rule: user bans
   are grep-gates, not aspirations. v3 must pass
   `grep -ci foam` == 0 on the water shader (particles excepted).*
4. **Debugged symptoms instead of questioning architecture.** The violet-dash
   saga (10+ rounds) ended at "simultaneous-contrast illusion over legit
   shading". The yellow band was sand poking through holes that exist *by
   construction* (Gerstner lateral displacement vs a rest-X clamp). Bisection
   cannot fix architecture. *Rule: if an artifact survives two rounds, the
   model is wrong, not the constants.*
5. **CPU/GPU surface mirrors diverged.** Particles rode a 3-wave CPU mirror
   of a 10–28-wave GPU surface; the clamp existed only on GPU. Result: foam
   floating, buried, or banded. *Rule: one height function both sides read
   (FFT textures sampled on GPU; CPU reads back the same field or a shared
   analytic subset — never two independent implementations that drift).*
6. **No scale budget.** λ0.75 chop on 3 m cells; λ62 primary viewed from
   inside one face; particle sizes vs viewing distance eyeballed.
   *Rule: wavelength / camera-distance / mesh-density / sprite-size are
   budgeted together, on paper, before coding.*
7. **Validated via thumbnails, misread them.** The `hide=water` "failure" was
   a misread thumbnail; pixels later proved the mechanism worked. *Rule:
   sample pixels, don't squint; distrust any single run (diag7/8 shuffle).*

## 2. What the field does (findings)

### 2.1 Tessendorf FFT ocean — the realistic-water standard

"Simulating Ocean Water" (Tessendorf 2001/2004) is what films
(*Titanic*, *Pirates of the Caribbean*, *Moana*, *Life of Pi*) and games
(*AC IV: Black Flag*, *Sea of Thieves*, *ATLAS*) build on. Pipeline:

1. **Spectrum.** Phillips (or JONSWAP) directional spectrum from
   wind speed/direction + fetch:
   `P(k) = A · exp(-1/(kL)²) / k⁴ · |k̂·ŵ|²`, suppressing tiny waves.
2. **Initial state.** `h̃₀(k) = gauss() · √(P(k)/2)` per wavevector, once.
3. **Propagation.** `h̃(k,t) = h̃₀(k)e^{iωt} + h̃₀*(-k)e^{-iωt}`,
   deep-water dispersion `ω = √(g|k|)`; **finite-depth**
   `ω = √(gk·tanh(kh))` slows waves over shallows — physical shoaling
   falls out of the model (gikster implementation notes).
4. **IFFT → spatial.** 4–5 inverse FFTs yield height, XZ displacement
   (choppiness), XZ slopes. Normals come from slopes.
5. **Jacobian.** `J = (1+∂Dx/∂x)(1+∂Dz/∂z) − (∂Dx/∂z)(∂Dz/∂x)`;
   `J ≤ 0` marks folding crests → the foam-emission signal (used by
   Tessendorf's notes, Crest, GodotOceanWaves).

Why FFT beats hand-placed Gerstner (GodotOceanWaves README states it best):
Gerstner is fine for low-frequency calm water but cannot produce open-ocean
chop/interference; spectra expose sea-state parameters (wind, swell,
choppiness) directly instead of per-wave fiddling. Our v1/v2 *were* the
failure mode they describe.

### 2.2 FFT in WebGL2 is proven — no compute shaders needed

FFT runs as fragment-shader ping-pong (Stockham butterflies via a
precomputed dataflow texture). Precedents: **jbouny/fft-ocean**
(three.js + projected grid, live demo), **Frederoche/Webgl-FFT-Ocean**
(Tessendorf + Phillips + projected grid), ARM's GLES FFT-ocean SDK sample,
**david.li/waves** (interactive wind/size/choppiness demo), and Tidewater's
WebGL2 fallback (cascaded FFT in fragment passes). Typical: 256²–512²
RGBA16F, 2–3 cascades (e.g. 512 m / 128 m / 32 m tiles) à la Crest.

### 2.3 Foam with NO water-shader mask — the sanctioned architecture

The user ban is compatible with the literature if we split *emission*
(physics, in the water system) from *appearance* (particles, the only
visible foam):

- Emission field on GPU: Jacobian fold + steepness + shore-breaker
  criterion, grown linearly / decayed exponentially on a texture
  (GodotOceanWaves foam maps; Crest foam).
- Appearance: **particles only** — spray/whitewater/whitecap sprites
  spawned and culled by the emission field, with per-sprite dissolve.
  Parberry's halftone dither gives bubble-pop erosion *on the sprites*,
  not on the water.
- OMYOG shoreline + "A Model for Real Time Ocean Breaking Waves"
  (wave-map + particles) confirm: particles-as-foam-density is the
  accepted surf representation.

### 2.4 Breaking surf = FFT + depth-driven shore transform

Deep-water FFT does not break. The shore layer is separate and analytic:

- Depth texture baked from the beach profile (CPU, once).
- Shoaling: wavelength shortens, height grows, celerity drops with depth;
  choppiness rises toward the bar.
- Breaker criterion: `H/h ≳ 0.78` (Miche) or Jacobian < threshold at the
  bar → particle lip emission + whitewater mat source; swash = particles
  advected over the beach profile to/from the waterline.

### 2.5 Mesh, light, camera guidance (from prior art + our scars)

- Mesh: static grid (256–512² over the surf reach) + FFT vertex texture
  fetch + 2 fragment normal cascades. Projected grid (Bruneton/jbouny)
  is a later optimization, not v3.
- Light: mid sun (30–50°) models wave form; default sky stays
  blue-dominant; fresnel-into-sky is what makes water read as water —
  keep, but validate against references instead of eyeballing.
- Camera: show the line-up (several crest wavelengths) by default;
  close-up presets for the lip. Lock before effects.

## 3. v3 plan (committed)

1. **Reference board** — collect 5–8 surf photos/video frames; palette +
   structure targets written down. Gate: board exists before code.
2. **FFT core** — Tessendorf/Phillips, 256², WebGL2 fragment ping-pong,
   one cascade + one detail cascade; fields: height, displacement,
   slopes, Jacobian emission. CPU mirror reads the same spectra
   (shared code path, §1.5).
3. **Shore module** — baked depth texture; shoal transform; Miche/Jacobian
   breaker criterion driving emission only.
4. **Water shader: absorption + fresnel + spec + SSS. Zero foam terms.**
   Gate: `grep -ci "foam\|band\|crest" water-shader <assembled>` == 0.
5. **Particle foam (the only foam)** — seeded emission from §2.3 fields:
   lip spray, whitewater mat, whitecaps, swash. Blob-atlas sprites with
   halftone erosion. Budgeted sizes/counts (§1.6).
6. **Look-dev gate** — default frame (noon-blue) reviewed against the
   board with particles OFF, then ON. Multi-t, multi-view screenshots
   + pixel sampling keep `?t= ?static= ?segs= ?only= ?hide=`.
7. **Deliverable** — single self-contained HTML (vendored three.js),
   same harness as v1/v2.

Open questions for the user (before build): spilling vs plunging
default break? Fixed cinematic camera vs orbit default? Nothing else
blocks starting.

## 4. Sources

- Tessendorf, "Simulating Ocean Water" (course notes, PDF):
  https://jtessen.people.clemson.edu/reports/papers_files/coursenotes2004.pdf
- "Oceans: Theory to Implementation" (JONSWAP + TMA depth + code):
  https://gikster.dev/posts/Ocean-Simulation/
- jbouny/fft-ocean (three.js FFT + projected grid + demo):
  https://github.com/jbouny/fft-ocean
- Frederoche/Webgl-FFT-Ocean (Tessendorf/Phillips, projected grid):
  https://github.com/Frederoche/Webgl-FFT-Ocean-
- GodotOceanWaves (FFT + Jacobian foam + spray particles):
  https://github.com/2Retr0/GodotOceanWaves
- david.li/waves (interactive FFT waves reference):
  https://david.li/waves/
- Bruneton ocean/atmosphere sources: https://evasion.inrialpes.fr/Membres/Eric.Bruneton/
- ARM GLES FFT-ocean SDK sample:
  https://arm-software.github.io/opengl-es-sdk-for-android/ocean_f_f_t.html
- Yuksel/House/Keyser, "Wave Particles" (particles→heightfield, wakes):
  https://www.cemyuksel.com/research/waveparticles/
- "A Model for Real Time Ocean Breaking Waves Animation" (wave-map +
  particles, ResearchGate — may be access-gated)
- Tidewater architecture notes (cascaded FFT + CPU mirror + foam from
  surface compression): https://ilikekillnerds.com/2026/05/21/i-built-tidewater-threejs-ocean-kit/
- Parberry et al., foam halftoning (sprite erosion technique):
  https://ianparberry.com/pubs/GAMEON-NA_GRAPH_04.pdf
- keithlantz.net FFT ocean series: currently DOWN (database error);
  use gikster + jbouny instead.

## 5. Break style decision: spilling (decided)

Default: **spilling beach break**. Three reasons, all load-bearing:

1. **It matches the bathymetry.** Our beach (~14 m over 170 m, ≈1:12,
   plus bar) sits in the spilling regime (Iribarren number well under
   ~0.5). Plunging needs a steep bar or reef we do not have; faking it
   on a gentle slope is exactly the kind of wrong that reads as fake.
2. **It matches the particle architecture.** Spilling = foam cascading
   down the face + whitewater mat behind + swash — precisely what
   emission-gated sprites render well. No mesh surgery required.
3. **Honest about heightfields.** FFT surfaces cannot overturn into
   hollow barrels, full stop. A "plunging" default would promise
   geometry the mesh cannot produce. Spilling is what looks most
   realistic *within a heightfield*, which is the only honest target.

A steep-face parameter may suggest pitching on the biggest sets (face
steepening + dense lip-particle curtain), but there will be no
hollow-barrel claims anywhere in v3.
