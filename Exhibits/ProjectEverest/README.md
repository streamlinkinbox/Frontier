# Project Everest — Phase 3 Dual-Tier 3D Renderer, 10 Engine Observatories, 8-Module Vehicle, and Tier-3 Domain Studios

Retained evidence and validation harness for **Project Everest** (`Frontier/Projects/Project-Everest/ProjectEverest.frontier`):
- **Dual-Tier 3D Proving Grounds Renderer & Slang Shaders (`Frontier/Engine/Rendering/DualTierRenderer.{h,cpp}` + `Frontier/Engine/Rendering/Shaders/*.slang`)**:
  - **Scene geometry (`318` triangles / `335` 16-bin SAH BVH enclosures / `4` SDF cascades / `36` SurfaceCard texels)**:
    Proving Grounds apron + red/white kerbs + lane markings, Coral-Red & Mint-Green indirect colour-bleed walls,
    overhead emissive gantry, and 3D `ControlVehicle` fastback chassis with cabin headrests, OLED HMI cluster, 4 wheel
    assemblies, LED headlamp cones, brake light bar, and laminated **Dielectric Cabin Glazing** (`IOR = 1.52`, `6 mm`
    Beer-Lambert acoustic tint).
  - **Tier A — RTX Hardware Raytracing (`HardwareRayQueryRtx`)**: `PathReplayRayQuery` (`VK_KHR_acceleration_structure`
    + `VK_KHR_ray_query` capability negotiation), triangle BVH ray-query visibility, reservoir-resampled indirect GI +
    2-step normal-weighted edge-stopping SVGF spatial filter, glossy reflections, and Snell + Fresnel dielectric glass.
  - **Tier B — GTX / Lower-End Fallback (`ToroidalSdfBvhGtx`)**: 4 nested toroidal 3D SDF cascades (`SceneClip0..3`,
    `ToroidalDistanceClipmap.slang`), multi-bounce `SurfaceCards` radiosity (`SurfaceCardRadiosity.slang`, residual
    `1.45e-7`), and 2-level SAH triangle `OrderedSoftwareBvh` reflections (`OrderedBvhReflections.slang`) gated by
    roughness cutoff.
  - **6 Viewport Diagnostic AOVs**: `FinalLit`, `IndirectIrradiance`, `SpecularReflections`, `DistanceCascades`,
    `DielectricGlazing`, and `SurfaceNormals`.
- **10 Global Reusable Engine Observatories (`Frontier/Engine/Scene/EnvironmentRecords.{h,cpp}` + `Frontier/Engine/Interface/SceneInspectorPanel.{h,cpp}`)**:
  `FolderRecord`, `SunRecord`, `LensFlareRecord`, `AtmosphereRecord`, `CloudRecord`, `WindRecord`, `FogRecord`
  (`FogMedium`), `StarsRecord` (Hipparcos planisphere + magnitude histogram), `MoonRecord` (`CelestialModel` 29.53-day
  synodic terminator + log angular tape), and `WeatherRecord` (`WeatherEffects` forked lightning + `343 m/s` thunder
  acoustics + chunked `0..4 mm` surface deposit retention grid).
- **Project Everest Custom Inspectors & Tier-3 Domain Studios (`ProjectEverestScene.{h,cpp}` + `DomainStudioPanels.{h,cpp}`)**:
  - 26 initial scene records enclosed across 6 `FolderRecord`s (`Render`, `Environment`, `Forcefield`, `Vehicle`,
    `Effects & Studios`, `Level`).
  - 8-module `EverestVehicleRecord` (`ControlVehicle`: `Overview`, `Chassis`, `Engine`, `Tyres`, `Suspension`, `Lights`,
    `HMI & Cabin`, `Mirrors`) with live 3D **Collision X-Ray** wireframe chassis + 4 wheel contact hubs in the viewport.
  - Docked `DomainStudioPanel` switching cleanly into the bottom workspace slot for **Fracture Voronoi Studio**, **Fluid
    & Plume Lattice Studio** (verified against `ValidatePlumeLatticeFootprint`), and **Particle Emitter Studio**.

