# Project Zero — celestial sun + sky + fog in `.slang`, lit strictly by ReSTIR

The celestial panel's sun and atmosphere (Mie scattering, dawn/dusk/twilight,
sun disc + aureole, planet ground), implemented in Slang from the panel's own
algorithms, plus researched atmospheric + local volumetric fog — all lit by
ReSTIR direct + indirect, nothing else.

## Layout

- `Integration/Shaders/` — the shipped code: `SkySpecification.slang` (sky +
  sun), `FogSpecification.slang` (atmospheric + local fog),
  `ReSTIRSequence.slang` (reservoirs + the 9 compute entries),
  `SlangInterchange.h` (dual-compile header). See `Integration/Shaders/README.md`
  for the contract and the Vulkan integration guide.
- `INTEGRATION.md` + `PZIntegration/0001-project-zero-showcase.patch` —
  the Project Zero integration: the patch makes the engine open on the
  100-object showcase scene (sunset sky, moon, stars, clouds, ground
  mist through ReSTIR DI + GI; verified: builds warning-free, runs,
  deterministic, matches the harness sky within 3 LDR).
  `Integration/Build/` holds port/format tooling.
- `Host/` — CPU harness that compiles the shipped `.slang` verbatim as C++17:
  `SkyViewport` (reference frames), `CpuPortDiff` (second-truth frames),
  `ReSTIRConvergence` (T2/T3/T4), `MediaProbe` (media sampler for G3),
  `SunPosition.h` (host ephemeris), `SlangCompat.h` (dual-compile prelude),
  `CpuPort/` (vendored upstream transcription + provenance).
  `SkyViewport` takes `--media 0|1` (media ablation) and `--fov` (crops).
- `Diagnostics/` — `CheckCelestialSlang.py` (static contract rules),
  `RunCelestialParity.py` (the sky gate), `RunFogParity.py` (the fog gate),
  `CelestialParity.txt` / `FogParity.txt` (last reports), `Proof/` (proof
  renders + diff maps).

## Build and run the gates

```sh
cd Host && make            # SkyViewport CpuPortDiff MediaProbe ReSTIRConvergence
cd ../Diagnostics && python3 RunCelestialParity.py   # everything (about 100 s)
```

`RunCelestialParity.py` renders the 10-frame matrix (line window, ignition,
dawn/day/sun-disc, night), checks it against the oracle (G1) and the upstream
port media-free (G2), verifies the media against numpy over 36 000 seeded rows
(G3), runs the static rules, and runs the ReSTIR convergence proof. Exit code
0 iff all green;
`--skip-restir` skips the slow part. Requirements: `g++` (C++17), `python3`
with PIL + numpy. No GPU, no Vulkan SDK, no Slang toolchain needed for any
gate — the one thing this sandbox cannot do is compile the Slang-only shell
(see `Integration/Shaders/README.md`, last section).

## Results (last full run)

- Parity: 10/10 frames vs the oracle at max 1 LSB full-frame (ground and
  sun disc included); 10/10 vs the upstream port media-free at max 2 LSB on
  the sky mask (1 star-edge pixel); media 36 000/36 000 rows vs numpy.
- ReSTIR: DI/GI estimates match independent brute-force references at all 3
  probes (floor, floor-left, wall); determinism bit-exact.
- Fog: F1 density vs numpy worst 0.001 of tolerance, F2 march vs numpy worst
  0.806 (both over 2000 seeded rows; the march now covers the global
  height-fog span, not just local volumes); F3 determinism
  byte-identical; proofs `Diagnostics/Proof/fog_{clear,morning,backlit}.png`,
  report `Diagnostics/FogParity.txt`.
  Research: `Reviews/Fog-Research-2026-09-14.md`.
- Integration: the engine patch (`PZIntegration/`) builds warning-free and
  opens Project Zero on the 100-object showcase (sunset + moon + mist,
  sun-only lens flare/halo/streak, `--sun 18.3` for a starry night);
  sky matches the harness within 3 LDR, runs are byte-identical.
  See `INTEGRATION.md`.
- Background: the review that specified this work is
  `Reviews/SunSky-Parity-2026-09-14.md`; the mirror proof answering the
  white-line question is `Reviews/SunSky-Mirror-Report-2026-09-14.md`.
