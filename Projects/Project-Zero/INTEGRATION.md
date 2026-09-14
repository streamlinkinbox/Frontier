# Project-Zero integration: courtyard sun + sky + fog

The celestial sky mirror and the atmospheric/local fog research in this
directory are not harness-only: they run inside Project Zero. The engine
checkout gains an outdoor courtyard (soil + block) rendered through the
existing ReSTIR DI + ReSTIR GI pipeline, lit by the gated sun, skinned
with the gated sky, and marched with the gated fog volumes.

## Apply (engine checkout)

From a checkout of `SultanAladin/Frontier-`:

```sh
git apply PZIntegration/0001-courtyard-sky-fog.patch   # this file ships here
cd Projects/Project-Zero
make clean && make
./bin/Project-Zero --courtyard                        # morning mist, 06:24 sun
./bin/Project-Zero --courtyard --fog clear            # thin veil only
./bin/Project-Zero --courtyard --fog backlit --yaw 75 --pitch 4
./bin/Project-Zero                                    # Cornell path, unchanged
```

The patch was generated against engine `main` (shallow `f17fb6f`,
2026-09-14) and apply-checked against a fresh clone. It touches 10
files: 5 new (`Shaders/SlangInterchange.h`,
`Shaders/SkySpecification.slang`, `Shaders/FogSpecification.slang`,
`Source/SkyFogIntegrator.h/.cpp`), 5 minimal edits
(`RayTracingSolver.h/.cpp`, `RendererHost.h/.cpp`,
`GameExecution.cpp`). No Makefile change: the `Source/*.cpp` wildcard
picks the integrator up, and the `.slang` files compile as C++ through
the prelude (their Slang validity is untouched, so a future GPU path
can compile the same sources with `slangc`).

Always `make clean` after pulling this patch: the engine Makefile has
no header dependencies, and a stale `RendererHost.o` mixed with the new
header corrupts the heap at exit (found the hard way, see §5).

## Architecture

- `Source/SkyFogIntegrator` owns the single courtyard atmosphere. It
  compiles the shipped `.slang` core as C++ in one translation unit
  (no ODR risk: the header never names `.slang` types) and exposes
  sun direction/radiance, sky radiance, fog march, aerial perspective,
  and the panel post chain. The solar ephemeris, the 60-field panel
  defaults, and the three fog scenarios are verbatim copies of the
  gated harness sources, cited in-file.
- Frame convention: the engine world is Z-up, the render core is Y-up
  (panel frame). `RenderFromWorld` / `WorldFromRender` rotate between
  them (determinant +1: X east stays, world up becomes render up,
  world north becomes render −Z). All sky/fog math runs in the render
  frame; only ray endpoints and the sun vector cross the boundary.
- `RenderCourtyardFrame` mirrors the six Cornell phases: visibility,
  direct *sun* illumination with shadow segment tests (no area-light
  sampling; the sun is directional), ReSTIR GI candidate tracing
  (bounce misses contribute sky ambient), the unchanged spatial
  reuse + bilateral filter (thresholds retuned to courtyard metres),
  then composition: surface + aerial perspective, fog march along the
  primary ray, sky for misses. No new sampler type is introduced: fog
  applies at composition, lit by the same sun the reservoirs use, so
  the ReSTIR-only rule holds. Volumetric ReSTIR (reservoir-resampled
  scattering) is documented future work, not this patch.
- Below-horizon miss rays sample the horizon zenith rather than the
  panel underground (distant haze, kills a black horizon seam), and
  the courtyard exports through the panel post chain
  (`ExportCourtyardImage`) while Cornell keeps its ACES path.

## Verification record (sandbox, g++ 12, `-O3`)

- `make` clean under the engine's `-Wall -Wextra -Werror -pedantic`;
  courtyard 640×480 renders in ~2.4 s, exit code 0 both paths.
- Determinism: two morning runs are byte-identical.
- Sky path matches the gated harness: same view direction gives
  PZ `(156,149,123)` vs harness `(159,152,125)` (top-center) and
  `(179,170,138)` vs `(181,172,139)` (mid-sky) — within 3 LDR after
  the identical post chain. Surfaces differ only by PZ's ReSTIR GI
  (~1.3× the harness ambient-approx radiance), which is expected:
  the harness has no bounce light.
- Cornell path has no regression: still the single flat colour
  `(33,33,44)` — that flat frame is pre-existing (the Y-up Cornell
  room never intersected the Z-up camera frustum), not caused here.
- Proofs: `Diagnostics/Proof/pz_courtyard_{morning,clear,backlit75}.png`.

## Notes and limits

- The vendored shader sources are byte-identical to this repo's
  `Integration/Shaders/` at the time of writing (sha256:
  `SlangInterchange.h 2945beda…`, `SkySpecification.slang 91075e7f…`,
  `FogSpecification.slang 837d78a0…`). Re-vendor by copying the three
  files over `Projects/Project-Zero/Shaders/` in the engine checkout.
- The local fog sphere is subtle from the default courtyard camera
  (+4…16 LDR over a bright background); it reads best framed against
  the aureole (`--yaw 75 --pitch 4`). Placement/density are artist
  tuning, not physics errors — the F1/F2 gates pin the march math.
- Lens flare is intentionally absent (the fake flare was deleted per
  directive during the mirror phase); clouds are not present in the
  celestial reference v1, so there is nothing to mirror yet.
- Engine `.comp` shaders remain GLSL stubs; this integration runs on
  the engine's CPU path (the only live renderer). The `.slang` files
  it vendors are the GPU-ready sources for the eventual `slangc`
  step.
