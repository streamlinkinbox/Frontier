# Sky, Sun, lunar texture and mixed lens flare

Portable production models rendered by the native CPU exhibit; **not a Vulkan/Windows acceptance capture**.
This continuation corrects the earlier source audit: the full VehicleHMI reference contains lunar textures,
soft Sun edges and several independently mixed flare components. The local SlateReference copies do not.

## Reproduce

```sh
python3 -m venv _ClaudeScratch/venv
_ClaudeScratch/venv/bin/pip install Pillow
PYTHON="$PWD/_ClaudeScratch/venv/bin/python" bash Exhibits/NightSky/RunNightSky.sh
PYTHON="$PWD/_ClaudeScratch/venv/bin/python" SANITIZE=1 bash Exhibits/NightSky/RunNightSky.sh
# Independent, offline JPL DE421 comparison; downloads the pinned Python dependencies once.
_ClaudeScratch/venv/bin/pip install -r Exhibits/NightSky/Code/EphemerisRequirements.txt
PYTHON="$PWD/_ClaudeScratch/venv/bin/python" bash Exhibits/NightSky/RunEphemerisValidation.sh
PYTHON="$PWD/_ClaudeScratch/venv/bin/python" SANITIZE=1 bash Exhibits/NightSky/RunEphemerisValidation.sh
# Requires CMake and g++; CAPTURE=1 also requires Pillow.
CMAKE=cmake PYTHON="$PWD/_ClaudeScratch/venv/bin/python" CAPTURE=1 bash Exhibits/NightSky/RunSolarValidation.sh
CMAKE=cmake SANITIZE=1 bash Exhibits/NightSky/RunSolarValidation.sh
# Actual reference-renderer daylight, textured Moon, star catalogue and lunar bounce.
CMAKE=cmake PYTHON="$PWD/_ClaudeScratch/venv/bin/python" CAPTURE=1 bash Exhibits/NightSky/RunDaylightValidation.sh
CMAKE=cmake PYTHON="$PWD/_ClaudeScratch/venv/bin/python" SANITIZE=1 bash Exhibits/NightSky/RunDaylightValidation.sh
PYTHON="$PWD/_ClaudeScratch/venv/bin/python" bash Exhibits/NightSky/RunCatalogueValidation.sh
PYTHON="$PWD/_ClaudeScratch/venv/bin/python" SANITIZE=1 bash Exhibits/NightSky/RunCatalogueValidation.sh
```

If CMake is installed in the virtual environment, set `CMAKE="$PWD/_ClaudeScratch/venv/bin/cmake"` instead.

GCC 12.2, C++17, `-O2 -Wall -Wextra -Werror`, Linux CPU. Source basis: `ae0bb7d` plus this continuation's changes.
The runner converts the retained demo JPEG into the ignored runtime PPM. The previous checkout omitted its lunar
source; the initial reproduction failed 3 checks. That failure is retained in `Results/InheritedSkyFailure.txt`.

## Reference-renderer daylight continuation

The actual CPU `ReferenceRenderer` now has opt-in atmospheric surface sunlight, day/night textured Moon and
Hipparcos stars, lunar direct light and a static one-bounce lunar SDF path. Release and ASan/UBSan pass 37/37;
the 6,228-case Astropy/ERFA catalogue comparison has max angular difference `0.007170°`. The Moon uses the
actual demo map. These are limited reference paths, **not** a complete GPU ReSTIR GI pipeline or observed-sky
calibration. Setup, evidence, captures and limitations: [DaylightReference.md](DaylightReference.md).

## Source audit and changes — 2026-10-27

