# VehicleHMI ReSTIR GI — 1:1 C++/Vulkan/Slang port

## Status

| Area                                   | State        | Evidence (`Exhibits/RestirGiParity`)                            |
|----------------------------------------|--------------|-----------------------------------------------------------------|
| WGSL → Slang (51 units, 29 entries)    | ✔️ ported    | compiles; outputs below                                         |
| `DeviceExchange` (raster + compute)    | ✔️ ported    | bit-identical raster and compute targets                       |
| Scene, BVH, cards, distance volumes    | ✔️ identical | uploads byte-identical, volumes/captures 100 % bit-identical    |
| Cascades, shadow, sky, surface, direct | ✔️ identical | 100 % bit-identical at refreshes 0 and 11                       |
| ReSTIR GI, reflection, SVGF, display   | ✔️ parity    | within the demo's own run-to-run variation                      |
| Host encoders (constants, cascades, TLAS, tiles, bricks) | ✔️ identical | byte-identical over 12 refreshes             |
| `LightingSystem` (`LuminaireRegister`), `EncodeEnvironment`, `CelestialSolver` | ✔️ identical | refresh constants + luminaire register byte-identical over 12 refreshes |
| Editor viewport presentation           | ✔️ live      | Everest Scene Viewport on the editor's device (`Exhibits/SceneViewport`) |
| Environment (clouds, fog, flare, sun, moons, stars, planets) | ✔️ overlaid  | PlanetaryAtmosphere over the refresh, same device and queue |

## Source of truth

- SultanAladin/Frontier `arena/e9bd7db5-frontier` @ `6afb6f00b5f25c45f7cda92c7da65b64845768f3`,
  `Experimental/VehicleHmiPrototype/` (WebGPU JS + WGSL), plus `ReferenceApps/Working/` (imported by the demo).
- Reference run: the demo itself under Node + Dawn (`webgpu@0.6.2`) on SwiftShader, recorded by
  `Exhibits/RestirGiParity/Tools/CaptureHooks.mjs` (write log, command log, scene topology, raw targets).

## Method

1. Shaders: `Frontier/Projects/Project-VehicleHmiDemo/Contract/TranslateShaders.py` makes a first token-level pass
   (declarations, entry points, WGSL builtin semantics via `IntrinsicParity.slang`, matrix products, rename table);
   the output is then corrected by hand. Matrices: WGSL columns are stored as Slang rows (`-matrix-layout-row-major`),
   so WGSL `M * v` is Slang `mul(v, M)`.
2. Host: each demo module is ported statement for statement; JS doubles stay doubles, `Float32Array` stores round
   through `float` (`StoreSingle`), JS `Math.hypot` is reproduced (V8 algorithm), stable sorts stay stable.
3. Device: `DeviceExchange` keeps WebGPU ordering — queue writes precede the next submission, every dispatch is
   ordered after earlier work, images live in `GENERAL`, raster viewports are y-flipped like Dawn's Vulkan backend.
4. Parity: every host upload is compared byte for byte with the demo's write log; every device target is compared
   texel for texel with the demo's dump of the same refresh.

## Naming rulings (SKILL-Naming.md conflicts in the demo vocabulary)

| Demo                         | Port                        | Rule hit              |
|------------------------------|-----------------------------|-----------------------|
| `FrameRecord` / `Frame`      | `RefreshConstants` / `Refresh` | `Frame`, `Record`  |
| `FrameIndex`                 | `RefreshIndex`              | `Frame`               |
| `FrameDescriptor.wgsl`       | `RefreshDescriptor.slang`   | `Frame`               |
| `*Record` structs            | `*Profile` / `PackedReservoir` / `*Constants` | bare `Record` |
| `SceneRecords.wgsl` etc.     | `SceneLayouts.slang` etc.   | `Record`              |
| `RayTraversal.wgsl`          | `RayIntersection.slang`     | `Traversal`           |
| `DistanceTraversal.wgsl`     | `DistanceMarch.slang`       | `Traversal`           |
| `ReflectionTraversal.wgsl`   | `ReflectionTrace.slang`     | `Traversal`           |
| BVH `Node`                   | `Entry`                     | `Node`                |
| `Emission`                   | `Incandescence`             | `Emission`            |
| `OccupantRole` / `Roles`     | `OccupantParticipation`     | `Role`                |
| `Footprint`                  | `Coverage`                  | `Footprint`           |
| `VarianceKernel`             | `VarianceFilter`            | `Kernel`              |
| `LightingSystem.js`          | `LuminaireRegister`         | `system`              |
| `ClusteredLightBuffer`       | `ClusteredLightRegister`    | `Buffer`; owner: no `Table` |
| `compositePlume`, `composite` sets | `BlendPlume`, `Fragmented` sets | `compose`     |

