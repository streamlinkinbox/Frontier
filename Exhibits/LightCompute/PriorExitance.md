# Prior-screen exitance, rigid authored motion and unified moving vehicle — 2026-10-26

Software Vulkan only: **SwiftShader Device (Subzero), CPU type 4**. No physical RTX/GTX, Windows, editor, or real-time claim.

## Verdict

| Item | Status | Evidence |
|------|--------|----------|
| Roughness-cone prior-screen exitance pyramid (six levels, ceiling condensation) | 🟢 verified (native + SwiftShader) | `Results/PriorExitance.txt`, max mip RGB error 6.7e-7 |
| Pinned pre-remedy control (3e8259c): cold reset on authored motion, no pyramid | 🟢 verified control | `Results/PriorExitanceOriginal.txt`, `moving_reused=0` |
| Rigid authored-motion continuation (prism reuse under motion) | 🟢 verified for one prism | 318 rigid pixels reused under motion; 5 tamper rejections native |
| Guarded lighting refresh and negative control | 🟢 verified | refresh diff 0; unguarded stale leak 0.305 |
| DistanceFieldSpace as certified clearance field | 🟢 verified bound (native + GPU) | 0 Lipschitz violations; 0 bound overestimate |
| DistanceFieldSpace accelerating ray traversal | 🔴 **not implemented** | field cannot reject non-rigid geometry; traversal is exact BVH |
| Unified six-pose ControlVehicle + cloud + fog + closed glass + SSR scene | 🟢 verified for the fixture (not a production scene) | `Results/UnifiedScene.txt`, `UnifiedSceneMetrics.json` |
| Filtering / denoising of authored reflections beyond cone prefilter | 🚧 partial | sharp-strip bias measured (below) |
| Physical RTX / GTX / Windows / editor | 🔴 not run | — |

## What changed

- `LightInfluence.slang`: `ExitancePyramid` condensation entry (`CondenseExitancePyramid`), `EvaluateScreenStrikeExitance` (reprojects the screen-struck surface, verifies token, normal and position, then samples the roughness-cone mip). `ReprojectToPreviousWorld` is shared with primary motion. `DistanceFieldSpace` header, lower bound and clearance query.
- `MaterialRayTransport.slang`: screen-proposed reflections use the pyramid when `Extent.Primary.w & 64`; sharp mirror (Category 1) and thin sheet (Category 2) paths are unchanged.
- `LightComputeValidation.cpp`: `ConfirmRigidPoseContinuation` (native unposing, sorted matching, tamper rejection), pyramid upload/readback, lighting-revision guard.
- `PriorExitanceValidation.h`, `UnifiedSceneValidation.h`: native and GPU acceptance.

## Defects found and fixed during this phase (not hidden)

| Defect | Found by | Fix |
|---|---|---|
| Host and shader used different mip-size rules (floor vs ceiling), shifting later levels | GPU mip check, level 4 zero | Ceiling rule in host, dispatch and reference |
| Reference classified texels by diagnostic code, not material address | GPU mip check | Category from `ExpectedDirect.w` as in shader |
| `DistanceFieldSpace` clipped the **whole** scene's traversal interval, dropping non-rigid rays (15,511 of 16,000 misses) | GPU primary vs native (native 2,758 misses) | Traversal kept exact; field used only as a clearance query |
| Clearance bound evaluated at an offset probe point but compared to the surface distance (1,537 false violations) | Unified Mode 2 | Bound evaluated at the surface point |
| Mode-2 host read the wrong component; cap counters read compact-route counters; SSR code 4 mistaken for glass (5) | Unified harness | Corrected decoding |
| Sharp-strip fixture was not visible, rough strip had zero pixels | Token histogram | Strips moved into frame; sharp roughness at floor 0.04 |

## Measured results (SwiftShader CPU type 4, 160×100 unless stated)

