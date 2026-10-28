# Thin window followed by closed glass — 2026-10-25

> Historical checkpoint: [VehicleGlazing](VehicleGlazing.md) subsequently adds derived vehicle panes,
> shared-edge evaluation and a 2,048-interaction bound. Earlier 32-step descriptions below refer to the
> original checkpoint, not the current runtime limit.

**Primary camera rays can now cross the existing parallel-sheet window and then traverse smooth closed
volumes.** The sheet is still an approximation, not a newly implemented refracting thin-film model.

![Window followed by index-matched, clear refracting and absorbing closed glass](Images/WindowDielectric.png)

Actual **96×64 Vulkan readbacks**, 64 samples × two sheet directions, enlarged nearest-neighbour. The camera
sees a thin pane in front of the same closed box and emissive checker in each image. The box is index matched
on the left, index 1.5 in the middle, and index 1.5 with RGB absorption 0.2/0.6/1.2 on the right.

A subsequent [compact diffuse extension](DiffuseDielectric.md) closes the material-address-zero gap.
The limitations below describe this window checkpoint.

## Verification

| Check | Result |
|-------|--------|
| Device | SwiftShader Device (Subzero), CPU type 4; software Vulkan only |
| Window → index-matched slab, no absorption | Maximum central RGB mean error **7.28721e-8** |
| Window → index-matched absorbing slab | **6.06713e-8** |
| Window → index-1.5 clear slab | **0.00312531** |
| Window → index-1.5 absorbing slab | **0.00256934** |
| Analytic coverage | 100 central pixels per slab case; all 6,144 primary pixels assert sheet classification |
| Retained sheet reflection, rear source off | Maximum error **4.58834e-9** |
| Retained sheet reflection + transmission, both sources white | Maximum error **1.1283e-6** |
| Path diagnostics | Every rendered sheet pixel asserts zero capped / unsupported paths |
| Source removal | All **6,144 pixels exactly black**, no explicit history-reset request |
| Existing regressions | 199 boundary cases, primary/mirror/rough paths, opaque history and 4/4 native CTests pass |
| Build | 13 production compute entries compiled; strict C++20 Release host |
| Pinned negative control | Transport shaders from `abed7c6` reproduce blocked-black central transmitted glass rays |

The independent native expectation combines the previous sheet expression and a unit-thickness slab series:

- `Fw = 0.04 + 0.96 (1-cos(theta))^5`
- `Tw = (1-Fw)/(1+Fw) * exp(-sheet_sigma * 0.006 / max(0.05,cos(theta)))`
- `Tslab = (1-F)^2 A / (1-F^2 A^2)`, `A = exp(-volume_sigma / cos(theta_transmitted))`
- Transmitted slab expectation: `Tw * Tslab`; retained sheet reflection: `2Fw/(1+Fw)`.

`F` and the refracted angle use independent double-precision exact Fresnel/Snell arithmetic. Central **means**
are compared against an infinite-slab series; finite edges and sampling are not a converged whole-image oracle.
Declared tolerances are 0.0001 for index matching, 0.03 for index 1.5, and 1e-5 for the sheet energy controls.
The energy controls use an index-matched, nonabsorbing volume to isolate the sheet weights. The emitter-removal
check exercises fresh transport, not a new temporal-denoiser acceptance claim.

## Implementation scope

| Change | Detail |
|--------|--------|
| Unified authored-material route | All authored camera receivers use the same flat `IntegrateBoundaryRays` sequence |
| Primary sheet proposals | Reflected and straight-through directions; both feed the one closed-volume trace site |
| Previous sheet weights | Retained Schlick F0=0.04, two-interface factors and fixed RGB absorption/thickness |
| Closed volume | Existing Snell/exact-Fresnel/TIR, distance absorption, geometric outward normals and 32-interaction bound |
| Sampling | At most 128 samples × two sheet directions; fixture uses 64 |
| Sheet diagnostics | Marker 2; `y` any reflected scene hit; `z = capped*512 + unsupported` |
| Removed code | Separate primary thin-sheet ray loop that rejected category-4 hits black |

The new path uses the shared 32-interaction tracer rather than the former eight-pane camera-secondary loop.
It retains the sheet weighting approximation, **not bit-identical traversal/sampling or a measured frame-time
improvement**. Multiple/closely spaced panes and arbitrary scene scales are not exhaustively accepted.
Thin panes encountered while inside a solid medium remain unsupported; this phase covers sheets in air
followed by smooth volumes. Saved material TOML/presets and descriptor layouts are unchanged.

## Reproduce

```bash
bash Exhibits/LightCompute/RunWindowDielectric.sh
# Reuse an explicitly completed build:
WINDOW_BUILD=0 bash Exhibits/LightCompute/RunWindowDielectric.sh
```

Requires the repository's pinned tools, Pillow setup and the pinned Git revisions used by the negative controls.
The wrapper also runs all previous boundary, primary-glass, mirror, rough-reflector, opaque history and native
CTest checks. The mirror negative control now compiles pinned `8a1f38f` transport shaders rather than editing
an obsolete dispatch condition. Shared scattering math is unchanged; these controls do not replay an entire
pinned application.

- [Slab, sheet energy and source-off log](Results/WindowDielectric.txt)
- [Pinned blocked-path control](Results/WindowDielectricOriginal.txt)
- [Capture hashes and scope](Results/WindowDielectric.json)
- [Remaining renderer work](RendererProgress.md)

Still remaining: legacy compact-only diffuse integration, saved closed-glass authoring and actual vehicle
bindings, rough glass boundaries, nested media/inside cameras, caustics/transmitted direct shadows, complete
sampling/denoising and unified moving-scene acceptance, physical RTX/GTX and Windows/editor verification.
Both analytic and emissive lighting, existing cellular flakes and sun/flare/stars remain present.
