# Project-Zero integration: showcase scene (default)

Opening the Project-Zero executable renders the showcase: a soil plain
scattered with one hundred analytical shapes (boxes, spheres, cones,
cylinders, pyramids, tetrahedra, wedges — near and far, one unique
material + colour each), under a sunset sky with full moon, stars,
broken cirrus, and marching ground mist. The Cornell box and the
courtyard scenes are deleted; this is the only scene, and it is the
speed-test bed. Everything is lit strictly by ReSTIR DI + ReSTIR GI,
nothing else.

## Apply (engine checkout)

From a fresh checkout of `SultanAladin/Frontier-` at engine `main`
(shallow `f17fb6f`, 2026-09-14 — the same base the patch was generated
and apply-checked against):

```sh
git apply PZIntegration/0001-project-zero-showcase.patch   # this file ships here
```

It touches 11 files: `CMakeLists.txt` (the Project-Zero target gains
the integrator sources — without this the target does not link), 3 new
shaders (`Shaders/SlangInterchange.h`, `Shaders/SkySpecification.slang`,
`Shaders/FogSpecification.slang`), 2 new sources
(`Source/SkyFogIntegrator.h/.cpp`), 5 rewritten sources
(`RayTracingSolver.h/.cpp`, `RendererHost.h/.cpp`,
`GameExecution.cpp`). No Makefile change: the `Source/*.cpp` wildcard
picks the integrator up. The `.slang` files compile as C++ through the
prelude (their Slang validity is untouched, so a future GPU path can
compile the same sources with `slangc`).

## Build and run

Linux (`make`, the engine's own flags `-O3 -Wall -Wextra -Werror`):

```sh
cd Projects/Project-Zero && make
./bin/Project-Zero                                   # sunset showcase, 640x480
./bin/Project-Zero --sun 18.3                        # moonlit night + stars
./bin/Project-Zero --fog clear --yaw 40              # thin veil, look around
./bin/Project-Zero --width 1280 --height 720 --bounce 4 --passes 1   # speed test
```

Windows (cmake + MSVC, Developer Command Prompt or VS Code CMake Tools):

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Project-Zero
.\build\Release\Project-Zero.exe
```

Flags: `--sun H` (any hour; below-horizon sun is clamped so night
works), `--yaw D` `--pitch D` (aim; default `0 2`), `--fog
clear|morning|backlit`, `--width W` `--height H`, `--bounce N` (GI
candidates per pixel, default 8), `--passes N` (spatial reuse passes,
default 2), `--help`. Output lands in `./Diagnostics/`:
`ProjectZero_Showcase.ppm` always, plus `.png` when a Python
interpreter is found. The PPM is byte-identical run to run
(seeded hashes; determinism is verified, not assumed).

## Architecture

- `Source/SkyFogIntegrator` owns the single showcase atmosphere. It
  compiles the shipped `.slang` core as C++ in one translation unit
  (no ODR risk: the header never names `.slang` types) and exposes
  sun/moon direction + radiance, sky radiance, the fog march, aerial
  perspective, and the panel post chain. The solar ephemeris and the
  panel defaults are verbatim copies of the gated harness sources,
  cited in-file; the showcase staging (sun hour 17.93, azimuth −60°,
  moon in the northern sky, broken cirrus, boosted stars) only stages
  the mirror algorithm's inputs, never its code.
- Frame convention: the engine world is Z-up, the render core is Y-up
  (panel frame). `RenderFromWorld` / `WorldFromRender` rotate between
  them. All sky/fog math runs in the render frame; only ray endpoints
  and light vectors cross the boundary.
- `RayTracingSolver::ConstructShowcaseScene` builds the field
  deterministically (`mt19937(2026)`): 100 shapes in three distance
  bands over a 1 km soil disc, golden-ratio hue per object, plus a
  median-split BVH (leaf ≤ 4, iterative stack traversal) so the
  speed test measures shading, not triangle loops.
- `RenderShowcaseFrame` keeps the six-phase structure: visibility,
  direct sun *and moon* illumination with shadow segment tests (both
  directional, no new sampler types), ReSTIR GI candidates (bounce
  misses contribute sky ambient), spatial reuse with Jacobian shift
  (clamped ≤ 10, M-cap 20×), the bilateral filter, then composition:
  surface + aerial perspective, fog march along the primary ray, sky
  for misses. The GI estimator shades true reservoir estimates
  (kept radiance × W / kept target weight, ratio-clamped ≤ 8 —
  this plus the previously missing primary-albedo multiply is what
  killed the fireflies and the wash), and the fog march lights its
  single-scatter term from the luminance-winning luminaire, so night
  mist is moonlit automatically.
- Below-horizon miss rays sample the horizon zenith rather than the
  panel underground (distant haze, kills a black horizon seam), and
  the showcase exports through the panel post chain.

## Verification record (sandbox, g++ 12)

- `make` clean under `-Wall -Wextra -Werror -pedantic`; the cmake
  target lists every source explicitly for MSVC.
- Default 640×480: 6.0 s at `-O1`, 4.4 s at `-O3`. Night 320×240:
  1.4 s. Renders are single-threaded CPU path tracing; scale by
  `--width/--height/--bounce/--passes` for the speed test.
- Determinism: repeated runs are byte-identical, and three
  independent builds (dev `-O1`, patched-pristine-tree `-O1`, engine
  `make -O3`) produce the same sha256
  (`f59ef151…ebd1` for the default 320×240 frame).
- Sky path matches the gated harness within 3 LDR after the identical
  post chain (unchanged from the courtyard proof; the sky algorithm
  is untouched, only its staging inputs moved).
- Gates: `RunCelestialParity.py` OVERALL PASS (moon/cloud/stars ship
  present-but-off in the mirror configuration),
  `RunFogParity.py` OVERALL PASS (F1 0.001, F2 0.806 over 2000 rows).
- Proofs: `PZIntegration/showcase_{sunset,night}.png`.

## Notes and limits

- The vendored shader sources are byte-identical to this repo's
  `Integration/Shaders/` (sha256: `SlangInterchange.h 2945beda…`,
  `SkySpecification.slang 3c29aa2a…`, `FogSpecification.slang
  a33588ce…`). Re-vendor by copying the three files over
  `Projects/Project-Zero/Shaders/` in the engine checkout.
- The moon, clouds, and star boost are showcase staging with no
  panel source (new features, default-off in the mirror gates).
  Placement/density are artist tuning, not physics errors — the
  F1/F2 gates pin the fog march math, the celestial gate pins the sky.
- Lens flare is intentionally absent (the fake flare was deleted per
  directive during the mirror phase).
- Engine `.comp` shaders remain GLSL stubs; this integration runs on
  the engine's CPU path (the only live renderer). The `.slang` files
  it vendors are the GPU-ready sources for the eventual `slangc`
  step.
- History: the courtyard integration (`0001-courtyard-sky-fog.patch`,
  commit `bc83b52`) is superseded by this patch and removed; its
  morning/clear/backlit proofs stay in `Diagnostics/Proof/`.
