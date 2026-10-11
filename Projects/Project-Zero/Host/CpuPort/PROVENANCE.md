# CpuPort provenance

Verbatim upstream C++ transcription of the celestial panel, used as the
**second truth** for the parity gate (the first truth is the oracle in
`Tools/SkyReference/`, transcribed independently from the same panel).

- Upstream branch: `ref/cxx-port` (`arena/01a09644-frontier`)
- Upstream commit: `6a2c1e40a8d7cbc45989465856d5ec2000d7e8e8`
- Files (unmodified):
  - `CelestialSpecification.h`
    `sha256:4e04aa54deda46cc68f50b255e2c44696261f64f99a8162c500874b45f0d1978`
  - `CelestialIntegrator.h`
    `sha256:c598d1c1d70691c9c4ea4d65d38dd489dee6cc791f13604c40fd92f5c39e6b01`
  - `CelestialIntegrator.cpp`
    `sha256:1cdc0f29e1a8eb9a63cc01d3a3776617dc2042457235fc8ddadae83c4741cded`
- Not vendored: `CelestialStage` (the room renderer — superseded by ReSTIR),
  `DeviceExchange/OrientationClassifier` (unused by the integrator),
  solvers, textures.

`CpuPortDiff.cpp` (ours, next to this file) drives the port with every system
except sun + sky + post disabled, and aspect-corrected display coordinates so
the port's own vignette formula reproduces the panel's to the last ulp.

## Known upstream omission (verified 2026-09-14, not patched — vendored files stay verbatim)

`CelestialIntegrator.cpp` `SampleSkyRadiance` (~lines 1969-2090) implements
only the reference `main()` else-branch (sky): `col = Sky+Glow`, then
stars/moons/clouds/disc/aureole/media. The planet branch (`Ground > 0.5`,
reference `main()` if-branch, with its `+ sky` and `+ vec3(.002,.003,.006)`
terms) exists in the integrator only inside the environment/bounce sampler
(~line 2261), which the beauty path never calls. Measured effect: on
below-horizon rays the port returns sky-only radiance, differing from the
panel (and from `CelestialCore.slang`) by up to ~48 LSB. The parity gate
(`Diagnostics/RunCelestialParity.py`) therefore compares the port only where
the ray direction points above the limb (`dir.y > 0.02`); the ground is
covered full-frame by the oracle comparison instead. Sky rows agree at
MAE 0.000 / max 1 LSB, which validates the march, glow, disc, aureole, and
post chain across two independent transcriptions.

## Paths

`OrientationClassifier.{h,cpp}` live under `CpuPort/DeviceExchange/`
(upstream layout: `Core/DeviceExchange/`). The build provides an include shim
at `CpuPort/shim/a/b/` so the header's `../../../DeviceExchange/...` include
resolves to the vendored copy; see `Host/Makefile` (`CpuPortDiff` rule).

## Confirmed vendor bug in ApplyMedia (found 2026-09-14, not patched)

`CelestialIntegrator.cpp`, end of `ApplyMedia` (the `Lin` combine):

- Panel line 962: `vec3 Lin = La*(1.-Ta)*mix(1.,Tf,.5) + ...` — `(1.-Ta)`
  is PER-CHANNEL: `(1-Ta.r, 1-Ta.g, 1-Ta.b)`.
- Port: `La * (1.0f - Ta.x) * Mix(...)` — the RED channel's factor scales
  ALL THREE channels of the aerial-perspective in-scatter.

Since Ta.r > Ta.g > Ta.b (red transmits most), the port's G/B in-scatter
is too weak: with AF on, day skies come out up to ~46 LSB darker in G/B
than the panel (R exact), measured full-frame MAE ~20-29 LSB. The rest of
the port's media path (hRef/dS, beta, HG lobes, Ta, La, height-fog branch)
matches the panel. Because vendored files stay verbatim, the parity gate
(G2) runs the port with AF off and verifies the media FUNCTION separately
against an independent numpy transcription (G3 in RunCelestialParity.py).
