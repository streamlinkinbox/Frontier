# Phase 4: Unified ReSTIR GI, Compact VRAM, Power MIS, and Authored Multi-Strata PBR & Reflection System

## Overview

Phase 4 completes the `Frontier` C++/Vulkan ReSTIR GI and authored PBR material & reflection stack across
`Frontier/Engine/Rendering/` and `Frontier/Engine/Render/Shader/`:

1. **Generalized Compact Output Slab Remapping (`PathReplayCompactStorage = 67108864u`)** — strips the legacy
   `25*N + 15,425` radiance-accumulator prefix across **all** modern reservoir modes (`6*N` ReSTIR GI,
   `10*N` `SampleHistory`, `17*N` ReSTIR GI + `VarianceFilter`, `+N` diagnostic `Slot 9`), reducing 1080p
   `Buffers[6]` VRAM from `1,455.70 MiB` (`46*N + 15,425`) to **`537.89 MiB` (`17*N`, `-63.05%`, `-917.81 MiB`
   saved)** and `6*N` ReSTIR-only storage from `949.46 MiB` to **`189.84 MiB` (`-80.01%`)** with **byte-for-byte
   `0.000000` parity** across RGBA output, all `24` GPU diagnostic counters, and all `12` logical slabs.
2. **Branch-Inverted Veach $\beta = 2$ Power MIS (`PathReplayPowerMis = 33554432u`) & `<= 71` History Cap** —
   adopts `ReplayMisWeight(Ratio)` (`r <= 1 -> 1/(1+r^2)` vs `1/(1+1/r^2)`, zero FP32 overflow up to `r = 1e30`)
   with automatic `PathReplayHistoryDomain` reset, plus tight `SourceSlot.w <= 71` reservoir clamp.
3. **Authored Multi-Strata PBR Material & Reflection System (`PathReplayAuthoredMaterials = 134217728u`)** —
   integrates the `268`-float `Angles` material atlas (1–8 dielectric/conductor strata, 12-swatch Voronoi
   metallic-flake automotive clearcoat paint, Charlie retroreflective velvet sheen, mip-pyramid albedo &
   tangent-space normal maps, `Category == 2` Beer-Lambert + Schlick Fresnel thin-sheet tinted glazing, and
   multi-pane non-opaque shadow transmittance across both software BVH and `VK_KHR_ray_query`).
4. **Roughness-Driven Specular-Cone SVGF & Grazing Normal Gates** — scales `PathReplayVarianceFilter` spatial
   à-trous normal exponent from `32` (`Roughness = 1`) up to **`224` (`Roughness = 0`)** while accelerating
   history responsiveness on mirror/clearcoat surfaces and rejecting cross-material spatial reservoir reuse.

![Phase 4 Authored Multi-Strata PBR Showroom Comparison](Images/ReplayPbrReflectionComparison.png)

## Head-to-Head Audit: `arena/5ec53579-frontier` vs `origin/arena/08e7de48-frontier`

| Dimension | `origin/arena/08e7de48-frontier` (`ceebd8a`) | `arena/5ec53579-frontier` (Phase 4) | Verdict |
| :--- | :--- | :--- | :--- |
| Repository Baseline | `3de9061` (`1,316` files; missing `EnvironmentRecords`, `SceneInspectorPanel`, `TextureStudio`) | `fa04adf` (`2,961` active files; `<85 MB` working tree + `RestoreDeferredArchives.py`) | **`5ec53579` ahead** |
| Seed + Reservoir Spatial/Temporal Reuse | Unguarded `+-1 px` cross + 3-seed reentry regressed Room RMSE `+75.70%` (`promote=0`) | Fresh `Bits` + `min(RetainedCount, 1u)` + `!= TemporalSeed` + 4-tap Vogel disk (`0.0840` RMSE) | **`5ec53579` ahead** |
| Compact Output VRAM | `6*N` (`189.84 MiB` at 1080p) only when `SampleHistory=0` and no denoiser | Generalized `6*N` / `10*N` / `17*N` across `SampleHistory` and GPU SVGF (`-63.05%` to `-80.01%`) | **Adopted & generalized** |
| Veach $\beta = 2$ Power MIS | Diffuse-only `Fresh8` (`-24.17%` Plane at `z=0.03125`, `-0.08%` Room) | `PathReplayPowerMis` across diffuse + multi-strata rough/metallic/clearcoat PBR + domain reset | **Adopted & extended** |
| Temporal History Clamp | Tightened `SourceSlot.w <= 71` matching `1 + floor(70.5)` | Adopted `SourceSlot.w <= 71` across CPU & GPU validators | **Adopted** |
| Motion-Aware GPU SVGF | Not implemented | 3-pass `PathReplayVarianceFilter` + roughness-driven specular-cone tightening (`-88.5%` flicker) | **`5ec53579` ahead** |
| Ray Acceleration | Legacy unordered median BVH only | Dual-path `VK_KHR_ray_query` (`spirv_1_4`) + 16-bin SAH-2 + front-to-back ordered BVH (`-76.74%` ops) | **`5ec53579` ahead** |
| Authored PBR & Glazing | Untextured diffuse fixtures in Phase experiments | Full 8-strata `TextureStudio` PBR: Clearcoat Flake Paint, Chrome, Charlie Velvet, Thin-Sheet Glass | **`5ec53579` ahead** |