Owner's review of the viewport code (2026-11-01):

| Was                              | Now                                         | Reason                         |
|----------------------------------|---------------------------------------------|--------------------------------|
| `Frontier/Engine/Illumination/`  | `Frontier/Engine/Rendering/GlobalIllumination/` + `Rendering/SceneViewport/` | owner: technical folder names |
| `Shaders/Illumination/`, `ShaderArchives/Illumination` | `Shaders/GlobalIllumination/`, `ShaderArchives/GlobalIllumination` | same |
| namespace `Frontier::Illumination` | `Frontier::GlobalIllumination`            | follows the folder             |
| `AdoptedDevice`, `Adopt`, `QueryAdoption`, `AdoptTexture` | `VulkanDeviceAccess`, `AttachDevice`, `QueryDeviceAccess`, `WrapTexture` | owner: odd name; matches `VulkanRasterAccess` |
| `LuminaireTable`, `LuminaireKind` | `LuminaireRegister`, `LuminaireCategory`   | owner: no `Table`; `Kind`      |
| `EnvironmentComposition`         | `EnvironmentOverlay`                        | owner: `compose` is banned     |
| `CameraState`, `LightState`      | `CameraPose`, `LightSolution`               | `state`                        |

## Editor viewport and environment (2026-11-01)

- **Device:** `VulkanPresentation` asks for a Vulkan 1.3 device with the port's features (`SceneRendering`);
  `DeviceExchange::AttachDevice` and `VulkanComputeExchange::AttachDevice` borrow it (`VulkanDeviceAccess.h`), so the
  refresh, the environment and the interface share one queue. The present submission ends with a full dependency; the interface samples the
  presented image in `GENERAL`.
- **Viewport:** `Frontier/Engine/Rendering/SceneViewport/SceneViewport` brings up the proving ground
  (`EngineContent/EnvironmentArchives/ProvingGround`, the demo's own scene, vehicle deactivated) and refreshes it through the demo's sky path
  (`AdvanceCelestial → ResolveLight → EncodeEnvironment`, light-change relighting) and `LuminaireRegister` every refresh.
  `Projects/Everest/Source/EverestSceneViewport` maps the level's records (Solar, OpticalFlare, Atmosphere, Clouds, Stars,
  Moons, Fog, Wind, RenderPipeline) onto it; `EverestWorkspace` owns the orbit/fly camera and draws the image.
- **Environment (owner's choice: PlanetaryAtmosphere, not the demo's own clouds/flare):** `EnvironmentOverlay`
  imports the resolved radiance and depth into the environment's visibility layers (`EnvironmentVisibility.slang`), runs
  every PlanetaryAtmosphere stage except its own visibility raster (`ExecuteOverVisibility`), restores the radiometric
  scale and presents the overlaid image through the port's tone mapping. The demo's own fog, aerial perspective and flare are
  switched off in the refresh; its sky still lights the scene (the environment does not relight the illumination).

## Scope decisions

- Ported: everything the demo executes for the illumination image with clouds, cloud shadows, lens flare, smoke,
  sparks and tyre marks switched off (the reference run switches them off too).
- Not ported: those effect systems, vehicle physics, HMI/cabin overlays, editor UI of the demo. The editor overlays
  the PlanetaryAtmosphere environment instead of the demo's clouds and flare (owner's choice, 2026-11-01).
- Removed from the engine: the earlier non-1:1 CPU illumination chain (see Tracker).
