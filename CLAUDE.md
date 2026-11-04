# Frontier — Agent Instructions

Frontier is a general-purpose, cross-platform editor with reusable engine code.
The legacy migration is complete. Do not treat this as an unfinished 1:1 migration target.

## Mandatory reading — every session

Read all four documents at session start, before naming, formatting, editing, or presenting work.
They are authoritative and override agent defaults within their scope:

- `EngineDocs/AgenticInstuctions/SKILL-Naming.md` — all naming, banned words, and name construction.
- `EngineDocs/AgenticInstuctions/SKILL-formatting.md` — source and document formatting.
- `EngineDocs/AgenticInstuctions/SKILL-Emoji.md` — the only approved emojis and permitted usage.
- `EngineDocs/AgenticInstuctions/SKILL-CompactOutputProtocol.md` — compact output; tables and diagrams over prose.

Report missing rules before affected work. Do not silently skip them or invent exceptions.
These paths remain authoritative until an approved relocation updates every reference.

## Content, formats, and rendering

- Use **Content Directory**, `ContentDirectory`, and `EngineContent/`.
  Do not introduce `AssetDirectory` or `UI/Assets/`.
- Shared fonts belong in `EngineContent/FontArchives/<font name>/`, with the original licence beside each collection.
- Shared SVG content belongs in `EngineContent/Icons/`. Resolve bundled content relative to the executable first;
  launching from another working directory must not hide fonts or pick an unrelated partial collection.
- All engine-authored C++ configuration and serialization formats use **TOML**, decoded/encoded with **toml++**.
  Preserve established schemas and extensions; inspect existing codecs before creating another format.
  `Appearance.toml` stores applied theme/typography; `FontCatalogue.toml` and per-font archive TOMLs describe fonts.
  JSON lock manifests and exhibit reports are automation interchange, not new C++ runtime serialization formats.
  External file formats such as OpenType, SVG, and shaders are not converted into TOML.
- **Vulkan is the primary rendering API. Slang (`.slang`) is the primary shader language.**
  DX11 is permitted for the isolated ImGui display exhibit; it is not a replacement engine-renderer architecture.
  Keep the scene renderer excluded from the current selective UI port.
- **GLFW owns platform windows and input.** Use a no-OpenGL-context window for Vulkan or the DX11 ImGui exhibit.
- Third-party sources belong under the existing ignored `ExternalPackages/`, restored from pinned dependencies.
  Reuse the existing toml++, GLFW, ImGui, and ThorVG dependency setup; do not vendor duplicate copies elsewhere.
- The Control Centre is a top-edge drawer, not a separate floating settings window. Retain the reference's
  trapezoid, travelling sheet, bottom grip, axis-locked drag, and upward swipe/flick dismissal.
  Typography sliders and scroll content retain their pointer ownership; drag the header, quick-card blank space,
  notch, or bottom grip to carry the sheet. GI and renderer settings remain excluded.

## Work and verification

Complete the requested work, run relevant local verification, then deliver results with evidence.
Local builds, tests, focused validation programs, and actual application captures need no separate approval.
Never deliver an implementation as complete without testing it.

| Work                  | Required verification                                             |
|-----------------------|-------------------------------------------------------------------|
| C++ or shaders        | Relevant build and tests; runtime validation where applicable.     |
| UI                    | Relevant tests and actual rendered or interaction evidence.        |
| Behaviour repairs     | Reproduce the defect and verify the repaired behaviour.            |
| Performance claims    | Reproducible measurements, conditions, and comparison results.      |
| Documentation         | Paths, links, structure, consistency, and applicable rule checks.   |
| Plans and analysis    | Verify factual claims against inspected sources; show a report.    |

- Exercise the changed behaviour, not merely unrelated passing tests.
- Add focused validation when existing coverage is insufficient.
- Compilation alone does not establish runtime correctness.
- Progress messages must not imply unfinished work passed verification.
- When blocked, report the command, observed result, missing prerequisites, and remaining checks.
- Mark unavailable checks blocked or not run, never passing. A blocked report is not completed implementation.

## Visible evidence — required with delivery

Every delivery must include an actual image, graph, results table, or concise verification report.
UI appearance claims require actual rendered evidence when capture is supported.
Mockups, generated illustrations, and invented measurements are not validation evidence.
A browser prototype does not prove that the production C++ editor renders the same result.

Retain evidence and its generating code together under `Exhibits/`.
Include reproduction commands, prerequisites, platform, compiler, configuration, tested revision,
and any relevant uncommitted changes. Explain limitations and present the evidence, not just its location.

## Folder ownership and documentation

