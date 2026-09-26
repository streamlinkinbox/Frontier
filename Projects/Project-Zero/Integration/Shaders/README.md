# Project Zero shaders — celestial sky + fog + ReSTIR, strictly `.slang`

The only lighting in Project Zero is ReSTIR (direct + indirect) over the
celestial sky. There is no ambient term, no analytic sun lamp, no legacy path:
every shaded pixel is either a sky backdrop (display, not lighting) or a
ReSTIR estimate. The sky is the celestial panel's sun + atmosphere, line by
line — Mie scattering, dawn/dusk/twilight band, sun disc + aureole, planet
ground — with the panel's GLSL line numbers cited at every function. Fog
(atmospheric + local volumetric) is researched original work in the same
dual-compile style; see `../../Reviews/Fog-Research-2026-09-14.md`.

Reference: `docs/celestial/index.html` on branch `ref/celestial-html`.
Engine drop-in guide: `../INTEGRATION.md`.

## Files

- `SkySpecification.slang` — the panel's sky + sun as pure functions
  (`AtmosphereCompute` 20×8, `DawnGlowCompute`, `SunDiscCompute`,
  `SunAureoleCompute`, `SunGroundCompute`, `SkyFieldCompute`,
  `SkyRadianceCompute`, `StarFieldCompute`, `StarPixelCompute`,
  `SkyMediaApply`, `SkyAmbientCompute`, `SkyPixelCompute`, `SkyPostApply`,
  viewport rays).
- `FogSpecification.slang` — atmospheric fog for surfaces
  (`FogAtmosphereApply`, the gated sky-media math) + local volumetric fog
  (`FogDensityQuery`, `FogMediumQuery`, `FogMarchVolumes` with closed-form
  slabs, analytic box/sphere volumes, integer-hash noise), lit by the
  winning ReSTIR DI sample.
- `ReSTIRSequence.slang` — deterministic RNG, candidate draws, DI/GI
  reservoirs with RIS update/combine (dual region), then the Slang-only shell:
  uniform buffers, visibility/reservoir descriptors, TLAS ray queries, and
  the 9 compute entries (`SkyAmbient`, `DirectInitial`, `DirectTemporal`,
  `DirectSpatial`, `IndirectInitial`, `IndirectTemporal`, `IndirectSpatial`,
  `PixelShade`, `SkyViewport`).
- `SlangInterchange.h` — C++ dual header (vector arithmetic, builtins).

## Dual-compile contract

The dual region compiles as **C++17** (the Host harness in `../../Host/`, via
`SlangInterchange.h`) **and** as **Slang** (the Vulkan engine) from the same
text. `../../Diagnostics/CheckCelestialSlang.py` enforces the contract; the
rules:

- pure functions of their arguments (no globals, textures, ray queries);
- multi-value returns via small structs built by field assignment —
  no `&`, no `out`/`inout`, no `{...}` struct literals;
- struct parameters use the `in` modifier (input in Slang, empty in C++);
- `uint`/`int` only (no fixed-width aliases), leading-zero float literals,
  float32 only, compute-only (no samplers/derivatives).

## ReSTIR design (what the shell does)

- **DI candidates**: deterministic mixture over sun-cone ×2 / aureole-lobe /
  sphere. Every candidate evaluates THE sky (`SkyRadianceCompute`: disc +
  aureole + atmosphere + glow + stars + media) and is weighted by the balance
  heuristic against the mixture pdf
  (½ cone + ¼ lobe + ¼ sphere). Per-stratum targets without the mixture
  denominator would bias the estimate; the convergence gate proves the fix.
- **Targets**: `lum(emission·albedo·cos/π)` (DI), `lum(throughput·emission)`
  (GI), D65 weights. Visibility is retraced after every reuse pass and applied
  at shade time, never baked into a target.
- **Reuse**: temporal (motion-vector reprojection, same guards) + 4-tap
  Poisson-disc spatial, geometric guards (10% depth, normal·normal > 0.9),
  M-cap 20. DI combine re-evaluates the target at the receiver (exact across
  materials); GI reuse is exact on uniform surfaces and M-cap-bounded
  elsewhere (standard ReSTIR-GI practice, see the harness report).
