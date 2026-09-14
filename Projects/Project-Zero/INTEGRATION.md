# Project-Zero integration: showcase scene (default)

Opening the Project-Zero executable renders the showcase: a soil plain
scattered with one hundred analytical shapes (boxes, spheres, cones,
cylinders, pyramids, tetrahedra, wedges — near and far, one unique
material + colour each), under a sunset sky with full moon, stars,
broken cirrus, and marching ground mist — plus camera lens flare,
halo, and an anamorphic streak, all enabled by default. The Cornell
box and the courtyard scenes are deleted; this is the only scene, and
it is the speed-test bed. Everything is lit strictly by ReSTIR DI +
ReSTIR GI, nothing else; the flare is a post-process camera artifact,
not a light.

## Apply (engine checkout)

From a fresh checkout of `SultanAladin/Frontier-` at engine `main`
(shallow `f17fb6f`, 2026-09-14 — the same base the patch was generated
and apply-checked against):

```sh
git apply PZIntegration/0001-project-zero-showcase.patch   # this file ships here
```

It touches 12 files: `CMakeLists.txt` (the Project-Zero target gains
the integrator sources — without this the target does not link), 4 new
shaders (`Shaders/SlangInterchange.h`, `Shaders/SkySpecification.slang`,
`Shaders/FogSpecification.slang`, `Shaders/FlareSpecification.slang`),
2 new sources (`Source/SkyFogIntegrator.h/.cpp`), 5 rewritten sources
(`RayTracingSolver.h/.cpp`, `RendererHost.h/.cpp`,
`GameExecution.cpp`). No Makefile change: the `Source/*.cpp` wildcard
picks the integrator up. The `.slang` files compile as C++ through the
prelude (their Slang validity is untouched, so a future GPU path can
compile the same sources with `slangc`).

## Build and run

Linux (`make`, the engine's own flags `-O3 -Wall -Wextra -Werror`):

```sh
cd Projects/Project-Zero && make
./bin/Project-Zero                                   # sunset showcase facing the sun, 640x480
./bin/Project-Zero --sun 18.3 --yaw 0 --pitch 2      # moonlit night + stars + moon
./bin/Project-Zero --fog clear --yaw 40 --pitch 2    # thin veil, look around
./bin/Project-Zero --width 1280 --height 720 --bounce 4 --passes 1   # speed test
```

Windows (double-click build, no CMake needed — MSVC only):

```bat
Projects\Project-Zero\Construct.bat            :: builds bin\Project-Zero.exe, opens the live window
Projects\Project-Zero\Construct.bat -Rebuild -Run
```

`Construct.bat` forwards to `Build/Construct.ps1`, the per-project
direct `cl.exe`/`link.exe` driver (same convention as Project-F20):
`/std:c++20 /O2 /W4 /WX`, engine objects reused unless stale, links
`user32.lib` + `gdi32.lib` (window + frame presenter). CMake works
too (`cmake --build build --config Release --target Project-Zero`,
or open `build/FrontierEngine.sln` in the VS IDE and run the
`Project-Zero` target) — the patch keeps `CMakeLists.txt` listing
every source explicitly.

The Windows `.exe` is windowed by default and opens facing the
sunset, so the sun-only flare/halo/streak is visible on launch. The
loop mirrors the Project-Zero main-loop pattern (window → input →
Control Centre → fly camera → render → present, Δτ clamped at 0.1 s)
on this tree's native pieces: `WindowExchange` (same host
Project-F20 uses — no GLFW install needed; the engine's
`FRONTIER_ENABLE_GLFW` path stays off), `InputExchange`, and the
`FRONTIER_DEVELOPMENT` `ControlCentrePanel` notch (left-drag it;
the camera looks with right-drag, so they never fight). One full
ReSTIR frame costs seconds, so the render runs on a worker thread
while the loop pumps messages — the window stays live (move,
resize, fly keys) and frames stream in as they finish; resizing
re-renders at the new size. Fly with `WASD` + `Q`/`E`, hold right
mouse + drag to look (the patch feeds `WM_MOUSEMOVE` into the
existing `InputExchange` delta tracker — the only shared-engine
change), scroll for flight speed, `Shift` for boost, `ESC` closes.
The cached frame re-renders only while the camera moves. `--headless`
keeps the old render-to-file behaviour (Linux always runs headless).
Note: the pattern branch's ImGui panels ride its Vulkan backend,
which this tree does not have, so there is deliberately no ImGui
here — the window shows exactly the proof pixels through GDI.

