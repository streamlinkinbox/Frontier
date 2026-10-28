# Dynamic camera and blocker: four-bounce replay

This is a **validation checkpoint**, not a new general-purpose renderer. It tests the existing Engine replay
path when a camera and an occluding rectangle move together. It does not enable colour accumulation through
moving geometry: accumulated colour is reset at each scene change. The GPU path still performs software
triangle-BVH ray traversal; this is not hardware RT, non-ray-traced GI, complete ReSTIR GI, unified PBR,
a physical-GPU benchmark or an editor viewport.

## Scenario and independent reference

The `32 × 32` diffuse fixture contains a static plane, an emissive rectangle and a smaller opaque blocker.
The camera translates by `0.0625` world units while the blocker shifts by `0.1`; then the blocker returns.
Each tested transition uses four path bounces, four initial candidates, a uniform RGB environment of `0.025`
and no explicit emitter-facet sampling. Three previous stationary frames populate the temporal reservoirs.
The candidate paths selected from temporal and spatial donors are **replayed through the current BVH** at the
current receiver, not trusted with their old visibility. Previous represented history is capped at one at the
scene revision. Spatial seeds may originate on a different surface; they are retraced, but this check does not
prove general spatial-reuse correctness or independent random streams.

A double-precision CPU reference uses brute-force triangle intersections, an independent `mt19937_64` generator
and a different cosine-hemisphere frame. It shares the same bounded Lambertian transport, uniform environment
and ray-origin offsets with the GPU, so this is an independent implementation of a model, not real-world
radiometric evidence. The reference evaluates `1,024` paths at each of `816` visible plane points:
`835,584` total. It does not cover the foreground blocker's pixels. CPU standard errors are finite-sample
corrected and nominal; streams at different points are separately seeded, not proven independent.

## Results

Sixteen seed batches compare the first moved-scene frame with a four-candidate GPU control that has temporal
and spatial reservoir reuse disabled. The two GPU modes use the same current-frame seeds. Batch standard
errors are finite-sample corrected but are **nominal**, because correlations among GPU streams are not ruled
out. The CPU point reference is shared by all batches.

| Red-channel result | Independent CPU model | Four fresh candidates | Temporal + spatial replay |
|---|---:|---:|---:|
| Mean ± nominal SE | `0.116587 ± 0.000332` | `0.116829 ± 0.001476` | `0.116991 ± 0.001206` |
| Per-output RMSE vs CPU point means | — | `0.154536` | `0.082765` |

The paired reused-minus-fresh mean is `0.000162 ± 0.000664` nominal batch SE. These finite checks do not
prove unbiased adaptive reuse or universal variance reduction. The reused mode also performs three warm-up
frames, temporal replay and spatial-donor path evaluations; its lower per-output RMSE is **not an equal-time
or equal-cost speedup**. Other fixtures have already shown rare tails and sampling correlations, so this result
must not be generalized beyond this scene and seed set.

On the first transition the fixture reports `904` accepted temporal reservoir histories and `3,968` spatial
links. All `88` pixels whose reprojected previous surface token differs keep only their four fresh initial
candidates in the pre-spatial reservoir: temporal cross-token history is rejected. This assertion concerns
the **temporal** donor, not all spatial donors. In every moved-scene batch, colour history is reset, accepts
zero histories and matches the equivalent un-reconstructed replay output bit-for-bit. The first reopening
frame also resets and matches raw. These are controlled tests of specified revisions and occlusion, not proof
of arbitrary animated disocclusion safety.

Release and host ASan/UBSan runs agree numerically on SwiftShader CPU Vulkan; the external driver and shaders
are not sanitizer-instrumented. Previous [camera reconstruction and default-output regressions](ReplayCameraReprojection.md)
remain applicable: no Engine shader or recorder changed in this validation checkpoint.

## Reproduce

Requires the prepared Slang/SwiftShader tools, Vulkan headers, CMake and a C++20 compiler.

```sh
bash Exhibits/LightCompute/RunReplayDynamicCamera.sh
SANITIZE=1 BUILD_JOBS=1 bash Exhibits/LightCompute/RunReplayDynamicCamera.sh
```

[Release results](Results/ReplayDynamicCamera-release.txt) ·
[Host ASan/UBSan results](Results/ReplayDynamicCamera-sanitized.txt) ·
[Regression summary](Results/ReplayDynamicCamera-regression.txt).
