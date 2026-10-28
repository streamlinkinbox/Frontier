# Static indirect reservoir and demo spatial audit

The CPU reference renderer now has an **opt-in, static one-bounce SDF-emitter temporal reservoir**. This is a small
subset of ReSTIR GI, not a port of the full VehicleHMI GI pipeline. Its existing discrete direct-light spatial reuse
also had a measurable bias on sparse and mismatched domains and has been corrected for the static CPU renderer. The
retained demo spatial estimator has an exact sparse-signal bias counterexample; it is not enabled or modified here.

## Reproduce

```sh
# Requires g++ and CMake; use an absolute CMAKE path if it is installed in a virtualenv.
CMAKE=cmake BASELINE=1 bash Exhibits/LightCompute/RunStaticIndirect.sh  # also characterize 07ab116
CMAKE=cmake SANITIZE=1 bash Exhibits/LightCompute/RunStaticIndirect.sh
```

Linux x86-64, GCC 12.2, C++17; release `-O2` and ASan+UBSan. Retained output:
`Results/StaticIndirect-{release,sanitized}.txt` and `Results/DirectSpatial-before.txt`; source base `07ab116` plus
this change. The optional baseline compiles the pinned old `ReferenceRenderer.cpp` with the same harness and other
current dependencies, and characterizes its output without asserting success; it requires that Git object. The
runner checks the original GI shader blob hashes and compiles the changed C++ and validations with strict warnings.
`ReferenceRenderer` uses `IndirectTemporalReuse = false` by default; the new setting is appended to preserve the
ordering of existing aggregate initializers. With spatial reuse disabled, the pinned old renderer and the new one
also produced the same binary64 pixel hash (`04da4f54d91705d0`) for a fixed four-frame SDF/direct-light probe. This
is one compatibility fixture, not a universal image guarantee. Enabling the option only changes the existing scalar
emissive SDF gather; the cheap vehicle test raster does not include this renderer or pay this cost.

## Executed numerical checks

| Check | Result | Independent expectation |
|---|---:|---:|
| One sphere, 12,000 sequences × 24 frames, final temporal sample | `0.282163 ± 0.001598` | `0.283310` analytic irradiance |
| Same sphere, temporal average | `0.283497 ± 0.002198` | `0.283310` |
| Two unequal off-axis emitters, 4,000 sequences × 12 frames, 4 new rays/frame | `0.405707 ± 0.003781` | `0.404165` analytic irradiance |
| Reference renderer, 1,024 seeds × 16 frames | `0.094734 ± 0.003009` | `0.090180` analytic diffuse radiance |
| Constant-radiance plane | error `2.23e-15` | `π` irradiance |
| Zero-weight history | count `1 → 2`, zero radiance | zero candidates must still count |
| History limit | final count `21` | one fresh sample + 20 retained |
| Opaque SDF blocker / removed source | `0` | `0` |
| Moved receiver, different scene, zero normal | reuse/stream rejected | no invalid context reuse |

`±` is one empirical standard error of independent sequences, not a confidence interval or measured physical error.
Sphere expectations use the closed-form solid-angle integral for uniform radiance at the nudged ray origin. Tests
allow a small SDF-tracing tolerance and five standard errors. The renderer comparison uses a single rasterized diffuse
receiver pixel, no analytic lights, atmosphere, cards or denoising. A time average of reused frames is correlated;
its uncertainty is computed across independent sequences, not as if frames were independent.

## CPU discrete direct-light correction

A separate problem existed in the CPU renderer's default-on **direct-light** spatial reuse: it skipped zero-weight
neighbors and divided by the accumulated count regardless of whether a selected light had support in each source
domain. This is separate from the retained demo's spatial *path* shader. In the fixed CPU pass, the selected light's
normalizer `Z` sums the candidate counts of the current and adjacent static domains whose light proposal contains
that light and whose unshadowed selection target is positive. A zero-weight draw does not erase its domain. Clustered
proposal membership is checked explicitly. Temporal history is saved before spatial reuse, so source domains are not
silently folded into later frames. The final selected light is visibility-tested at the receiver as before.

For this discrete, static setting, conditioning on the resampling selection gives
`E[estimate] = Σ_j M_j ∫_{support_j} F(y)/Z(y) dy`; since `Z(y) = Σ_j M_j 1_{support_j}(y)`, the source-domain
contributions sum to the integral where the union covers the receiver's nonzero contribution. An exact two-light,
two-domain enumeration in `PathSpatialAudit.cpp` yields `1.5` for naïve total-M normalization versus `2.0` for
selected-support normalization, with an exact sum of `2.0`. This is not a proof for path shifts, animation,
non-discrete light samples or an arbitrary future reuse schedule.

