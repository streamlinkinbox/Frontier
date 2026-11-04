# Environment pipeline validation

Headless proof for the portable `PlanetaryAtmosphere` & `Rendering` cores:

- `Frontier/Engine/PlanetaryAtmosphere/AtmosphereScattering.*` — Rayleigh/Mie/ozone medium, numeric
  transmittance + horizon-stretched LUT, Rayleigh/Henyey-Greenstein/Cornette-Shanks phases, single
  scattering, bounded multiple-scattering, and 6-point Gauss-Legendre coupled solar/sky/twilight continuum;
- `Frontier/Engine/PlanetaryAtmosphere/AstronomicalEphemeris.*` — Julian dates, solar/lunar Meeus
  ephemeris, twilight classification, lunar phase, deterministic star catalogue, and Lommel-Seeliger disc radiance;
- `Frontier/Engine/PlanetaryAtmosphere/FogSpace.*` — exponential height fog by closed-form integral + local volumetric fog;
- `Frontier/Engine/PlanetaryAtmosphere/CloudSpace.*` — non-tiled 3D integer-hashed C2 quintic rotated-octave density slab +
  Beers-Powder raymarch + orthographic `CloudShadowProjection`;
- `Frontier/Engine/PlanetaryAtmosphere/WeatherEffects.*` — analytic (wrap-exact) precipitation
  volume + midpoint-displacement lightning with restrike envelope;
- `Frontier/Engine/PlanetaryAtmosphere/OpticalLensFlare.*` — ghost layout on the sun-centre axis + solar disc filter;
- `Frontier/Engine/Rendering/EnvironmentUnits.*` — the whole environment frame declared
  (LUT bakes persistent, depth imported).

Latest result: **73/73 checks passed**.
Optional argument: a path prefix writing `<prefix>sky-strip.ppm` — [noon | sunset | sunset under cloud deck].
