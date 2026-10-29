# Textured camera and rigid-motion reference

The replay experiment is still **not complete ReSTIR GI**. This validation adds independent primary-ray and
four-hit comparisons for two actual camera pixels during a combined camera, rigid-object and texture change,
followed immediately by a moving blocker. It exercises the existing implementation; it is not editor integration.

## Fixture and verification

- The scene is a 64×64 synthetic room. Its camera shifts `0.07` world units; a cuboid rotates `0.12` rad and
  translates `0.096` units. Its texture coordinates remain attached to the cuboid. The palette also changes;
  the following submission inserts a blocker beneath the light.
- The floor and cuboid use an 8×8 opaque diffuse texture pyramid with a coarse checker. The selected cuboid
  and floor pixels use mip levels 0 and 2, respectively, and differ from the 1×1 average. Secondary hits use the
  same approximate camera-footprint filtering as the existing replay experiment.
- A separate double-precision, brute-force Möller–Trumbore primary-ray reference reconstructs camera rays,
  interpolated normals and UVs, and albedo independently of the Vulkan BVH. It checks every pixel in the
  previous, moved and blocked frames. It shares the authored scene and texture inputs, not GPU strike data.
- For each of two selected pixels and two lighting conditions, the CPU independently traces **240,000**
  four-hit paths with `mt19937_64`. The GPU uses 128 reset batches, each with a fresh comparison, three warmup
  frames, the actual camera/object/atlas transition and then the blocker transition. Sampling uses the
  existing facet/BSDF balance mode and a uniform `0.025` RGB environment.
- Both probe pixels retain one temporal candidate in every transition. The test reads the represented-candidate
  count directly from the pre-spatial reservoir; the total counters also confirm spatial links. A separate CPU
  check confirms the blocker changes the reference illumination. The CPU estimator uses cosine sampling, while
  the GPU also samples emissive facets; both target the same bounded diffuse model.

| Check | Observed result |
| --- | --- |
| Rigidly attached UVs | 30 vertex instances; max component error `1.20e-7` |
| Independent primary rays | 3,840 hits per frame; no hit or surface-token disagreement |
| Moved primary texture mips 0/1/2/3 | 184 / 158 / 572 / 259 textured hits |
| Primary max position / albedo error | `3.82e-6` / `7.16e-7` across the three frames |
| Moved / blocked accepted histories | 475,904 / 465,024 across 128 batches; each probe accepted 128 / 128 |
| Spatial links across both transitions | 3,868,672 |

Selected linear-RGB channel results are means ± **sample-variance** standard error. GPU uncertainty is computed
across the 128 per-pixel batch outputs, not by pretending neighbouring pixels are independent.

| Probe / condition | Independent CPU | SwiftShader replay |
| --- | ---: | ---: |
| Cuboid green, moved and reused | `0.0974605 ± 0.0011232` | `0.1006794 ± 0.0037911` |
| Cuboid green, blocker inserted | `0.0122664 ± 0.0000937` | `0.0120226 ± 0.0006975` |
| Floor red, moved and reused | `0.3385145 ± 0.0019985` | `0.3427832 ± 0.0045399` |
| Floor red, blocker inserted | `0.0118323 ± 0.0002434` | `0.0101216 ± 0.0010472` |

All RGB comparisons use `6 × combined SE + 1% of CPU mean + 1e-4` as their acceptance threshold. The CPU and GPU
random streams differ, but independence of the GPU's finite seeded streams is not proven. These two pixels,
this diffuse material, and this authored rigid transform cannot establish general unbiasedness, deformable-mesh
reprojection, real-world accuracy, equal-time performance or the absence of ghosting.

## Reproduce

```bash
bash Exhibits/LightCompute/RunReplayTextureMotion.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayTextureMotion.sh
```

Preparation: `python3 Exhibits/LightCompute/Code/Prepare.py`; CMake, GCC or Clang, Slang and Vulkan are required.
The retained result is Linux x86-64, GCC 12.2, Slang 2026.14.1 (`-O0` SPIR-V) and **SwiftShader CPU Vulkan**.
The external Vulkan driver is not sanitizer-instrumented. Source hashes, all channels and sample counts are in
[release](Results/ReplayTextureMotion-release.txt) and [sanitized-host](Results/ReplayTextureMotion-sanitized.txt)
logs. [Release regressions](Results/ReplayTextureMotion-regression.txt) cover the earlier replay, textured and
motion fixtures; the older eight-batch motion log uses population-variance SE, as explained in
[ReplayMotion.md](ReplayMotion.md). Physical GPUs, Windows and Khronos validation layers were not run.

See [texture domain and earlier independent room tests](ReplayTexture.md),
[previous motion tests](ReplayMotion.md), and [moving textured-room readbacks](ReplayTextureMotionCaptures.md).
