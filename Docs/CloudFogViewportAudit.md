# Clouds and fog missing from the GPU viewport

Historical audit: **the missing wiring below was confirmed before the repair.**

The GPU view-ray repair is now implemented; see [CloudFogViewportRepair.md](CloudFogViewportRepair.md) for current behavior, tests and remaining verification limits. Raw pre-fix evidence below is preserved.

Audited after icon repair `f1e7ef9`. The missing scene volumes are separate from the ThorVG/outliner icon issue.

## Findings

| Effect | CPU reference renderer | Current main GPU viewport |
|---|---|---|
| Global clouds | Settings reach `VolumetricMedia::March` | No camera-ray cloud rendering pass; live cloud edits do not reach existing GPU celestial records |
| Local cloud | Bounded-volume march implemented | No local-volume settings upload or visible-volume march |
| Height fog | `FogModel::Apply` implemented | No matching fog settings upload/composition |
| Atmospheric/aerial fog | Analytic surface-distance effect implemented | No matching authored fog settings upload/composition; physical sky atmosphere is a separate feature |
| Local volumetric fog | Bounded-volume march implemented | No local-volume settings upload or visible-volume march |

The GPU **does** have `CloudShadow.slang`. It computes sunlight attenuation at shaded surfaces from separately staged weather. It does not draw cloud bodies, draw local cloud/fog volumes, or apply the inspector's height/aerial fog. Its staging is assigned at level load, independently of the live cloud inspector settings.

This is not fixed by adding a missing C++ source to the build: the CPU model exists in headers, but the GPU equivalent and its upload/composition wiring are absent.

## Source trace

1. `Projects/Project-Zero/Source/CelestialSequence.cpp`: `Prepare` initializes the entities; `AppendRoster` exposes them to the outliner. `ApplyTo` supplies clouds, local volumes and fog to **VisibilityRaster**, the CPU reference renderer.
2. `Engine/GeometricRaster/VisibilityRaster.cpp`: calls `VolumetricMedia::March` and `FogModel::Apply` for scene pixels.
3. `Projects/Project-Zero/Source/GameExecution.cpp`: the GPU frame uploads `PackSkyRecord`, `PackMoonRecord` and `PackPostRecord` via `RefreshSky`, `RefreshMoons` and `RefreshPost`. It does not call `Celestial.ApplyTo` or upload local cloud/fog/analytic fog settings.
4. `Engine/Shaders/ReSTIRViewport.slang`: missed primary rays resolve sky plus rainbow; primary surface shading resolves without cloud/fog view-ray integration. The only cloud include is `CloudShadow.slang`.
5. `Engine/DeviceExchange/VisibilityExchange.h`: the volumetrics timing span is explicitly reserved for a future pass. A timing field is not evidence that the pass exists.

Earlier native scene proofs in `Exhibits/Workbench/Billboards/NativeSceneProof.cpp` render clouds/fog through `VisibilityRaster`. Those CPU proofs must not be described as proof that the normal Vulkan viewport implements these effects. The user's exact older executable was not available for comparison.

## Executed diagnostic

Built the actual current `CelestialSequence`, solver, catalogue and asset resolver with GCC 12.2, then changed each weather setting and compared the exact sky/moon/post upload records:

- Global cloud enabled/coverage/density/base edits: **0 / 0 / 0 record changes**.
- Height fog enabled/density edits: **0 / 0 / 0**.
- Aerial fog enabled/density edits: **0 / 0 / 0**.
- Local cloud enabled/density/position edits: **0 / 0 / 0**.
- Local fog enabled/density/position edits: **0 / 0 / 0**.
- Positive control, atmosphere Mie edit: **1 / 0 / 0** — the GPU sky record does change.

Direct CPU evaluator probes produce nonzero scattering/extinction for all five media. These are deliberately simple dense fixtures, not performance measurements or screenshots of the user's scene. Raw output: [wiring-audit.txt](WeatherEvidence/wiring-audit.txt).

Reproduce on Linux after dependency bootstrap:

```sh
python3 Exhibits/Workbench/WeatherWiring/RunAudit.py
```

This is a **diagnostic**, not a passing regression test that enshrines missing wiring. It reports changes; a future GPU repair should make the relevant weather data respond to edits and extend the audit to any new volume record. No GPU dispatch or Windows application execution was performed.

## A second, independent visibility gate

Defaults after `Prepare`:

- Global clouds: enabled.
- Height fog, aerial fog, local cloud and local fog: disabled.

An outliner row's presence/eye visibility does not turn on a separate inspector Enabled switch. That matters for the CPU path. However, enabling those switches does **not** fix the missing GPU connection, as the diagnostic confirms. Enabling every medium by default would conceal neither problem and could wash out existing scenes.

## Required repair

1. Pack live global/local cloud and fog settings, visibility gates, wind/time and quality budgets into a GPU volume record; keep it synchronized with the CPU `ApplyTo` path.
2. Implement the existing shared-extinction cloud/local-volume march in the GPU path, respecting the primary surface distance and supporting global plus local media simultaneously. Retain medium self-shadowing and the engine's Z-up convention.
3. Composite cloud/local-volume scattering and analytic height/aerial fog for sky and surface rays, including emissive/unlit early returns, before tone mapping. Do not replace them with billboards or an offline sky texture.
4. Handle temporal-history invalidation/rejection when weather parameters, local bounds or visibility change; wire real pass timings.
5. Verify rendered pixel changes, occlusion, enable/visibility gates, global/local overlap, inspector round-trips and shader compilation, then run the real Windows/Vulkan viewport.

The icon repair remains valid; it only restores the artwork used to represent these entities, not their missing GPU scene rendering.
