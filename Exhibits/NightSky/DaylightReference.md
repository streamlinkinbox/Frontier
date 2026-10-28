# Daylight, celestial light and lunar bounce — CPU reference

This adds opt-in features to the actual `ReferenceRenderer`; it is **not** an editor/Vulkan integration or a
completed ReSTIR GI pipeline. Existing settings retain their previous defaults. The ordinary sky scattering
already responds to `SunDirection`; this continuation also adds an atmospheric directional Sun for surfaces,
a textured Moon, catalogue stars and a narrow Moon-lit indirect path.

## What is implemented

| Area | Status | Behaviour |
|---|---|---|
| Solar atmosphere | ✔️ CPU subset | `SolarSurfaceLighting` adds a separate directional Sun; `AtmosphericSunlight` controls its extinction. The planet and triangle shadow rays block the source. Sky scattering follows the Sun independently through `AtmosphereEnabled`. |
| Moon and stars | ✔️ CPU subset | Opt-in textured lunar disc and magnitude-based stars are present in the linear HDR background by day and night. Daylight can wash stars out; an optional artistic gain can boost them. The Moon geometrically masks point stars, and clouds and foreground geometry can obscure them. |
| Moon lighting | ✔️ CPU subset | `LunarSurfaceLighting` adds a directional Moon, with phase, apparent area, atmospheric extinction, planet and triangle shadowing. |
| Moon indirect | 🚧 Limited | `LunarIndirectLighting` samples one Moon-lit Lambertian bounce from analytic SDF spheres, boxes and half-spaces. Optional `IndirectTemporalReuse` combines only the same receiver during a single static `Render` call. |
| Full ReSTIR GI | 🔴 Incomplete | No GPU execution, dynamic reprojection, GI spatial shifts, general mesh/card materials, multibounce or full demo-pipeline parity. |

## Using the reference API

Prepare `EngineContent/CelestialMaps/MoonAlbedo2048.ppm` with `Automation/PrepareMoonAlbedo.py`. Supply the map,
Sun and Moon directions, and a local star catalogue rather than relying on hidden filesystem or time state:

```cpp
auto Map = std::make_shared<Frontier::LunarAlbedoMap>();
if (!Map->LoadPpm("EngineContent/CelestialMaps/MoonAlbedo2048.ppm"))
    throw std::runtime_error("lunar map not prepared");

const double Utc = Frontier::JulianDate(2026, 10, 18, 13.0); // 15:00 SAST
const double Tt = Utc + 69.184 / 86400.0; // example 2026 TT−UTC; supply appropriate time scales
const double Latitude = -26.2, Longitude = 28.3, Height = 1640.0;
const auto Moon = Frontier::MoonTopocentric(Frontier::MoonGeocentric(Tt), Utc, Latitude, Longitude, Height);
std::ifstream Csv("Exhibits/NightSky/Data/Hipparcos8.csv");
auto Stars = std::make_shared<std::vector<Frontier::StarRecord>>(
    Frontier::DecodeHipparcosCatalogue(Csv, Utc, Latitude, Longitude));

Frontier::ReferenceRenderSettings Settings;
Settings.SunDirection = Frontier::SunDirectionLocal(Frontier::SunPosition(Utc, Latitude, Longitude));
Settings.SolarSurfaceLighting = true; // Remove any equivalent Scene.Lights Sun to avoid double counting.
Settings.AtmosphericSunlight = true;  // Set false for an unattenuated surface Sun above the horizon.
Settings.Lunar.Albedo = Map;
Settings.Lunar.Direction = Moon.Direction;
Settings.Lunar.AngularRadiusRad = Moon.AngularRadiusRad;
Settings.LunarDiscEnabled = true;
Settings.LunarSurfaceLighting = true;
Settings.LunarIndirectLighting = true;
Settings.SiderealStars = Stars;
Settings.SiderealStarsEnabled = true;
Settings.StarBrightnessScale = 1.0; // Increase for deliberately nonphysical daytime visibility.
Settings.IndirectTemporalReuse = true; // Optional; static same-receiver history only.
```

The code above assumes the renderer's local east/north/up coordinates and UTC≈UT1. Set
`Settings.Lunar.North` and sub-observer longitude/latitude if a better projected pole/libration model is
available. The catalogue is a snapshot of local directions: recompute it when the observing time or location changes.
`SunIrradiance` supplies the common arbitrary linear scale, so setting it to zero also sets the model's Moon and
star fluxes to zero. The stars need not be visible on an ordinary daylight display to exist in the HDR signal.

To receive indirect moonlight, the scene must include a reflecting `DistancePrimitive` with
`DiffuseReflectance > 0`, for example an SDF wall with reflectance `0.8`; the receiving mesh also needs
`IndirectSamples > 0`. The separate SDF representation is not automatically populated from the triangle scene.
Emissive SDF paths retain their existing independent reservoir. Triangle shadows apply to direct moonlight;
indirect bounce and shadow rays see only the analytic SDF scene, with a 1,000 m ray limit.

## Evidence

Release and ASan/UBSan both pass **37/37** checks: [release](Results/Daylight-release.txt),
[sanitized](Results/Daylight-sanitized.txt). The independently executed star-direction comparison passes in
[release](Results/Catalogue-release.txt) and [sanitized](Results/Catalogue-sanitized.txt).