Follow `Documentation/FolderStructure.md`. Do not invent alternative top-level folders.

| Location                         | Responsibility                                        |
|----------------------------------|-------------------------------------------------------|
| `Engine/`                        | Reusable production code shared across applications.  |
| `UI/Editor/`                     | Production editor UI.                                 |
| `Exhibits/`                      | Validation code, retained results, and proof images.   |
| `Exhibits/EditorMirror/`          | Comparison mirror of the C++ editor UI.                |
| `Experimental/`                  | Prototypes, experimental HTML, and related assets.     |
| `Documentation/Tracker.md`       | Single project progress and violation tracker.        |
| `Documentation/Rules/`           | Project rules; four retained skills remain above.     |
| `Documentation/Plans/Ongoing/`   | Approved active plans.                                |
| `Documentation/Plans/Completed/` | Completed plans, outcomes, and evidence links.         |
| `_ClaudeScratch/`                | Disposable local work; ignored and never committed.    |

Do not put prototypes, captures, reports, or validation-only code in `Engine/`.
Exhibits must reuse production engine code, not independent copies of it.
Keep requested prototype source, assets, and launchers together in `Experimental/`.
Do not delete inherited build, dependency, content, or application folders without an approved migration.
Update the folder map and affected references whenever approved moves change paths.
Maintain one tracker, not per-agent progress reports or competing backlogs.
Keep detailed results in exhibits and link them from the tracker.
Do not silently weaken rules to make a scan pass.

## Scratchpad

Use `_ClaudeScratch/logs/`, `_ClaudeScratch/build/`, and `_ClaudeScratch/tmp/` for disposable material.
Never commit scratch contents or leave the only copy of a deliverable or retained evidence there.
Permanent prototypes go in `Experimental/`; retained validation code and results go in `Exhibits/`.
Respect existing ignored build-output conventions when tools require their own directories.
Do not leave generated output next to source or loose at the root.
Incomplete HTML fragments use a non-`.html` extension such as `.part`.

## Platforms, binaries, and tooling

The user is on Windows. Deliver Windows binaries by default when a binary is requested or part of the task.
Do not build, package, or deliver Linux binaries unless requested.
Never substitute a Linux executable for a Windows deliverable.
If Windows compilation is unavailable, report the blocker; do not claim a Windows binary was verified.

Preserve Windows, Linux, and macOS portability. Android is a future consideration; no Android app is in scope.
Do not download macOS binaries or set up macOS during current work.
MSVC is the user's current compiler, not the only permitted compiler.
GCC/g++, MinGW-w64, or Clang may be used where the repository supports them.
Distinguish locally tested configurations from untested portability expectations.

Use actual repository build instructions and available tooling; do not hard-code developer installation paths.
Isolate platform-specific code from reusable code. Avoid unnecessary Windows-only assumptions.
Use the appropriate host interpreter: Windows command tooling for `.bat`, PowerShell for scripts requiring it,
and Bash for appropriate Linux/macOS commands. Python need not live at a fixed path.

## GitHub Actions — explicit approval required

Always ask before causing a GitHub Actions run unless the user explicitly authorised that run.
This includes dispatches, reruns, and remote operations such as pushes or pull requests that trigger workflows.
Inspect triggers before an authorised remote operation. Never use Actions as an automatic local-test fallback.
Permission to edit or commit locally is not permission to run Actions.
Follow the user's current commit and push instructions; do not infer permission to commit or push.

## Delivery

Follow the compact output skill: short outcome, verification table, visible evidence, exhibit paths,
blockers, untested configurations, and remaining work. Do not substitute confidence or compilation for testing.

### Recovered native Float32 surface maps (2026-10-19)

`VectorSurfaceCompute` writes typed RGBA32F channel maps and separate per-channel contribution masks; coverage/selection
stay RGBA8. `VectorCoverageRasterization` derives the actual storage format from SPIR-V and refuses unsupported Float32
capabilities instead of silently quantizing. The shared PFM writer preserves float bits and emits little-endian,
bottom-first scalar/RGB output. Seven GPU channel assertions pass strict and sanitizer builds; the archive contains
14 actual 32x32 GPU fixture maps/masks with separately authored gold thread and fabric metalness. See
`Exhibits/TextureStudio/NativeSurfaceEvidence.html` and `NativeMaterialApplication.md`.

The retired-slot cache path dispatches and samples the 512×512 Float32 surface output. Texture Workspace now toggles
to the native Pattern panel, persists vector TOML, and submits GPU coverage/selection. Its C++ runner passes 606 checks;
the recorded evidence explicitly does not represent a Windows desktop session. Pattern Float32 outputs remain
unconnected to the PBR material renderer; native 3D application, physical height calibration, user-document export and
Pattern 1:1 parity remain open. GIF previews are 8-bit evidence, not precision checks. Scope remains approximately 68%.

