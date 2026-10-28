# Bounded initial candidates for replay GI

The Engine replay path can now evaluate **1–4 fresh path candidates** per receiver before temporal and spatial
reservoir reuse. The existing one-candidate path remains the default. This is a bounded software triangle-BVH
fixture, not completed ReSTIR GI, hardware RT, a non-ray-traced GI implementation, or an editor integration.

## Contract

- `LightComputeFootprint::Sampling[1]` selects the fresh count: `1–4`; legacy `0` also means one. The recorder
  rejects larger counts before issuing Vulkan commands. The existing ABI and output allocation are unchanged.
- The initial reservoir samples path seeds using the positive radiance target and retains its represented count.
  Temporal candidates are replayed in the current scene; spatial donors are replayed at the current receiver.
  Four initial candidates mean four fresh `EvaluateReplayPath` calls, in addition to any replay for reuse.
- `PathReplayHistorySpecification::InitialCandidates` uses the canonical range `1–4`. Changing it is a sampling
  revision: the existing heuristic caps old reservoir history at one, and the exhibit resets accumulated colour.
  Caller-owned histories, allocation changes and submission failures still require explicit lifecycle handling.
- The floor, history cap and re-tracing guards do not establish unbiased adaptive reuse, independent random
  streams, or correct visibility in a scene whose caller fails to report changes. Existing texture, BVH, material,
  motion and floating-point restrictions remain. The audit-only tail diagnostic still requires one candidate.

## Independent occlusion and multibounce checks

The half-blocked one-bounce fixture compares 32 batches × 1,024 receiver outputs per setting with an independent
`1024 × 1024` emitter-area integral. Sample standard errors use finite-sample correction; the reported per-output
standard deviations are not equal-time comparisons.

| Paths/output | Area ref   | GPU mean   | Batch SE   | Pixel SD   | SD ratio |
| -----------: | ---------: | ---------: | ---------: | ---------: | -------: |
| 1            | `0.119909` | `0.119568` | `0.001740` | `0.324456` | `1.000`  |
| 2            | `0.119909` | `0.117872` | `0.001324` | `0.229095` | `0.706`  |
| 3            | `0.119909` | `0.118580` | `0.001334` | `0.188646` | `0.581`  |
| 4            | `0.119909` | `0.118913` | `0.001158` | `0.164710` | `0.508`  |

Four candidates do **four times the initial path evaluations** here. The lower per-output spread is not an
energy, cost-normalized convergence, physical-GPU performance or universal speedup claim. Batch uncertainty and
the numerical area tolerance are included in the acceptance test; finite random streams may be correlated
and prior rare-tail concerns are unresolved.

A coloured four-bounce room is compared against `400,000` independently seeded double-precision CPU paths per
open/covered scene, using brute-force triangle intersections rather than the GPU BVH. Each GPU mode has 32 batches
of 1,024 outputs. The four-candidate run therefore evaluates 131,072 fresh initial paths versus 32,768 for one.
The CPU and GPU share the bounded diffuse transport model; the CPU is not measured ground truth.

| Scene   | CPU red ± SE          | One red ± batch SE    | Four red ± batch SE   | Four/one RGB SD       |
| ------- | --------------------- | --------------------- | --------------------- | --------------------- |
| Open    | `0.837954 ± 0.003790` | `0.827906 ± 0.013078` | `0.831983 ± 0.004824` | `0.503, 0.501, 0.527` |
| Covered | `0.028061 ± 0.000444` | `0.028941 ± 0.001564` | `0.027251 ± 0.000793` | `0.481, 0.491, 0.514` |

The first fixture also reconstructs 4,090 individual selected seeds and weights on the host using analytic
rectangle intersections and a mirrored PCG sequence. Six boundary/selection-near-threshold cases are explicitly
excluded; the maximum weight and output discrepancy among compared paths is zero. That mirrored PRNG test checks
implementation agreement, not independent randomness. A separate host spatial merge matches all 1,024 GPU
outputs and 3,968 donor links in the identical-receiver fixture.

## Motion, history and regression checks

- Four-bounce parallel planes give open mean `0.625`; moving a blocker accepts 1,024 temporal histories and
  3,968 spatial links, yet produces black and matches the un-reconstructed output bit-for-bit. Reopening also
  matches raw. A four-to-two candidate change caps the old reservoir at one, resets colour, and yields represented
  count three; the following stable frame resumes colour and yields count five.
- A separate 32×32 textured rough-opaque fixture moves the camera by `0.07`, rotates an object by `0.12 rad` and
  changes material. Four candidates/four bounces exercise 288 textured pixels, 909 temporal accepts and 3,716
  spatial links. Colour accepts are zero on the change; the result matches the raw output bit-for-bit. This is
  a reset control, not dynamic colour reconstruction or numerical proof of unified PBR.
- The host reconstructs legacy-zero versus explicit-one output and 1,024 histories bit-for-bit. The previous
  `b0cf5c9` shader agrees with the new default for 20 rough-textured uniform/power and cosine/GGX images:
  `655,360` float components and eight history summaries match. That float comparison does not exercise mixture-on.
  Separately, 14 mixture-on moving-radiant PPMs match the older pre-recorder captures byte-for-byte and their RGB
  pixels match the committed PNGs; three reconstruction PNGs also match. Five release CTests pass.
- Engine history validation rejects canonical counts zero/five and preserves its previous domain; the recorder
  accepts legacy zero and counts 1–4 and rejects five before recording. The validation host rejects zero/five.
- Reconstruction resets by default on camera, geometry, lighting and material revisions. A later default-off
  [strict diffuse camera mode](ReplayCameraReprojection.md) has a narrow same-point exception. No arbitrary animated
  reconstruction, hardware-RT, physical-GPU, editor viewport or Windows validation is established.

## Reproduce

Prerequisites: pinned Slang, Vulkan headers and SwiftShader under `ExternalPackages/VulkanCompute`; CMake and a
C++20 compiler. The saved runs use GCC 12.2, Slang 2026.14.1 and SwiftShader CPU Vulkan (device type 4).
The sanitizer instruments the host only, not Slang-generated shaders or the external Vulkan driver.

```bash
PARITY=1 BUILD_JOBS=2 bash Exhibits/LightCompute/RunReplayCandidates.sh
SANITIZE=1 BUILD_JOBS=1 bash Exhibits/LightCompute/RunReplayCandidates.sh
```

[Release, including prior-shader parity](Results/ReplayCandidates-release.txt) ·
[Host ASan/UBSan](Results/ReplayCandidates-sanitized.txt) ·
[Reconstruction, moving-radiant and CTest regressions](Results/ReplayCandidates-regression.txt) ·
[Existing replay recorder contract](PathReplayEngine.md) · [Bounded colour history](ReplayReconstruction.md).