- **GI candidates**: cosine bounces, one explicit vertex, then the sky;
  second-vertex paths that re-hit terminate with zero emission.
- **Determinism**: PCG-seeded LCG streams, frame/pixel/tag addressed; the same
  seed produces bit-identical reservoirs and frames (T4).

## Proof (all green, see `../../Diagnostics/`)

- `CheckCelestialSlang.py` — static contract rules (3 files).
- `RunCelestialParity.py` — G1: core vs the oracle (`Tools/SkyReference/`)
  full-frame incl. ground and sun disc (max 1 LSB); G2: core vs the vendored
  upstream C++ transcription, media-free sky mask (max 2 LSB: 1 star-edge
  pixel); G3: media vs numpy over 36 000 seeded rows.
- `RunFogParity.py` — F1: fog density vs numpy; F2: volume march vs numpy;
  F3: outdoor proof renders + determinism.
- `ReSTIRConvergence` — T2/T3: DI/GI estimates vs independent brute-force
  references (own RNG/sampling) on an analytic corner scene; T4: determinism.

## Vulkan integration

Build with Slang targeting SPIR-V, IEEE-precise float math (no reassociation:
the parity proofs assume panel-exact operation order):

```
slangc ReSTIRSequence.slang -target spirv -o ReSTIRSequence.spv \
  -entry SkyAmbient -entry DirectInitial -entry DirectTemporal \
  -entry DirectSpatial -entry IndirectInitial -entry IndirectTemporal \
  -entry IndirectSpatial -entry PixelShade -entry SkyViewport
```

Descriptors (HLSL registers; Slang maps them to Vulkan bindings):

| register | object | contents |
|---|---|---|
| b0 `SkyExchange` | 18×float4 | sun dir/color+elev, sun params, atmosphere A/B, sky tint, ground, post A/B, misc, media A-E, stars A-C |
| b1 `SkySequenceConfiguration` | 8×uint | width, height, temporal index, initial candidates, reservoir cap, spatial taps, seed, pad |
| b2 `SkyViewExchange` | 4×float4 | world-frame camera basis + tanHalf (all entries: star pixel-angle) |
| t0 | `VisibilityStructure[]` | pos, normal, albedo, depth (≤0 = background, pos = ray dir), motion |
| t1 | TLAS | engine acceleration structure |
| t2/t3/t4 | positions/normals/indices | merged mesh buffers (float3 / float3 / uint) |
| t5 | `InstanceStructure[]` | per-instance vertex/index bases |
| t6/t7 | DI/GI reservoirs (read) | previous frame |
| u0/u1 | DI/GI reservoirs (write) | current frame (ping-pong with t6/t7 on the host) |
| u2 | float4[] | LDR output |
| u3 | float3[1] | sky-ambient probe (written by `SkyAmbient`, read by all entries) |

Per frame the host: fills b0/b1/b2, dispatches `SkyAmbient` (1 thread,
barrier on u3), then
`DirectInitial → DirectTemporal → DirectSpatial → IndirectInitial → IndirectTemporal → IndirectSpatial →
PixelShade` (each a full-screen 8×8 compute dispatch with UAV barriers between),
swapping the reservoir bindings each frame. `SkyViewport` is the standalone
reference viewport (no visibility/TLAS needed) used to reproduce the parity
proofs on the GPU.

One API surface to verify with `slangc` on the first GPU build (no Slang
toolchain exists in this sandbox): the `RayQuery` spelling
(`TraceRayInline` flags+mask overload, `Proceed`/`CandidateType`/
`CommitNonOpaqueTriangleHit`, `CommittedInstanceID/PrimitiveIndex/RayT/
TriangleBarycentrics`) follows the DXR names Slang mirrors; if the installed
Slang revision spells any of them differently, only `TraceBounceCompute` /
`TraceOccludedQuery` need touching — the dual region is unaffected.
