# Native rendering port audit — 2026-10-28

Historical recovery snapshot. The subsequent [seed-reservoir continuation](ReplaySeedReservoir.md) resolves the
tooling blocker and adds default-off local RIS plus spatial replay; general ReSTIR/editor integration remains open.

**Clouds and fog reached shared C++ and offscreen Vulkan, not just browser prototypes. ReSTIR GI is incomplete.**
The editor integration and physical-game-GPU acceptance are not established by those offscreen results.

## Recovery and provenance

- Destination: `streamlinkinbox/Frontier`, session branch `arena/08e7de48-frontier`.
- Destination base: `68f301600c72b442898313aeb49d57be0baac2f0`; initially only README and an uploaded patch.
- Recovered source: `eosclient0001-rgb/Frontier`, `arena/672a179a-frontier`,
  `3de9061ab614856de03ef61b534c641061d14a53`. Fetch only; no other branch was checked out or changed.
- `arena/8da5a2dd-frontier` lookup returned HTTP 404 in both repositories. This is not a recovery of that branch.
- Imported relevant shared source, UI source, automation, rules and the LightCompute/environment/night-sky exhibits.
  This is a selective recovery, not a complete clone of all content and exhibits. The uploaded patch is untouched.
- Large content archives, unrelated exhibits and experimental reference trees were not imported. Root editor
  packaging/build readiness is not claimed. The isolated LightCompute and environment builds were exercised.
- No commits, pushes, PRs or GitHub Actions runs were initiated. Files are working-tree changes on the session branch.

## Ported versus integrated

✔️ means source plus retained validation exists; it does not mean production-editor integration is complete.
All paths below are relative to the repository root unless linked locally.

| Feature | Native implementation | Established evidence | Still incomplete |
| --- | --- | --- | --- |
| ✔️ Clouds | `Frontier/Engine/Rendering/CloudField.*`; `Shaders/LightInfluence.slang` | Vulkan volume, sky, wind, spatial Sun shadows, finite-budget comparisons | Editor, final appearance, game-GPU timing |
| ✔️ Fog | `Frontier/Engine/Rendering/FogMedium.*`; shared Slang | Height/local fog, shadowed shafts, emissive facets, pane transmittance | Unified production viewport |
| ✔️ Sun/sky | `AtmosphereModel.*`, `CelestialModel.*`; shared Slang | Native analytic checks; Vulkan Sun/cloud extinction | New CPU optics are not all GPU ports |
| ✔️ Direct reservoirs | `ReservoirSampling.*`; shared lighting shader | Bounded temporal/spatial direct-light reuse and rejection | Physical GPU acceptance |
| 🚧 ReSTIR GI | `PathReplayDomain.*`, `PathReplayVulkan.*`, replay Slang | Offscreen bounded path replay and sample-seed retracing | Combined estimator, broad motion, production submission |
| 🚧 Reflection/PBR/glass | Shared material/scattering code and Slang | Rough reflection, dielectric and posed glazing fixtures | Complete unified moving-scene renderer |
| 🚧 UI | `Engine/Interface/`, `UI/Editor/` | Native implementation present; inherited UI reports elsewhere | Not rebuilt here; no replay viewport integration |
| 🚧 Weather/optics | `WeatherEffects.*`, `LensFlareModel.h` | Native model tests; separate sky evidence | Not full Vulkan integration |
| 🔴 Hardware RT | No executed hardware-ray-query evidence established | Retained Vulkan device is SwiftShader CPU type 4 | Physical RTX/GTX and hardware tracing |
| 🔴 Non-ray-traced GI | No completion evidence established | Replay uses software triangle BVH tracing | Separate validated fallback path |

Detailed inherited scope: [renderer progress](RendererProgress.md), [LightCompute overview](README.md),
[environment model](../EnvironmentPipeline/README.md), [sample history](ReplaySampleHistory.md).
UI, vehicle physics/Jolt and editor work remain outside this continuation's priority.

## Visual evidence — inherited, not newly rendered

These are actual retained Vulkan fixture captures from the recovered checkpoint, not generated mockups.
They establish the limited fixture described by the corresponding report, not current Windows/editor rendering.

### Clouds: 32 versus 64 steps

![Cloud budget comparison](Images/CloudBudgetComparison.png)

Left: 32 steps; right: finite 64-step comparison; two wind times. Native/Vulkan RGB agreement and finite-budget
image errors are retained in [CloudBudget.txt](Results/CloudBudget.txt). Halving outer sample counts is not a
measured twofold frame-time improvement. Full-image linear RGB RMSE: `0.00207406` and `0.00187121`.

### Fog: area illumination disabled versus enabled

![Fog facet comparison](Images/FogFacetComparison.png)

See [pane transmittance](PaneTransmittance.md), the overview and [fog sampling results](Results/FogSampling.txt).
The latter's 32-shadow-step RMSE versus finite 128-step reference is `0.000836603` at wind 0 and
`0.0000789365` at wind 120. These are quality measurements, not frame timings or real-world reference images.