| Area | Inspected source / production behaviour |
|---|---|
| Full reference | `Experimental/VehicleHmiReference`, pinned `6afb6f00b5f25c45f7cda92c7da65b64845768f3` |
| Atmosphere | Demo coefficients retained; single scattering now uses its normalized Cornette–Shanks aerosol phase |
| Sun | CPU reference renderer uses finite-pixel filtering of a limb-darkened disc; apparent radius 0.2666° |
| Warm streak | Ported full-demo exponential profile with correct full-height → half-height coordinate conversion |
| Mixed optics | Independent warm streak gain/rotation/spread, added alongside blue anamorphic, burst, ghosts and halo |
| Moon | Actual demo `luna_2k.jpg`, 2048 × 1024; byte-identical source retained with provenance |
| Moon colour | Fixed sRGB bytes being treated as linear radiance; decode before interpolation, matching the demo texture |
| Moon position | Meeus periodic series, topocentric WGS84 parallax and distance-dependent apparent radius; star coverage is masked |
| Moon phase | New geocentric finite-distance illuminated-fraction API; legacy `MoonPhase` remains a mean-age approximation |

The generic flare model retains a zero warm-streak default for existing callers. In `ReferenceRenderSettings`, flares
are opt-in; when enabled, warm streak (gain 0.35, angle −0.18 rad) and blue anamorphic streak are both active. Flare
is added after geometry, clouds and fog, before display encoding. Source irradiance is attenuated at the Sun direction;
a 13-point screen-space visibility estimate checks geometry, with one extra cloud ray when clouds are enabled.
Its irradiance scaling is linear, unlike the demo's artistic brightness clamp. The other flare terms retain the
existing parameterized implementation; this is not a wholesale pixel-identical port of every demo optical formula.

## Evidence

| Capture | Interpretation |
|---|---|
| `Images/SunDiskLensFlare.png` | NightSky exhibit: scattering sky with simultaneous warm and blue streaks |
| `Images/SolarReferenceMixed.png` | Actual CPU reference-renderer capture with both streaks and other flare terms |
| `Images/DayMoonReference.png` | Actual reference renderer: daylight Moon, 3° view, Benoni model time |
| `Images/DaySkyReference.png` / `Images/DayStarsBoostedReference.png` | Same daylight view/exposure; second uses nonphysical 100,000× star gain |
| `Images/NightMoonReference.png` / `Images/NightStarsReference.png` | Actual reference renderer: night Moon and catalogue field; exposures differ |
| `Images/SunDiscClose.png` | Telephoto Sun at sky exposure |
| `Images/SunDiscFiltered.png` | Same radiance at solar-filter exposure, revealing limb darkening |
| `Images/MoonClose.png` | Restored demo texture, Benoni moonrise orientation, atmospheric tint |
| `Images/MoonPhases.png` | New / first quarter / full / last quarter geometry and earthshine |
| `Images/DuskSequence.png` | One exposure, Sun elevation sweep +14° through −18°, not a time series |
| `Images/NightSkyMoonField.png` | Moon at its angular size and Hipparcos stars |
| `Images/NightSkyCatalogueMilkyWay.png` | Catalogue field, unchanged |

`Results.txt` and `Results/NightSky-sanitized.txt` record the 42/42 NightSky exhibit checks.
`Results/Solar-{release,sanitized}.txt` records the additional
finite-pixel and actual-reference-renderer tests, run on Linux x86-64 with GCC 12.2 at base `0eb6d76` plus this
change. The solar filter and the two changed reference source files also compile with `-Wall -Wextra -Werror`.
The release reference build passes five CTests, including the existing lighting and complete-renderer regressions.

| Solar check | Observed result | Scope |
|---|---:|---|
| Disc flux, radius ≤ 3 pixels | max relative error `1.36e-10` | Three subpixel offsets, Gaussian footprint |
| Disc flux, transition/resolved up to 32 pixels | max relative error `7.04e-5` | Four-by-four resolved pixel integral |
| Independent Cartesian integral | max error `0.00311 × peak` | 600×600 source quadrature, three small-disc radii |
| Blended path continuity | `4.84e-7 × peak` | Radius change of `1e-9 rad` near four pixels |
| Full reference-renderer solar flux | max relative error `4.39e-4` | Seven cameras, square/wide and subpixel/telephoto |
| Simultaneous flares | both signals nonzero; additive residual `1.05e-16` | Warm and anamorphic terms in the actual reference renderer |
| Fully hidden source | max image difference `0` | Opaque foreground and fully planet-occluded Sun |