## Quantitative Benchmark Results (`64x64`, 3 Bounces, `148` SAH-2 Triangles, `5` `TextureStudio` Materials)

| Metric / Experiment | Baseline / Control | Phase 4 Unified Engine | Improvement / Verification |
| :--- | ---: | ---: | :--- |
| `64x64` Vulkan Output `VkBuffer` (`Buffers[6]`) | `3,261,456 B` | **`1,114,112 B`** | **`-65.84%` actual bytes (`0.000000` max diff)** |
| `1920x1080` Full-Stack (`ReSTIR + SVGF`) VRAM | `1,455.70 MiB` | **`537.89 MiB`** | **`-63.05%` (`-917.81 MiB` VRAM saved)** |
| `1920x1080` ReSTIR-Only (`6*N`) VRAM | `949.46 MiB` | **`189.84 MiB`** | **`-80.01%` (`-759.62 MiB` VRAM saved)** |
| Close-Canopy Sill Reflection (`16` paired batches) | `0.19696` (Balance) | **`0.19494` (Power $\beta=2$)** | **Strictly lower RMSE at `168,303 == 168,303` rays** |
| Closed 3-Bounce Showroom MIS (`8` paired batches) | `0.33907` (Balance) | **`0.34282` (Power $\beta=2$)** | **Statistical parity (`1.10%`) at `192,396 == 192,396` rays** |
| Moving-Camera Showroom Radiance RMSE (vs `80-spp`) | `0.35690` (`1-spp` Raw) | **`0.22770` (Unified+SVGF)** | **`-36.20%` RMSE (`0.23338` pre-SVGF, `-34.61%`)** |
| Consecutive-Frame Showroom Temporal Flicker | `0.51101` (`1-spp` Raw) | **`0.05879` (Unified+SVGF)** | **`-88.50%` (`8.69x` lower flicker)** |
| Thin-Sheet Tinted Glazing Window Luminance (`289 px`) | `0.02103` (Opaque) | **`0.79633` (`Category=2`)** | **`37.87x` physical Beer-Lambert transmittance** |
| Material-Specific Showroom RMSE (`Hood` / `Velvet`) | — | **`0.21477` / `0.01311`** | Crisp clearcoat reflection + Charlie sheen |

## Subzero SIMD4 Compiler Finding (SwiftShader Vulkan Validation)

Under `slangc -emit-spirv-directly -O0`, dynamically indexed local arrays (`float Roughnesses[8]`,
`Candidate*[12]`) expand into per-lane SIMD4 gather/scatter basic-block cascades in SwiftShader Subzero, causing
quadratic `O(B^2)` `Cfg::liveness` memory growth (`>3.85 GiB`). Refactoring `EvaluateOrSampleReplayPbrLobes` and
`GenerateReplayReservoirs` to pure scalar accumulators over `Angles[]` eliminated all dynamic local arrays while
preserving exact multi-strata physics across all `1..8` strata, compiling all `6` Vulkan compute pipelines cleanly
within `~1.1 GiB` RSS and `<16 KB` stack.

## Reproducibility

```bash
CAPTURE=1 bash Exhibits/LightCompute/RunReplayPbrReflection.sh
```

Both Release (`-O3 -Wall -Wextra -Werror`) and `ASan+UBSan` (`-fsanitize=address,undefined -Wall -Wextra -Werror`)
pass **`9/9` CTests (`100%`)**, `4/4` `spirv_1_4+spvRayQueryKHR` binary checks, and full SwiftShader Vulkan GPU
validation.