### ReSTIR continuation: current paths, not cached old colour

| Four fresh paths | Current-surface paths using retained seeds |
| --- | --- |
| ![Fresh paths](Images/ReplaySampleFourFresh.png) | ![Retraced paths](Images/ReplaySampleRetraced.png) |

The diagnostic captures are 64×64. Numerical reference tests use a separate 32×32 fixture.
No denoising, editor or visual-fidelity acceptance is implied. See [full contract](ReplaySampleHistory.md).

| Four-bounce reference comparison | Red RMSE |
| --- | ---: |
| Four fresh paths | 0.153027 |
| Four current-surface paths, three retained seeds | 0.151685 |

The approximately 0.88% lower RMSE is a fixture observation, not a demonstrated general improvement.
Both evaluate four current paths at retained points; the retraced mode also requires prior frames and bank
traffic. The control uses four submissions. No equal-time or speedup claim follows. Explicit-facet sampling
has slightly worse retraced RMSE (`0.013665` versus `0.013592`), so there is no consistent quality win established.
The half-pixel test retained 970 seed banks and rejected all 22 changed-token donors.

## Fresh verification in this workspace

Platform: Linux x86-64, GCC 12.2.0, CMake 4.4.4, Vulkan headers v1.4.321. Source is the recovered checkpoint
plus this audit and recovery work; no shader/estimator changes. Local executables are verification tools,
not delivered Windows binaries.

| Check | Fresh outcome | Evidence |
| --- | --- | --- |
| LightCompute Release build | ✔️ Built, including C++ Vulkan recorder and validation executable | Reproduction below |
| Release native CTests | ✔️ 5/5 | [Release log](Results/PortAudit-release.txt) |
| Host ASan/UBSan native CTests | ✔️ 5/5 | [Sanitizer log](Results/PortAudit-sanitized.txt) |
| Native environment models | ✔️ 73/73 | [Environment log](Results/PortAudit-environment.txt) |
| Replay shader execution/captures | 🔴 Blocked: missing `slangc`; SwiftShader also absent | Command below exits 127 |
| Windows / physical GPU / editor | 🔴 Not run | No claims |

The Vulkan recorder CTest uses command callbacks, not a GPU submission. The five native CTests are not a
substitute for the replay image/reference regressions. Old release/sanitizer GPU logs remain inherited evidence.

### Reproduce fresh host checks

Requires CMake, GCC/C++20 and Vulkan headers; downloaded dependencies belong in ignored `ExternalPackages/`.
The local CMake installation is `_ClaudeScratch/venv/bin/cmake`; substitute a system installation if available.

```bash
export PATH="$PWD/_ClaudeScratch/venv/bin:$PATH"
cmake -S Exhibits/LightCompute -B _ClaudeScratch/build/replay-audit -DCMAKE_BUILD_TYPE=Release
cmake --build _ClaudeScratch/build/replay-audit --parallel 2
ctest --test-dir _ClaudeScratch/build/replay-audit --output-on-failure
cmake -S Exhibits/EnvironmentPipeline -B _ClaudeScratch/build/environment-audit -DCMAKE_BUILD_TYPE=Release
cmake --build _ClaudeScratch/build/environment-audit --parallel 2
_ClaudeScratch/build/environment-audit/EnvironmentPipelineValidation
```

For host sanitizers configure a separate Debug directory with
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`; build the five named validation targets from
`Exhibits/LightCompute/CMakeLists.txt`, then run CTest with
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.

Blocked render command (does not overwrite inherited results):

```bash
RESULTS="$PWD/_ClaudeScratch/logs/replay-audit-gpu.txt" bash Exhibits/LightCompute/RunReplaySampleHistory.sh
```

Observed: `ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc: No such file or directory`.
It fails before shader compilation or Vulkan execution. See the original script for driver/compiler overrides.

## ReSTIR completion gates — highest priority

1. Restore pinned shader compiler/software Vulkan tooling and reproduce inherited numerical/capture baselines.
2. Specify and implement a valid combined reservoir/retrace estimator with correct target, proposal and
   selection normalization. Do not just remove the mutually-exclusive recorder guards: selected seeds are
   radiance-conditioned, unlike the unselected seeds used by the current averaging experiment.
3. Test static convergence and equal-work/equal-time controls, then camera motion, disocclusion, light/material
   revisions, geometry changes, mode toggles, allocation changes and failed-submission history invalidation.
4. Retain actual before/after captures and independent numerical comparisons. Set a game workload/resolution
   and frame/memory budget before claiming a game-performance improvement; report any quality/cost tradeoff.
5. Wire the verified path into production C++ scene/frame submission and the viewport. Demonstrate actual frames,
   correct ownership/barriers, and history recovery. Shared C++ placement alone is not this integration.
6. Validate Windows and physical GPUs; disclose unavailable hardware rather than converting CPU-driver times to FPS.

**Checkpoint outcome:** relevant native work recovered and host baseline revalidated. ReSTIR remains incomplete;
no new estimator, new rendering improvement, fresh render capture or finished engine integration is claimed.
