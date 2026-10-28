# Environment pipeline validation

Headless proof for the Phase 6 portable cores (plan §8.1 atmosphere/celestial/clouds/
flare-fog/precipitation groups, §13 Phase 6):

- `Frontier/Engine/Rendering/AtmosphereModel.*` — Rayleigh/Mie/ozone medium, numeric
  transmittance + horizon-stretched LUT, Rayleigh/Henyey-Greenstein/Cornette–Shanks phases, single
  scattering and a bounded multiple-scattering approximation;
- `Frontier/Engine/Rendering/CelestialModel.*` — Julian dates, low-precision solar
  position, twilight classification, lunar phase, deterministic star catalogue;
- `Frontier/Engine/Rendering/FogMedium.*` — exponential height fog by closed-form integral;
- `Frontier/Engine/Rendering/CloudField.*` — integer-hashed value-noise density slab +
  Beer-Lambert raymarch with sun shadow march;
- `Frontier/Engine/Rendering/WeatherEffects.*` — analytic (wrap-exact) precipitation
  volume + midpoint-displacement lightning with restrike envelope;
- `Frontier/Engine/Rendering/LensFlareModel.h` — ghost layout on the sun-centre axis,
  limb-darkened sun disc with Gaussian-blurred analytic limb, aperture ghosts, starburst,
  anamorphic streak, halo, glow and veiling glare (radiance layer 2026-10-26, measured in
  Exhibits/NightSky);
- `Frontier/Engine/Rendering/EnvironmentUnits.*` — the whole environment frame declared
  (LUT bakes persistent, depth imported).

Analytic oracles: vertical optical depths τ = β·H (numeric 0.94039/0.86764/0.76233 vs
analytic 0.94040/0.86766/0.76235, re-measured 2026-10-26 after the Mie extinction split was
corrected to the Bruneton total 4.40e-3/km — scattering 3.996e-3 + absorption 0.404e-3);
LUT vs direct integration 0.219% mean error; phase
functions integrate to 1 by independent quadrature; noon sky B>G>R and sunset R>B from
the march itself; deep-night sky exactly zero; solstice declinations ±23.44°; solar-noon
elevation identity at Benoni, ZA (74.80° = 74.80° on 2026-10-22); fog vs Simpson
quadrature to 1e-4; uniform-slab cloud march == Beer-Lambert to 1e-9 (an early-out that
truncated the transmittance product was caught by exactly this check and fixed);
precipitation wrap periodicity exact; lightning endpoint/count/seed identities.

Latest result: **73/73 checks passed** (GCC, `-std=c++17 -Wall -Wextra -Werror`).
Optional argument: a path prefix writing `<prefix>sky-strip.ppm` — [noon | sunset |
sunset under cloud deck], single-scattering + cloud march (PNG master in `Capture/`).

Scope honesty: Earth constants are the canonical published ones (Bruneton/Hillaire).
Parameter parity with the demonstration's AtmosphereScattering.wgsl was verified on
2026-10-26 and is pinned by a check in Exhibits/NightSky; the aerosol phase now matches the full demo's normalized Cornette–Shanks form (2026-10-27).
This is not full multiple-scattering LUT parity.
Multiple scattering is a bounded, sun-gated second-order approximation (calibrated
2026-10-26 to the measured 10–30% clear-sky range; the previous form returned ~190%
and was sun-independent). Lightning is
visual only — thunder audio is Phase 8. Nothing here claims GPU execution.
