# SolidArc verification audit — what the suite did on a clean build, and what the tolerance numbers mean

This note records what the suite actually did when the transplanted tree was first built, what was broken, and what the
numbers mean. Everything here is reproducible with `Scratchpad/BuildSolidArcParallel.sh` and
`Scratchpad/RunSolidArcSuite.sh`.

---

## Verdict

The suite is green end to end — 56 binaries, no failures — but it did not get there by fixing a broken kernel. Two
verification binaries did not compile at all, and the tolerance-policy failures they would have reported had been
invisible for as long as that was true. Both are repaired, and the tolerance policy they cover is now measured rather
than assumed.

## What the suite is

56 registered binaries under `Verification/`, driven either by `ctest` or directly. Each prints a chart
(`CHECK · MEASURED · LIMIT · VERDICT`) and returns its failure count as the process exit code.

## Findings

### 1. The tree did not compile (2 of 56 binaries)

`KernelVerification.cpp` and `AsymmetricEndpointVerification.cpp` had been left behind when
`ReconstructAsymmetricSupport` moved from `bool` to `Deliver<BrepBody>` (`Deliver` has an explicit `operator bool`, so
the value has to be unwrapped or tested, never passed as a boolean). Six call sites. Because those two binaries did not
build, the failures below were invisible: one of them carries the tolerance-policy section.

### 2. Three tolerance-policy boundary failures (in the file that did not compile)

| check | expected | was |
|---|---|---|
| Volume gate accepts its scaled boundary | PASS | FAIL |
| Sweep gate accepts its boundary | PASS | FAIL |
| Circular gate accepts its boundary | PASS | FAIL |

The gates compared `|Measured − Exact| <= Tolerance` after the caller had built `Measured` as `Exact ± Tolerance`.
That reconstruction is not exact: `(π + 1e-6) − π = 1.0000000000288e-6`, and `12 + 12·VolumeTolerance − 12` overshoots
its own limit by `3.8e-14` of `VolumeTolerance`. Every gate therefore sat exactly on a rounding edge and only two of
four happened to fall inside it. `ScalarCriteria::BoundarySlack` (`1e-9` relative) makes the boundary decision
substantive: a value on the limit is inside it. The widening is `1e-12` of the volume gate — nothing previously refused
is now accepted, and the corresponding "refuses beyond boundary" checks still refuse at `1.1×`.

### 3. The volume integrator was capped, and the cap was invisible

`VolumeTolerance` (`1e-3` relative) was described as covering "analytic-versus-tessellated" error, but a body was
`5.3e-5` relative away from its closed form and would not converge any further. Two independent causes:

- `MaximumPerSpan = 32` capped the per-knot-span subdivision, so requesting a finer `ChordTolerance` changed nothing.
  Measured: `3.79e-4` relative at the shipping chord, `7.36e-5` at cap 128.
- The residual floor is the **trim sampling** of trimmed planar caps, not the sagitta. A wall-only body keeps converging
  (sphere: `1.02e-3 → 2.48e-7` relative); any body with trimmed caps stalls near `5e-5` relative, and a `1e-7` chord
  with a 2048 span cap does not move it.

`BrepBody::Validate`, `SignedVolume`, and `Area` now take a chord **and** a span cap, and verification states the
measured band (`MeasuredVolumeBand`, `2e-4` relative) at `VerificationChord`/`VerificationCap` instead of hiding behind
the acceptance gate. The shipped gate is unchanged and clears the default tessellation with a measured `2.6×` margin.

### 4. Two suite-wide properties that are not failures

- `DimensionVerification` compares source text and assumes the working directory is the tool's source root. Run from
  there (as `ctest` does) it passes 101/101; run from elsewhere it reports 2 failures. `Scratchpad/RunSolidArcSuite.sh`
  runs every binary from that directory.
- `SuiteVerification` drives all 22 phase scripts and the contact sheet; it takes ~165 s and rewrites the large proof
  PNGs. That is why it is the slowest binary in the suite and why this branch keeps proofs up to 1 MB only.

## Final state

| metric | value |
|---|---:|
| verification binaries | 56 |
| binary-level checks | 1,992+ |
| failing binaries | 0 |
| script-driven checks (`SuiteVerification`) | 65 |
| new checks added by this audit | +23 (asymmetric route, chains, refusals) |

## Where the numbers come from

`Scratchpad/ProbeVolumeBand.cpp` prints the tessellation-error table above; `Scratchpad/ProbeChainNetwork.cpp` prints the
chain topology, analytic volumes, and per-fixture refusal reasons quoted in the roadmap. Both are diagnostics: no
assertions, no build targets, and they exist so the constants in `ScalarCriteria` can be re-derived rather than
inherited.