Existing checks include:

- Phase solid-angle integral = 1; zero-asymmetry Cornette–Shanks = Rayleigh.
- sRGB 128 → linear 0.2158605; black/white interpolation → linear 0.5.
- Warm streak vs the independent demo formula; additive mixing, rotation and partial/zero visibility.
- Existing Mie extinction, solar geometry, near-ideal disc flux, limb ratio and six diffraction spikes.
- Existing lunar phase area, Lommel–Seeliger full-disc flatness, libration and named-feature orientation.
- Environment regression: 73 checks; retained in `../VehiclePhysics/Results/EnvironmentRegression.txt`.

## Independent ephemeris comparison

`Results/Ephemeris-{release,sanitized}.txt` compares 192 dates and times across 2026 at five WGS84 locations
(Benoni, Greenwich, equator and both poles), using Astropy/jplephem with the **JPL DE421 kernel**, not ERFA's
built-in Moon model, which shares the Meeus basis. The kernel is downloaded only by the pinned Python dependency,
not committed; SHA-256 `a20a7139da04cbc462454634918e9a9ca69127044e2cc9d4f9c16e238d2deedc` is checked at runtime.
Reference directions are apparent topocentric with refraction disabled; Frontier directions are mean-geometric.
TT−UTC is fixed to 69.184 s for 2026 and UTC approximates UT1. This is an independent **model** comparison,
not a telescope measurement.

| 960 comparisons | Maximum deviation from DE421 | Before |
|---|---:|---:|
| Moon direction, topocentric | `0.003118°` | `2.912255°` (two-term geocentric exhibit) |
| Moon direction at Benoni | `0.003063°` | `2.758499°` |
| Sun direction, topocentric | `0.012497°` | unchanged |
| Moon distance / angular radius | `47.740 km` / `0.113723 arcsec` | not previously computed |
| Geometric illuminated fraction | `0.000102` | mean-age model `0.085448` maximum gap |

The benchmark also reproduces Meeus example 47.a and rejects invalid inputs. It checks five selected places and
2026 dates, not the entire supported time range. The new ephemeris takes TT and UT1 explicitly and rejects dates
outside approximately 1500–2500; the exhibit's own 2026 conversion is not a general UTC-to-TT implementation.
The `MoonPhase` mean-age API is unchanged, so existing consumers retain its larger error; callers wanting
geometric phase can use `MoonGeometricIllumination`. The new position series adapts Sonia Keys' MIT-licensed
implementation; notice and license are retained in `Documentation/SourceNotices.md`.

## What these checks do not prove

- No comparison to calibrated sky photographs, measured lens PSFs, measured tyre data or absolute lunar photometry.
- The small-disc path integrates a Gaussian pixel footprint analytically and uses 32×48 polar source samples;
  it is compared with a finer Cartesian integral of the **same model**, not a measured lens. Between radii of three
  and six pixels it blends into a radially normalized erfc approximation, not an exact resolved-disc convolution.
  Pixel shear is ignored and a footprint-dependent minimum blur is imposed. Planet occultation suppresses a fully
  hidden Sun, but partial clipping and atmospheric refraction at the geometric limb are not modelled.
- Flare is a parameterized camera effect, not optical ray tracing through a measured lens prescription. Screen-space
  visibility is coarse and can miss subpixel occluders; clouds use a single sampled Sun ray, not a full optical solve.
- Multiple scattering remains the bounded factored approximation, not the full demo's multiple-scattering LUT.
- Moon direction now includes topocentric parallax, but nutation, diurnal aberration, refraction, terrain and
  topocentric libration are still approximate or absent. Phase darkening and earthshine are artistic approximations;
  there is no eclipse, relief, Hapke fit or Moon illumination of scene geometry. The texture is used by the separate
  NightSky exhibit, not the reference-renderer sky path. Source-map licensing requires review.
- No changes to clouds. No claim of GPU, physical hardware, Windows/editor or real-time acceptance.
