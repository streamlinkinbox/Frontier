# Environment pipeline validation

Headless proof for the Phase 6 portable cores (plan §8.1 atmosphere/celestial/clouds/
flare-fog/precipitation groups, §13 Phase 6):

- `Frontier/Engine/Rendering/AtmosphereModel.*` — Rayleigh/Mie/ozone medium, numeric
  transmittance + horizon-stretched LUT, Rayleigh/Henyey-Greenstein phases, single
  scattering and a bounded multiple-scattering approximation;
- `Frontier/Engine/Rendering/CelestialModel.*` — Julian dates, low-precision solar
  position, twilight classification, lunar phase, deterministic star catalogue;
- `Frontier/Engine/Rendering/FogMedium.*` — exponential height fog by closed-form integral;
- `Frontier/Engine/Rendering/CloudField.*` — integer-hashed value-noise density slab +
  Beer-Lambert raymarch with sun shadow march;
- `Frontier/Engine/Rendering/WeatherEffects.*` — analytic (wrap-exact) precipitation
  volume + midpoint-displacement lightning with restrike envelope;
- `Frontier/Engine/Rendering/LensFlareModel.h` — ghost layout on the sun-centre axis;
- `Frontier/Engine/Rendering/EnvironmentUnits.*` — the whole environment frame declared
  (LUT bakes persistent, depth imported).

Analytic oracles: vertical optical depths τ = β·H (numeric 0.93588/0.86348/0.75867 vs
analytic 0.93591/0.86351/0.75870); LUT vs direct integration 0.28% mean error; phase
functions integrate to 1 by independent quadrature; noon sky B>G>R and sunset R>B from
the march itself; deep-night sky exactly zero; solstice declinations ±23.44°; solar-noon
elevation identity at Benoni, ZA (74.80° = 74.80° on 2026-10-22); fog vs Simpson
quadrature to 1e-4; uniform-slab cloud march == Beer-Lambert to 1e-9 (an early-out that
truncated the transmittance product was caught by exactly this check and fixed);
precipitation wrap periodicity exact; lightning endpoint/count/seed identities.

Latest result: **73/73 checks passed** (GCC, `-std=c++17 -Wall -Wextra -Werror`).
Optional argument: a path prefix writing `<prefix>sky-strip.ppm` — [noon | sunset |
sunset under cloud deck], single-scattering + cloud march (PNG master in `Capture/`).

Scope honesty: Earth constants are the canonical published ones (Bruneton/Hillaire);
the demo's shader constants were never read, so parameter parity is an SDK-phase
comparison. Multiple scattering is a bounded second-order approximation. Lightning is
visual only — thunder audio is Phase 8. Nothing here claims GPU execution.
