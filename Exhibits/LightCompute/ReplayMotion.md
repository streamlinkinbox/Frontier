# Bounded motion guides for path replay

Previously, temporal reuse required exact floating-point equality between the stored world position and the
current hit's reprojected previous position. A real primary-ray render of the room, followed by a 0.07-unit
camera translation, accepted **0** of 8,545 in-bounds history candidates; 8,640 primary pixels hit surfaces.
The failure was lack of reuse, not evidence that fresh estimates were biased.

The experiment now stores the prior shading normal. The temporal gate checks the surface token, normal
alignment ≥ 0.9 and distance ≤ `min(motion-guide radius, 0.25 world units)`; without both normals it falls
back to exact position equality. The primary pass supplies the reprojected prior position, prior normal,
previous pixel and footprint-derived radius. A donor seed is still retraced through the **current** BVH;
the previous radiance or visibility is not copied. Domain changes cap its represented history at one.
The tolerance is a bounded quality heuristic, not a universal world-scale or disocclusion solution.

## Checks on SwiftShader CPU Vulkan

| Case | Result |
|---|---|
| Same camera shift, old exact-position control | 0 accepted, 8,545 rejected |
| Same camera shift, nearby-position/normal gate | 8,512 accepted, 33 rejected; no reset, cap 1 |
| Rotated/translated cuboid plus camera motion | 454 of 469 cuboid primary receivers accepted; no reset, cap 1 |
| Nearby manual guide, half-occluded light | 0.120576 ± 0.001124 SE vs area integral 0.119909; 4,096 centres |
| Mismatched token, opposite normal, outside radius or outside 0.25 cap | All rejected in the fixtures |
| Missing previous normal | Exact position accepted, non-exact position rejected |

The rigid test uses the primary shader's authored rigid mapping (0.12 rad Z rotation and 0.096-unit X
translation), rebuilds the triangle BVH, and isolates only the moved object's receivers after obtaining the
primary guides. It does not infer motion from arbitrary meshes or validate deforming motion vectors.
NaN motion radii/normals and excessively large supplied normals are rejected by the host.

Four-hit, explicit-facet-sampled replay was also compared with freshly generated spatial estimates in
8 paired batches each, for camera motion alone, camera plus moving blocker, and camera plus rigid object.
All RGB differences passed the recorded `6 × batch SE + 0.004` bounds. For camera plus blocker, the red-channel
mean difference (reused minus fresh) was `0.00713 ± 0.00553` batch SE using sample variance, with 64,520 temporal
accepts and 273,504 spatial links across the batches. The historical log reports `0.00517` using population
variance; for eight batches it is about 6.5% below the sample-variance SE. This paired implementation comparison
is **not an independent physical reference**, a proof of unbiasedness, or a real-time benchmark; both estimates
share the path kernel and finite pseudorandom streams. Independent area and four-hit room comparisons are documented in
[FacetReplay.md](FacetReplay.md). The cap and support assumptions remain as described in
[PathReplay.md](PathReplay.md).

```bash
bash Exhibits/LightCompute/RunReplayMotion.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayMotion.sh
```

[Original reproduction](Results/ReplayMotion-before.txt) · [release](Results/ReplayMotion-release.txt) ·
[sanitized host](Results/ReplayMotion-sanitized.txt) · [four motion captures](ReplayMotionCaptures.md).
The external SwiftShader driver is not sanitizer-instrumented.