| Fixed CPU renderer fixture | Old mean ± SE | Fixed mean ± SE | Exhaustive lighting |
|---|---:|---:|---:|
| Equal domains, sparse; 10,000 seeds, 1 frame | `0.502607 ± 0.002381` | `0.355025 ± 0.002030` | `0.352533` |
| Equal domains; 1,200 seeds, 24 frames | `0.385283 ± 0.001570` | `0.353192 ± 0.001610` | `0.352533` |
| Unequal target support, global; 8,000 seeds, 1 frame | `7.311370 ± 0.087350` | `12.039807 ± 0.133043` | `12.222667` |
| Unequal support, clustered; 8,000 seeds, 1 frame | `4.255496 ± 0.042919` | `12.073928 ± 0.133181` | `12.222667` |
| Unequal support, clustered; 1,200 seeds, 24 frames | `0.890232 ± 0.006809` | `12.094384 ± 0.100846` | `12.222667` |

These compare stochastic estimates with the renderer's *exhaustive discrete-light* calculation at the same surface
pixel; they do not independently validate its BRDF, light units or visibility. The clustered fixture asserts the
local light is in the receiver's list but absent from both neighboring lists. The 24-frame cases exercise the history
cap. The cheap vehicle test raster does not use this CPU reference renderer.

## Exact spatial counterexample in the retained demo

The recovered `ReservoirSpatial.wgsl` skips a neighbor if its reservoir `Weight <= 0`, before storing its surface and
candidate tally for the `Z` calculation. A zero result from the neighbor's **random draw** does not mean its sampling
domain could not have produced a different, selected bright sample.

Take two independent single-candidate reservoirs at identical receiver surfaces, each drawing a bright event with
probability 0.5 and a dark event otherwise. The bright reservoir's finalized weight is `2`, the dark one's is `0`;
the correct integral is `1`. The following exhaustively enumerates the demo's merge and `1/Z` equations:

| Own draw | Neighbor draw | Probability | Demo equation | Retaining neighbor domain in `Z` |
|---|---|---:|---:|---:|
| Dark | Dark | 0.25 | 0 | 0 |
| Bright | Dark | 0.25 | 2 | 1 |
| Dark | Bright | 0.25 | 1 | 1 |
| Bright | Bright | 0.25 | 2 | 2 |
| **Mean** | | **1** | **1.25** | **1.00** |

`Code/PathSpatialAudit.cpp` also enumerates probabilities 1/8 and 1/16, yielding source means `1.4375` and
`1.46875`. This is an algebraic source-equation audit, **not actual WGSL/GPU execution**. Retaining a zero-weight
neighbor's domain fixes this simple fixture only; a general spatial estimator must also handle sample-dependent
reconnection, visibility, support, domain rejection and correlations. No general fix is claimed here.

## Moon-lit reference continuation

A separate opt-in `LunarBounceReservoir` now samples one Moon-lit diffuse bounce from analytic SDF primitives,
with same-receiver temporal reuse only. The full Moon/Sun ratio, direct lighting, occlusion, analytic-wall checks
and limitations are documented in [DaylightReference.md](../NightSky/DaylightReference.md). It does not replace
or complete the demo's GPU ReSTIR GI pipeline.

## Boundaries

- The original `StaticIndirectReservoir` samples constant scalar emissive radiance on analytic SDF primitives. It has no sky
  candidates, RGB card radiance, spatial reuse, multi-bounce paths, moving receiver, motion reprojection, dynamic
  geometry or updated-illumination history. It is discarded at the end of each `Render` call.
- Reuse rejects a different receiver or scene object, but a caller must not mutate the same scene object during reuse;
  this API does not track scene revisions. The reference renderer holds it static for the call.
- The direct-light correction is limited to fixed raster surfaces and discrete lights within one render call. Its
  tests cover the stated source domains, not every BRDF, scene, visibility configuration or motion case.
- The actual demo includes storage, temporal reprojection, motion carrying, optional path tracing, spatial
  reconnection, visibility and filtering. Those are not reproduced or validated by this CPU subset.
- Sphere tracing approximates the surface. These are analytical/simulation checks, not calibrated physical light,
  GPU, Windows/editor or real-time validation. No GI spatial-reuse or performance claim is made.