| Check | Observed | Scope |
|---|---:|---|
| Vertical atmospheric beam | max RGB error `0.000536` vs analytic exponential/triangular optical depths | Sea-level zenith, same assumed medium |
| Elevation-dependent surface Sun | max absolute RGB transmittance error `0.001112` vs 4,096-step integration | Five elevations, 1°–90°; at 1° RGB is `0.2265 / 0.0393 / 0.00110` |
| Moon disc flux | max relative error `0.00422` | Full-phase texture, radii 0.1–24 pixels; subpixel/transition/resolved paths |
| Daytime star | max RGB increment / mean sky luminance `3.52e-7` | One magnitude-2 star in HDR, not a calibrated observation |
| Star positions | mean / RMS / max `0.004842° / 0.005034° / 0.007170°` | 519 real catalogue rows including near-pole stars × three 2026 instants × four sites against Astropy/ERFA; no refraction or proper motion |
| Analytic SDF rays | 1,059 non-grazing intersections within `0.000961 m` of SDF marching | 5,000 mixed-scene directions; one hit/miss disagreement between the two methods near a boundary |
| Moon-lit infinite wall | analytic irradiance `0.282483`; temporal mean `0.283668 ± 0.001341` | 2,400 independent sequences, 16 frames; standard error, 1,000 m ray limit |
| Actual renderer lunar bounce | expected red `2.36709e-7`; fresh `2.36247e-7`, temporal `2.35609e-7`, disabled `0` | One receiver, static SDF wall; lunar direct lighting disabled |
| Prior defaults/regressions | `04da4f54d91705d0` and 7/7 CTests | Existing default-off binary64 probe matches its earlier baseline; not a universal equivalence claim |

[Daytime Moon](Images/DayMoonReference.png), [night Moon](Images/NightMoonReference.png) and
[night catalogue](Images/NightStarsReference.png) are actual reference-renderer captures at Benoni model times.
The first two are narrow 3° views; the star field uses an illustrative long exposure. Their exposures differ,
so image brightnesses should not be compared as a calibrated photometric measurement. The identical-view
[day sky](Images/DaySkyReference.png) and [boosted stars](Images/DayStarsBoostedReference.png) use the same
exposure; the latter intentionally applies a **nonphysical 100,000× star gain**. The Moon uses the retained
actual demo JPEG; its redistribution licence remains to be confirmed.

## Model limits

- The default full-Moon/Sun flux ratio is `10^(-0.4 × 14) = 2.511886e-6`, based on approximate visual magnitudes
  −12.74 and −26.74 in the [NASA Moon](https://nssdc.gsfc.nasa.gov/planetary/factsheet/moonfact.html) and
  [Sun](https://nssdc.gsfc.nasa.gov/planetary/factsheet/sunfact.html) fact sheets. The demo albedo is normalized
  to this reference at a mean lunar angular radius; flux changes
  with apparent area. This is a broadband proxy, not a measured spectral lunar BRDF or absolute lux calibration.
  The phase law, earthshine, texture orientation and libration are approximate.
- Refraction, terrain, partial planet-limb clipping, lunar eclipses, atmospheric airglow and proper motion are
  not represented. The Moon's centre is used for the geometric horizon test; near-limb partial visibility is absent.
  The lunar surface beam is a point-direction approximation; cloud transmittance affects the visible sky, not
  direct Sun/Moon surface shadows or SDF bounce illumination.
- Moon-bounce atmospheric transmittance is evaluated at camera altitude, not independently at each bounce. The
  reference path uses scalar SDF diffuse reflectance and the lunar beam's RGB ratios. It does not sample stars or
  the lunar disc as an environment MIS proposal. Renderer work here is opt-in and may be expensive; it does not
  alter the separately maintained cheap test raster.
- Temporal reuse assumes unchanged scene and illumination during one call; pointer/receiver guards cannot detect
  in-place scene mutation. No extrapolation to moving scenes or spatial path reuse is justified.

## Reproduce

```sh
python3 -m venv _ClaudeScratch/venv
_ClaudeScratch/venv/bin/pip install Pillow cmake -r Exhibits/NightSky/Code/EphemerisRequirements.txt
export PYTHON="$PWD/_ClaudeScratch/venv/bin/python"
export CMAKE="$PWD/_ClaudeScratch/venv/bin/cmake"
CAPTURE=1 bash Exhibits/NightSky/RunDaylightValidation.sh
SANITIZE=1 bash Exhibits/NightSky/RunDaylightValidation.sh
bash Exhibits/NightSky/RunCatalogueValidation.sh
SANITIZE=1 bash Exhibits/NightSky/RunCatalogueValidation.sh
"$CMAKE" -S Exhibits/VehicleReference -B _ClaudeScratch/build/vehicle-reference -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build _ClaudeScratch/build/vehicle-reference -j2
"$PWD/_ClaudeScratch/venv/bin/ctest" --test-dir _ClaudeScratch/build/vehicle-reference --output-on-failure
_ClaudeScratch/build/vehicle-reference/DirectSpatialValidation --default-probe
```

Pillow and CMake are required for the first runner; Astropy and its pinned dependencies are required for the
catalogue comparison. The runners use `set -euo pipefail` and retain executable failures.