| Check | Result |
|---|---|
| Native mip constant gap / bound overestimate | 2.4e-8 / 0 over 21,504 field samples |
| GPU mip vs double reference, six levels | max RGB 6.7e-7, coverage ≤1e-6 |
| Pyramid hits / BVH fallbacks (stationary warm) | 3,788 / 31 |
| Rough strip (roughness 0.48) within-strip std, pyramid vs point | 0.0553 vs 0.0688 (0.80×) |
| Medium strip (0.24) within-strip std | 0.2096 vs 0.2242 (0.93×) |
| Rough strip mean energy, pyramid / point | 0.948 (tolerance 6%) |
| Sharp strip (0.04), pyramid vs point | **5.7% deviation vs 1.2% seed-to-seed noise** |
| Mirror / thin-window pixels, pyramid vs point | identical (diff 0) |
| Moving rigid prism reuse, pose 1 / pose 2 | 12,114 / 11,976 opaque pixels; 318 rigid |
| Guarded lighting refresh vs cold | diff 0 |
| Unguarded negative control (stale warm key light) | leak 0.305 |

**Sharp-strip limitation.** Level 0 is selected for the sharp strip, so this is not blur. The 5.7% gap comes from estimator difference: the pyramid samples the struck pixel's prior exitance, while the point path shades the struck point fresh along the reflected direction. The seed-to-seed noise is 1.2%. The test bound is 10%, chosen for this glossy-lobe estimator. A tighter bound would need a mirror-lobe comparison, which is not done.

## Unified six-pose scene (192×120, 16 samples)

Two coherent camera segments; wind 0 s on poses 0–2 and 90 s on poses 3–5; the body rotates 0.08 rad per pose. Closed 6 mm glass (284 triangles, 3 panes) and two body-mounted side mirrors are rigid tokens above the 500 threshold. Cellular paint, rubber, indigo-denim draped cloth, thin-sheet canopy with mullion, and emissive light strip are present. Analytic sun, canopy spot and accent point lights are present alongside emissive facets. Clouds and fog are on.

| Pose | Opaque reused | Rigid reused | Glass px | Mirror px | Pyramid hits | Caps / invalid |
|---|---|---|---|---|---|---|
| 0 warm | 17,058 | 1,284 | 302 | 112 | 6,350 | 0 / 0 |
| 1 moving | 15,078 | 1,191 | 297 | 112 | 5,999 | 0 / 0 |
| 2 moving | 15,052 | 1,201 | 289 | 117 | 6,023 | 0 / 0 |
| 3 weather + light change | 0 (reset, by design) | 0 | 560 | 225 | 0 | 0 / 0 |
| 4 moving | 14,369 | 2,709 | 528 | 235 | 12,216 | 0 / 0 |
| 5 moving | 14,318 | 2,723 | 500 | 243 | 11,889 | 0 / 0 |

- Native: 6 poses, 5 rigid continuations, closed-volume drift 5.9e-8, 3,000 bound probes, 0 overestimate.
- Source-off frame: 23,040 black pixels.
- Mode-2 clearance: 0 Lipschitz violations, 17,217 clear-solar and 1,806 near-vehicle pixels.
- Frame gate: no directional (mirror or volume) history reuse; the reuse gate is in the validator.

Images: `Images/UnifiedSceneHero.png` (320×200 pose 0 and pose 3), `UnifiedSceneSequence.png`, `UnifiedSceneMotion.gif`, `UnifiedDistanceDiagnostic.png`, `PriorExitanceComparison.png`.

## Scope limits (open)

- `DistanceFieldSpace` is a bounded clearance query. It does **not** accelerate BVH traversal. The earlier planned speed-up was removed after the clipping defect; no speed claim is made.
- The fixture is an authored proving room and a stylised ControlVehicle, not a production scene or editor import.
- Rigid reuse is measured for the prism (fixture) and the body (unified scene); general moving authored geometry still resets conservatively.
- Pyramid reflection energy is checked against point-hit shading for one strip, not all materials.
- No frame-time or equal-cost speed claim.

## Reproduction

`bash Exhibits/LightCompute/RunPriorExitance.sh` (after `Run.sh`/`Prepare.py` have produced the vehicle captures and saved glazing). Results and scratch hashes: `Results/PriorExitance*.txt`, `Results/UnifiedScene*.txt|json`, `Results/PriorExitanceImages.json`.