Flags: `--sun H` (any hour; below-horizon sun is clamped so night
works), `--yaw D` `--pitch D` (aim; default `0 2`), `--fog
clear|morning|backlit`, `--width W` `--height H`, `--bounce N` (GI
candidates per pixel, default 8), `--passes N` (spatial reuse passes,
default 2), `--flare 0|1` (lens flare, default 1), `--flarevar
0|1|2|3` (cinematic/anamorphic/starburst/halo, default 0),
`--window | --headless`, `--help` (defaults face the sunset:
`--yaw 220 --pitch -2`; the moon view is `--yaw 0 --pitch 2`).
Output lands in `./Diagnostics/`:
`ProjectZero_Showcase.ppm` always (every window session exports its
final frame too), plus `.png` when a Python interpreter is found
(off-Windows only; on Windows the window is the viewer). The PPM is
byte-identical run to run (seeded hashes; determinism is verified,
not assumed).

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
- Phase 7 applies the panel `post` lens flare mirror in linear HDR
  before the post chain. `FlareSpecification.slang` transcribes the
  reference `lensFlare` term by term (8-slot chromatic ghosts, the
  chromatic halo ring, the blue streak, starburst spikes, the hot
  core, `FlareVariety` weights 0 cinematic / 1 anamorphic /
  2 starburst / 3 halo) with the panel's own defaults (ghosts 5,
  halo 0.55, streak 0.8, chroma 0.65). The driver
  (`RendererHost::ApplyLensFlare`) reproduces `AddLensFlare`'s
  projection + visibility + kelvin-colour × intensity × 0.09 scale
  exactly. Flare is sun-only, like the panel: the moon and
  stars never carry flare, and a sun behind the camera (the default
  sunset view) or below the horizon ramp contributes nothing — night
  frames are flare-free. Face the sun (`--yaw 220 --pitch -2`) to
  see it; `--flarevar` selects the variety (default 0).

## Verification record (sandbox, g++ 12)

- `make` clean under `-Wall -Wextra -Werror -pedantic`; the cmake
  target lists every source explicitly for MSVC. All showcase
  translation units are additionally clean under GCC
  `-Wconversion -Wsign-conversion -Wsign-compare -Wdouble-promotion`
  (the closest local stand-in for MSVC `/W4 /WX`, which the
  `Construct` drivers enforce) — 13 sign-conversion sites fixed with
  behaviour-preserving casts, byte-identical output verified after.
- Default 640×480: 6.0 s at `-O1`, 4.4 s at `-O3`. Night 320×240:
  1.4 s. Renders are single-threaded CPU path tracing; scale by
  `--width/--height/--bounce/--passes` for the speed test.
- Determinism: repeated runs are byte-identical, and three
  independent builds (dev `-O1`, patched-pristine-tree `-O1`, engine
  `make -O3`) produce the same sha256
  (`bbcb710c…e5b` for the default 320×240 frame, which now faces the
  sunset with the sun flare in frame).
- Sky path matches the gated harness within 3 LDR after the identical
  post chain (unchanged from the courtyard proof; the sky algorithm
  is untouched, only its staging inputs moved).
- Gates: `RunCelestialParity.py` OVERALL PASS (moon/cloud/stars ship
  present-but-off in the mirror configuration),
  `RunFogParity.py` OVERALL PASS (F1 0.001, F2 0.806 over 2000 rows).
- Proofs: `PZIntegration/showcase_{sunset,night,anamorphic}.png`
  (default sun-facing view, moon night view, anamorphic variety).

## Notes and limits

- The vendored shader sources are byte-identical to this repo's
  `Integration/Shaders/` (sha256: `SlangInterchange.h 2945beda…`,
  `SkySpecification.slang 3c29aa2a…`, `FogSpecification.slang
  a33588ce…`, `FlareSpecification.slang c63662c4…`). Re-vendor by
  copying the four files over `Projects/Project-Zero/Shaders/` in the
  engine checkout.
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