### Project Everest — Branch Topology, Independent Project Ownership, and InspectorRuntime

Project Everest lives independently in `Projects/Everest/` (never inside `Engine/`). `Everest` is purely a generic
project codename — never introduce mountain, alpine, terrain, or weather-station gimmicks. Zero `Everest` identifiers
or project-specific types belong in `Engine/` or `UI/Editor/`.

- **Banned words (2026-10-31):** `Model`, `Cache` (`Cache`, `Caches`, `Cached`, `Caching`), `Widget`, `Kit`, `Footprint`,
  `Shared`, `DedicatedStudio`, `SpecialtyEditor`, `Specialty`, `Actor`, and `Beacon` are barred across new code,
  file names, folders, and comments, and `Buffer` (`Gbuffer`, `*Buffer`) is avoided unless strictly mandated by raw
  Vulkan/Slang keywords (`VkBuffer`, `StructuredBuffer`) — recorded in `EngineDocs/AgenticInstuctions/SKILL-Naming.md`.
- **Mathematical symbols & `SultanAladin/Frontier-` folder taxonomy:** Use real Unicode Greek/math scalar identifiers
  (`μ`, `θ`, `σ*`, `τ`, `Δτ`, `Δs`, `𝑔`, `ρ`, `λ`, `Φ` per `SKILL-formatting.md` §4) across C++20 and `.slang` shaders
  with full geometric PascalCase vector names, and organize engine rendering domains following `SultanAladin/Frontier-`
  (`DeviceExchange/`, `GeometricRaster/`, `PlanetaryAtmosphere/`, `Shaders/`).
- **Container file extensions (`*space` family):** Use `.projectspace`, `.levelspace`, `.worldspace`, and `.sdfspace`
  for project, level, world, and headless-baked Float16 signed distance field containers — never compound `.toml`
  suffixes such as `.level.toml` even when serialized as TOML.