## Rendered native C++ UI & 3D Dual-Tier evidence (`1920 x 1080`, CPU rasterized)

1. **`01-ProjectBrowser-Library.png`** — `ProjectBrowserPanel` opens and enumerates **Project Everest** beside peer `.frontier` projects.
2. **`02-ProjectBrowser-SelectEverest.png`** — Reader view opened on **Project Everest** prior to **Open in Frontier** hand-off.
3. **`03-Folder-Outliner-Observatory.png`** — 3-column workspace with 26 records across 6 `FolderRecord` groups, 3D RTX Proving Grounds viewport (`FinalLit`), and `Environment` **Folder Observatory**.
4. **`04-Sun-Observatory-Overview.png`** — Global Engine **Sun Observatory** (`ENVIRONMENT / 02`) coupled live to 3D solar shadows, sky dome, and mint/coral colour bleed on `ControlVehicle`.
5. **`05-LensFlare-And-DielectricGlazingAOV.png`** — Global Engine **Lens Flare Observatory** (`ENVIRONMENT / CAMERA OPTICS`) paired with the **`DielectricGlazing` (`Glass`)** viewport AOV isolating the refracting cabin canopy.
6. **`06-Atmosphere-And-IndirectGiAOV.png`** — Global Engine **Atmosphere Observatory** (`ENVIRONMENT / 03`) paired with the SVGF-filtered **`IndirectIrradiance` (`GI`)** viewport AOV showing red/mint wall bounce across the apron and chassis.
7. **`07-Clouds-And-ToroidalSdfCascadesAOV.png`** — Global Engine **Clouds Observatory** (`ENVIRONMENT / 01`) paired with the GTX **`DistanceCascades` (`SDF`)** colour-coded `SceneClip0..3` cascade rings.
8. **`08-Wind-And-GtxSdfBvhFallback.png`** — Global Engine **Wind Forcefield Observatory** (`FORCEFIELD / SHARED WEATHER`) paired with the live **`GTX SDF + BVH`** fallback viewport.
9. **`09-Fog-Observatory-KoschmiederTargets.png`** — Global Engine **Fog & Aerial Perspective Observatory** (`ENVIRONMENT / 04`) with Koschmieder meteorological range targets and exponential fog bank across the 3D scene.
10. **`10-Stars-Observatory-NightPlanisphere.png`** — Global Engine **Stars & Planisphere Observatory** (`CELESTIAL / HIPPARCOS SKY`) at night (`Sun -28°`) with stellar sky dome and LED matrix headlamp cones on the apron.
11. **`11-Moon-Observatory-Selenograph.png`** — Global Engine **Moon Selenographic Observatory** (`CELESTIAL / SELENOGRAPHIC DISC`, `83% Waxing`, `0.52° Luna` log tape) with night sky reflections on the fastback decklid.
12. **`12-Weather-StormLightningAndWetApron.png`** — Global Engine **Storm & Surface Deposit Observatory** (`WEATHER / STORM & SURFACE DEPOSIT`) with forked return-stroke lightning preview, `343 m/s` acoustic arrival, and wet reflective apron.
13. **`13-Weather-SurfaceDepositSnowGrid.png`** — Chunked `0..4 mm` apron snow retention grid (`Deposit grid` tab) showing the dry under-chassis shadow matched by snow accumulation across the 3D proving grounds.
14. **`14-ControlVehicle-8Tab-CollisionXRay.png`** — Project Everest 8-module **`ControlVehicle` Inspector** (`Chassis` tab + `Solar Copper` clearcoat) with live 3D **Collision X-Ray** wireframe chassis & wheel hubs in the viewport.
15. **`15-Tier3-FractureVoronoiStudio.png`** — Dedicated Tier-3 **Fracture Voronoi Studio** docked in the bottom workspace slot (`ShatterTestBarrier`, interactive impact epicentre + `96x52` cell crack decomposition).
16. **`16-Tier3-FluidPlumeStudio.png`** — Dedicated Tier-3 **Fluid & Plume Lattice Studio** (`ThermalPlumeEmitter`, `96x96x128 · 31.5 MiB VRAM · VALIDATED` against `ValidatePlumeLatticeFootprint`).
17. **`17-DualTier-GTX-Fallback-LightTheme.png`** — Dual-tier GPU renderer switched to **`GTX SDF + BVH Fallback`** + Golden-hour solar preset + dynamically spawned `Folder 01` + **Control Centre Light Theme** (`Dark Inspector luma = 33.83`, `Light Inspector luma = 249.67`).

