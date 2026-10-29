# Frontier continuation report

**Date:** 27 October 2026 · **Branch:** `arena/672a179a-frontier`

**Overall status:** useful, tested CPU/reference work is checkpointed; the requested system is **not fully ported or
physically validated**. Results below distinguish source matching, analytic/model comparisons and measured hardware.
No Windows/editor, Vulkan or GPU execution is claimed.

## Source reconciliation

The continuation started from `arena/2ac8ddab-frontier` (`ae0bb7d`). Three follow-up snapshots were compared:

| Snapshot | Decision |
|---|---|
| `faadde0` | Had useful cheap-raster work, but its 1024-pixel Three.js Moon was a fallback, not the requested demo asset. |
| `72db844` | Contained extensive sky/physics work and claimed calibration. Its fallback Moon and synthetic tyre/sky checks were not accepted as measured-system validation; the snapshot was not adopted wholesale. |
| `29c5ab1` | Restored the full VehicleHMI source reference and actual 2048-pixel Luna image; used as the continuation base. Subsequent changes below were verified and checkpointed. |

The full VehicleHMI source is `SultanAladin/Frontier` at `6afb6f00b5f25c45f7cda92c7da65b64845768f3`.
**Project Drive** is a separate vehicle/drivetrain/tyre source, `SultanAladin/Frontier-` at
`3f14808a6dcc909ae64070d62639a7019f5de794`. The demo's tyre-deformation models are not attributed to Project Drive.
Retained sources and hashes: `../Experimental/VehicleHmiReference/` and
`../Experimental/ProjectDriveReference/Vehicle/`.

## Delivered and checked

| Area | Result and evidence | Boundary |
|---|---|---|
| XPBD and 3D solid tyre shapes | Ported the demo's 160-node XPBD projection and 128-cell Hex8 solid models; controlled JS/C++ node, force, Jacobian and interpolation checks pass. [Details](VehiclePhysics/Continuation.md). | Deformation-only modes: the spring model still supplies chassis load. The separate shell model is not ported. These are not measured tyre results. |
| Spring tyre / vehicle | 3,480 controlled comparison steps pass; 720 synchronized single steps match essentially to floating-point precision. | A free-running hard-drop extension **fails after step 645**, with a maximum load difference `334.5 N` over 720 steps. Impact sensitivity and nonconvergent timestep behavior are documented, not explained away. |
| Project Drive physics | Imported source-matched Pacejka forces and aligning torque, slip dynamics, suspension, brakes/ABS, drivetrain, thermal and aero. Six translation units preprocess identically; 126 original-suite checks and 7,350 force/torque, 4,320 slip and 8,640 drivetrain samples match the retained source. | Standalone core and visual-only bridge; **not** integrated with editor/Jolt. Source parity does not prove measured-vehicle calibration or independent full MF6.1/GRIT equivalence. One original scene test is stale and does not compile. |
| Sun and flares | Finite-pixel, limb-darkened Sun in the CPU reference renderer; warm streak and blue anamorphic streak work simultaneously. Seven-view solar flux error ≤ `0.000439`; release and sanitizer tests pass. [Evidence](NightSky/README.md). | Approximate Gaussian/erfc optics and coarse occlusion, not a measured lens PSF or GPU port. Planet-limb partial clipping remains open. |
| Moon, stars and daylight | The **actual 2048 × 1024 demo Luna JPEG** is used in NightSky and, opt-in, the CPU reference renderer. The latter has catalogue stars, elevation-dependent surface sunlight, lunar direct light and a limited one-bounce lunar SDF path. [Evidence](NightSky/DaylightReference.md). | Only Luna is used; no editor/GPU integration. Texture redistribution rights need review; lunar shading and sky are not calibrated photometry. |
| Moon/Sun positions | Compared 960 selected 2026 cases at five sites against JPL DE421 through Astropy/jplephem. Worst Moon direction difference `0.003118°` versus `2.912255°` for the old geocentric approximation; worst Sun difference `0.012497°`. [Results](NightSky/Results/Ephemeris-release.txt). | Independent ephemeris model, **not telescope measurements**; refraction and some apparent-position effects are not implemented. Legacy mean-age `MoonPhase` remains approximate. |
| ReSTIR | Corrected the CPU reference renderer's **discrete direct-light spatial reuse** for tested static domains; sparse-light mean changed from `0.502607` to `0.355025 ± 0.002030`, against exhaustive `0.352533`. Added opt-in, static one-bounce SDF-emitter and Moon-lit temporal reuse. [Audit and results](LightCompute/StaticIndirect.md). | Not the full demo GI pipeline: no GI spatial shifts, moving scenes, RGB cards, sky paths, multibounce or GPU validation. The retained demo path-spatial equations have a separate exact zero-weight-donor bias counterexample. |
| Cheap test raster | Improved simple lighting and replaced tiled ground with a plane fill; paired CPU test medians dropped from `61.2–62.9 ms` to `36.7–38.2 ms`, with unchanged buffers. [Timing](VehiclePhysics/Results/RasterTiming.txt). | Testing visualization only; no PBR, added rendering pass or claim of visual fidelity. Timing is not a GPU benchmark. |

## Current verification

- NightSky exhibit: **42/42**, including ASan/UBSan; JPL DE421 comparison passes in release and ASan/UBSan. The new
  [Moon close-up](NightSky/Images/MoonClose.png), [phase strip](NightSky/Images/MoonPhases.png) and
  [Moon field](NightSky/Images/NightSkyMoonField.png) were regenerated and inspected.
- New daylight/lunar reference suite: **37/37** in release and ASan/UBSan; 6,228 selected catalogue directions
  differ from Astropy/ERFA by at most `0.007170°`. The [daylight Moon](NightSky/Images/DayMoonReference.png),
  [night Moon](NightSky/Images/NightMoonReference.png) and [night stars](NightSky/Images/NightStarsReference.png)
  are actual reference-renderer outputs, not calibrated photographs.
- VehicleReference: **7/7 CTests**; environment pipeline: **1/1 CTest** (73 individual checks in its retained baseline).
  GI release and ASan/UBSan validations pass; [solar and mixed flare capture](NightSky/Images/SolarReferenceMixed.png).
- Demo tyre and Project Drive source comparisons, spring studies and raster timings have reproducible scripts and
  retained outputs under [VehiclePhysics](VehiclePhysics/Continuation.md). The explicitly failing extended-drop
  result is preserved rather than counted as a pass.

## Remaining work

1. Resolve or bound the spring solver's long-impact divergence and validate tyre/vehicle behavior against actual
   measured data; wire Project Drive into a real Jolt/editor host before claiming a complete vehicle port.
2. Integrate the actual Moon map and celestial directions beyond the CPU reference into the desired editor/GPU path;
   establish texture redistribution rights and test refraction/occlusion and calibrated sky/lens appearance.
3. Implement and independently validate the rest of the demo ReSTIR GI pipeline, especially spatial reuse,
   temporal reprojection, visibility, dynamic scenes and GPU execution; do not extrapolate the small static subset.
4. Validate the integrated build on the user's Windows/MSVC/Vulkan environment and real hardware.

The verified checkpoints were pushed only to the approved branch. See [tracker](../Documentation/Tracker.md),
[vehicle details](VehiclePhysics/Continuation.md), [sky details](NightSky/README.md) and
[GI details](LightCompute/StaticIndirect.md) for commands, raw results, source provenance and limitations.