- **Geometry & Modular Preview Shadows Taxonomy:**
  - `Frontier/Engine/Geometry/PrimitiveGeometryGenerator.{h,cpp}` and
    `Frontier/Engine/Geometry/GeometryProcessing/{FacetTierBuild,QuadricTierBuild,ScreenSpaceClusterRefinement,VolumeDistanceArchive,BoundingVolumeAtlas}.{h,cpp}`.
  - `Frontier/Engine/GeometricRaster/{VisibilityProjection,PreviewRaster,ThemeCardPalette}` where `PreviewRaster`
    (with selectable `PreviewShadowMode`: `None`, `ContactEllipsoid`, `OrthoClusterDepth`, `SignedDistanceCone`,
    `HybridOrthoAndDistance`) is strictly guarded under `#if FRONTIER_EDITOR` (zero symbols in Shipping).
  - ReSTIR was removed on 2026-11-01 at the owner's direction: the Stage 3B `SpatiotemporalIllumination` and
    `SurfaceScatteringAtlas` renderer, its shaders, and its exhibit are gone. Its replacement is the **1:1 port of the
    VehicleHMI demo's ReSTIR GI** (`Frontier/Engine/Rendering/GlobalIllumination`,
    `Frontier/Engine/Shaders/GlobalIllumination`, parity in `Exhibits/RestirGiParity`) — never "optimize" or redesign it.
    It runs live in the Everest **Scene Viewport** (`Frontier/Engine/Rendering/SceneViewport/{SceneViewport,EnvironmentOverlay}`,
    `Projects/Everest/Source/EverestSceneViewport`, `Exhibits/SceneViewport`) on the editor's Vulkan 1.3 device
    (`VulkanDeviceAccess`), with the `PlanetaryAtmosphere` environment (clouds, fog, flare, sun, moons, stars, planets)
    overlaid on it — the owner chose PlanetaryAtmosphere over the demo's own clouds/flare. Its SPIR-V is also stored in
    `EngineContent/ShaderArchives/{GlobalIllumination,Environment}` (`Automation/StoreShaderArchives.py`; CTest
    `StoredShaderArchiveValidation`), so editor builds without the pinned slangc still run it. The editor's own raster set
    `EngineContent/ShaderArchives/Surface` (Project Browser start-up, CAD, materials, vector, bake) is stored the same way;
    refresh it after any `Engine/Render/Shader/*.slang` edit, or `Frontier.exe` built without slangc stops at start-up.
    `Automation/Workflows/EditorPreviewWindows.yml` builds, launch-checks and publishes Windows pre-releases once the owner
    copies it to `.github/workflows/` (GitHub refuses workflow files pushed by the Arena GitHub App, which has no
    `workflows` permission; the sandbox cannot reach `uploads.github.com`). Commits and pushes work as usual. Until the
    workflow runs, the agent commits each Windows package to its branch as
    `Binaries/FrontierSceneViewport-Windows-x64.zip` with its `.zip.sha256` (earlier packages went to the owner's Drive).
    `.sdfspace` SDF volumes, the software SAH BVH, and the `PreviewRaster` shadow modes remain.
- **Project creation auto-scaffolding (`ProjectGenerator`):** Creating a new project via `ProjectBrowserPanel`
  auto-generates C++ source scaffolding (`Source/<Identifier>Application.{h,cpp}` with main loop and `SceneWorld`
  integration), a buildable `CMakeLists.txt`, and a default scene (`Content/Levels/Default.levelspace`).
- **Main branch & post-commit sync rule:** Base all work on
  `https://github.com/SultanAladin/Frontier/tree/arena/11f31835-frontier` (reusing its `CLAUDE.md`, `PanelDockspace`,
  `ControlCentrePanel`, `ProjectBrowserPanel`, `LogStreamPanel`, `SceneDirectoryPanel`, and `FrontierInterface`), and
  sync with `SultanAladin/Frontier` `arena/11f31835-frontier` immediately after every commit.
- **Permanent Reference Repositories, Branches & Commits (selective synthesis — never blind merge; user may refer to these simply as "other repo references"):**
  1. **Reference Branch 1 — `streamlinkinbox/Frontier` (`arena/08e7de48-frontier`)**
     - URL: `https://github.com/streamlinkinbox/Frontier/tree/arena/08e7de48-frontier` (inspected tip `7bc60250e153a9b7d70c9740744723fde19fae22`)
     - Key contents: `FacetCdfProfile` + `FacetCdfSubmission` emissive triangle CDF sampling (`25.65M -> 466` evaluations), `FacetAreaReservoir` solid-angle area-light RIS, `DielectricRefraction.slang` + `PaneThickness.h` closed-glass optics, `Exhibits/LightCompute/`, and `Frontier/Engine/Physics/ProjectDrive/VehicleGeometry.h` socket coordinates.
  2. **Reference Branch 2 — `streamlinkinbox/Frontier` (`arena/5ec53579-frontier`)**
     - URL: `https://github.com/streamlinkinbox/Frontier/tree/arena/5ec53579-frontier` (inspected tip `56059b393f2d1a6f4ecf73c0f7661aa74f2de442`)
     - Key contents: Decomposed Vulkan Slang `PathReplay*` passes (`PathReplayVulkan`, `PathReplayReservoir.slang`, `PathReplaySampleHistory.slang`, `PathReplayReconstruction.slang`, `PathReplayVarianceFilter.slang`, `PathReplayRayQuery`), 16-bin SAH-2 ordered software BVH (`-96.88%` ray-triangle tests), multi-stratum `MaterialScattering` PBR lobe mixture sampling, `Docs/VehicleHmiEnginePlan.md`, and `Frontier/Projects/Project-VehicleHmiDemo/Contract/` (`DebugViews.md`, `ShaderContract.tsv`, 80-shader inventory).
  3. **Reference Branch 3 — `egoistm1inbox/Frontier` (`arena/9ce908bc-frontier`)**
     - URL: `https://github.com/egoistm1inbox/Frontier/tree/arena/9ce908bc-frontier` (inspected tip `4be51f2e4958701802c94e87344c95472e58e4bf`)
     - Key contents: Glass-aware Nanite-style `GeometryProcessing/` (`FacetTierBuild`, Garland-Heckbert locked-border `QuadricTierBuild`, `ClusterHierarchy` with IOR optical error boost + two-phase Hi-Z + two-layer glass peel + `64 KB` LRU page budget, GPU `ClusterCull.slang`), `ControlVehicle{Body,Rim,Tyre}.facet` binary `FCT1` meshes (`Frontier/Projects/Project-Drive/Content/Geometry/`), `VehicleHmiScene`, `BlinnRenderer` (CPU reference only — upgrade to GPU Vulkan), Jolt physics mechanics, and fluid/fracture/particle solvers (do NOT copy its CPU `SceneRenderer.cpp` or `O(N)` `SdfRestirPortalMirror.slang` loop).
  4. **Architecture & Naming Reference — `SultanAladin/Frontier-`**
     - URL: `https://github.com/SultanAladin/Frontier-` (inspected tip `28ec0657a23ed6bbf680f17cc06b464a53639dae`)
     - Key contents: Idiomatic domain folder taxonomy (`DeviceExchange/`, `GeometricRaster/`, `DisplayPresentation/`, `ContentInterchange/`, `PhysicalDynamics/`, `SpatialInterface/`, `Shaders/`), GPU `VisibilityRaster` (`R32_UINT` cluster/primitive ID + `RG16F` motion + reversed-Z), 8-wide compressed BVH (`TraversalCWBVH.slang` + two-level `TLAS`/`BLAS` `TraversalIndex`), OpenPBR channel-gated `MaterialEvaluation.slang`, `Docs/ClusterLodLab.md`, `Docs/ReSTIRPipelineInvestigation.md` (GTX 1650 SUPER cold-compile findings), and `Docs/V8DenoiserReview.md`.
  5. **Experimental Cards & Visual Controls Reference — `SultanAladin/Frontier` commit `6a83f8e`**
     - URL: `https://github.com/SultanAladin/Frontier/commit/6a83f8e` (`6a83f8e2beac6605449496e4edbeca36d132a2bc`)
     - Key contents: `Experimental/AtmosphereEditor/` (`GeometryDetail.js`, `VisualControls.js`, `AtmosphereEditor.css`, `index.html`) and `Experimental/EnvironmentInspectors/` — interactive Geometry Detail & Atmosphere cards (distance-response curve instrument, visual sliders, shaded/topology toggle). When adapting these cards, replace all hardcoded mint/emerald green colors with `ThemeStructure` (`ThemePalette` + `QueryAccentColor()`).
- **Editor compile-time boundary (`#ifdef FRONTIER_EDITOR`):** Guard all Editor mode / Editor UI (`SceneDirectory` UI,
  `InspectorRuntime`, `*Inspector` classes, `DiagramControls`, `FractureEditor`, `FluidPlumeEditor`, `ParticleEditor`,
  `PolygonClusterEditor`) behind `#ifdef FRONTIER_EDITOR`. Runtime/Shipping builds compile only pure scene records and
  their `*Configuration` structures.
- **Composable Property Configurations, Generic `DiagramControls`, and `InspectorRuntime`:**
  - Never use Unreal's monolithic `Actor`: scene entries are `RecordEntry` instances that attach only the modular
    property configurations they require — `SpatialConfiguration`, `PhysicsConfiguration` (with nested
    `MassConfiguration` and mass/density/kinetic helpers), `ThermalConfiguration` (Kelvin/Celsius, specific heat,
    conductivity, Stefan-Boltzmann flux), `ChemicalConfiguration` (molar mass, moles, Arrhenius kinetics, phase), and
    `SurfaceOpticsConfiguration` — so the general-purpose engine supports rendering, physics, and chemical simulations.
  - Plotting/diagram controls live in generic `DiagramControls` (`ConstructArcPlot`, `ConstructStrataPlot`,
    `ConstructPolarDial`, `ConstructCurvePlot`, `ConstructSpectrumBar`, `ConstructPolygonDiagram`, `ConstructDiscRow`,
    `ConstructTimeSeriesPlot`) — never domain-locked functions such as `ConstructLunarPhaseStrip`.
  - Scene records load from `.levelspace` files and can be dynamically added or deleted in the Editor at runtime.
  - `InspectorRuntime` supports attaching one or multiple modular `RecordInspector` instances (`SpatialInspector`,
    `PhysicsInspector`, `ThermalInspector`, `ChemicalInspector`, `SurfaceOpticsInspector`, `SolarInspector`,
    `AtmosphereInspector`, `CloudInspector`, `StarInspector`, `MoonInspector`, `FogInspector`, `LightningInspector`,
    `WindInspector`, `RenderPipelineInspector`, `DistanceFieldInspector`, `SurfaceCardInspector`,
    `PolygonClusterInspector`, `DiagnosticsInspector`, `FluidPlumeInspector`, `FractureInspector`,
    `ParticleInspector`) to any `RecordToken`.
  - Standalone authoring editors are individual `<Domain>Editor` classes (`FractureEditor`, `FluidPlumeEditor`,
    `ParticleEditor`, `PolygonClusterEditor`) invoked directly via `RecordInspector` virtual hooks.
  - Project-specific records and user-authored `*Inspector` classes live in `Projects/<Project>/` and register into
    `SceneWorld` and `InspectorRuntime` without modifying `Engine/`.
  - **Sequencing rule:** Leave `VehicleInspector` and vehicle systems deferred until Environment and Render Engines
    (RTX `VK_KHR_ray_query` and GTX 4-cascade SDF + Surface Cards + SAH-2 BVH) are complete.