![01 Project Browser Library](01-ProjectBrowser-Library.png)
![02 Project Browser Select Project Everest](02-ProjectBrowser-SelectEverest.png)
![03 Folder Outliner and 3D RTX Viewport](03-Folder-Outliner-Observatory.png)
![04 Sun Observatory](04-Sun-Observatory-Overview.png)
![05 Lens Flare and Dielectric Glazing AOV](05-LensFlare-And-DielectricGlazingAOV.png)
![06 Atmosphere and Indirect GI AOV](06-Atmosphere-And-IndirectGiAOV.png)
![07 Clouds and Toroidal SDF Cascades AOV](07-Clouds-And-ToroidalSdfCascadesAOV.png)
![08 Wind and GTX SDF BVH Fallback](08-Wind-And-GtxSdfBvhFallback.png)
![09 Fog Observatory and Koschmieder Targets](09-Fog-Observatory-KoschmiederTargets.png)
![10 Stars Observatory Night Planisphere](10-Stars-Observatory-NightPlanisphere.png)
![11 Moon Observatory Selenograph](11-Moon-Observatory-Selenograph.png)
![12 Weather Storm Lightning and Wet Apron](12-Weather-StormLightningAndWetApron.png)
![13 Weather Surface Deposit Snow Grid](13-Weather-SurfaceDepositSnowGrid.png)
![14 ControlVehicle 8-Module Collision X-Ray](14-ControlVehicle-8Tab-CollisionXRay.png)
![15 Tier-3 Fracture Voronoi Studio](15-Tier3-FractureVoronoiStudio.png)
![16 Tier-3 Fluid Plume Studio](16-Tier3-FluidPlumeStudio.png)
![17 Dual-Tier GTX Fallback Light Theme](17-DualTier-GTX-Fallback-LightTheme.png)

## Reproduction & verification

- **Platform / compiler tested**: Linux x86_64 sandbox (`g++ 12.2.0`, `-std=c++20 -O2 -Wall -Wextra`, plus `-std=c++17`
  and `-fsanitize=address,undefined` on portable suites). `slangc` and a Vulkan ICD (`libvulkan.so`) are not installed
  in this Linux sandbox; SPIR-V compilation and native Windows MSVC / Vulkan swapchain execution remain untested here.
- **Suites verified**:
  - `ProjectEverestValidation` (17 `1920x1080` PNGs + numerical assertions): **PASS** (`ProjectEverestChecks.json`).
  - `SceneDirectoryValidation` (Development profile): **45 passed, 0 failed**.
  - `SceneDirectoryValidation` (Shipping profile, `-DFRONTIER_SHIPPING_PROFILE=1`): **17 passed, 0 failed**.
  - `LogStreamValidation` (`-fsanitize=address,undefined`): **69 passed, 0 failed**.
  - `ProjectBrowserValidation`: **PASS** (770 drawn states).
  - `FrontierHeadless` (`WorkspaceApplication` C++17 `--project=ProjectEverest.frontier --headless`): **PASS**.
