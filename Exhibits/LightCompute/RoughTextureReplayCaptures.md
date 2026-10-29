# Rough-textured motion and occlusion readbacks

Four settled views and three first-transition views are **SwiftShader CPU Vulkan** readbacks of the bounded
four-hit rough-textured replay experiment. These are synthetic scenes, not editor or physical-GPU screenshots.
They illustrate transitions; they do not establish image fidelity or the absence of ghosting.

## Settled views

Each image averages the last eight of 16 submissions in linear RGB. Previews use a geometry-, normal- and
albedo-aware display blur. Unfiltered readbacks: [before][before-raw], [moved][moved-raw],
[blocked][blocked-raw], [reopened][reopened-raw].

| Before motion                         | Camera, cuboid and material changed    |
| ------------------------------------- | -------------------------------------- |
| ![Before][before]                     | ![Moved][moved]                        |
| **Blocker inserted**                  | **Blocker removed; material restored** |
| ![Blocked][blocked]                   | ![Reopened][reopened]                  |

[before]: Images/RoughTextureBeforePreview.png
[moved]: Images/RoughTextureMovedPreview.png
[blocked]: Images/RoughTextureBlockedPreview.png
[reopened]: Images/RoughTextureReopenedPreview.png
[before-raw]: Images/RoughTextureBefore.png
[moved-raw]: Images/RoughTextureMoved.png
[blocked-raw]: Images/RoughTextureBlocked.png
[reopened-raw]: Images/RoughTextureReopened.png

## First submission after each change

These are individual, visibly noisy submissions, not the settled averages. The display blur can spread bright
samples and blur shadows. Raw readbacks: [motion][motion-first-raw], [blocker][blocker-first-raw],
[reopening][reopening-first-raw].

| Camera, cuboid and material       | Blocker inserted                   |
| --------------------------------- | ---------------------------------- |
| ![First motion][motion-first]     | ![First blocker][blocker-first]    |

Blocker removed and the initial material restored:

![First reopening][reopening-first]

[motion-first]: Images/RoughTextureMovedFirstPreview.png
[blocker-first]: Images/RoughTextureBlockedFirstPreview.png
[reopening-first]: Images/RoughTextureReopenedFirstPreview.png
[motion-first-raw]: Images/RoughTextureMovedFirst.png
[blocker-first-raw]: Images/RoughTextureBlockedFirst.png
[reopening-first-raw]: Images/RoughTextureReopenedFirst.png

The camera shifts `0.35` world units; the cuboid rotates `0.28` rad and translates `0.224` units. The material
changes from roughness/metallic `0.65/0.35` to `0.48/0.70` along with its texture palette. Reopening restores
those initial material values while the camera and cuboid stay in their moved positions. Both current and
previous rigid/camera transforms are supplied; each reused seed is retraced against the current triangle BVH.
The mode combines emissive-facet and cosine sampling with balance weighting.

| Transition                         | Temporal accepts | Spatial links | Reset | History cap |
| ---------------------------------- | ---------------: | ------------: | ----: | ----------: |
| Camera, cuboid and material        | 34,922           | 142,226       | 0     | 1           |
| Blocker inserted                   | 34,470           | 142,148       | 0     | 1           |
| Reopen and restore material        | 34,470           | 142,226       | 0     | 1           |

The fixed lower-image linear luminance means were `0.169474`, `0.249564`, `0.016004` and `0.168702`. They are
scene sanity checks, not photometric ground truth. Exposure is `0.45` with `1-exp(-L×exposure)` and gamma `2.2`.
These images use a larger motion than the independently checked fixtures. The
[rough-textured moving-primary reference](RoughTextureMotion.md) checks two probes during a smaller motion;
[other numerical checks](RoughTextureReplay.md) cover static primary probes and dynamic secondary changes.
Neither test validates the larger rendered motion or establishes image-wide correctness.

```bash
bash Exhibits/LightCompute/RunRoughTextureCapture.sh
```

[Capture log and decoded RGB hashes](Results/RoughTextureReplay-capture.txt) ·
[Sanitized replay and capture-host regressions](Results/RoughTextureCapture-sanitized.txt) ·
[Additional replay regressions](Results/RoughTextureCapture-regression.txt) ·
[Earlier diffuse motion gallery](ReplayTextureMotionCaptures.md).
