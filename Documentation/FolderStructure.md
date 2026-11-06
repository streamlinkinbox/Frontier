# Folder Structure

## Target ownership

```text
Frontier/
├── CMakeLists.txt             # UI-only build and local verification
├── Automation/                # Existing tooling plus pinned UI setup and patches
├── ExternalPackages/          # Ignored downloaded dependency trees
├── README.md
├── AGENTS.md
├── CLAUDE.md
├── Documentation/
│   ├── Tracker.md
│   ├── FolderStructure.md
│   ├── Rules/
│   └── Plans/
│       ├── Ongoing/
│       ├── Completed/
│       └── References/        # Retained reference documents (InheritedEngine/)
├── Engine/                    # Production code
│   ├── Interface/             # Selectively ported UI foundations
│   ├── Lifecycle/             # Activation sequence
│   └── Render/                # Blank; Git retention marker only
├── Frontier/
│   ├── Extensions/            # Bridges between engine systems and the applications/runtime (see CLAUDE.md)
│   │   ├── Interface/         # ViewportTexture, GizmoSpace, GizmoRasterization
│   │   ├── Rendering/         # RenderingExtension, SceneGeometryOccupancy
│   │   ├── Particle/          # ParticleExtension
│   │   ├── Plume/             # PlumeExtension
│   │   ├── Physics/           # PhysicsExtension, JoltExtension, PhysicsSpace
│   │   └── Networking/        # NetworkingExtension, NetworkingSpace (EOSExtension / EpicAdapter later)
│   └── Engine/                # Multi-realm PlanetaryAtmosphere, GeometricRaster, DeviceExchange, Rendering & Shaders
│       ├── DeviceExchange/    # VulkanComputeExchange (compute), DeviceExchange (raster + compute, WebGPU ordering), VulkanDeviceAccess
│       ├── GeometricRaster/   # VisibilityProjection (cross-portal ray routing & reversed-Z visibility)
│       ├── PlanetaryAtmosphere/ # AtmosphereScattering, AstronomicalEphemeris, SkyProjection, PlanetaryProjection,
│       │                        # CloudSpace, VolumetricClouds, FogSpace, OpticalLensFlare, PlanetaryEnsemble, WeatherEffects
│       ├── Rendering/         # SubmissionOrder, BarrierDerivation, TransientTargetAtlas, environment/inspection units
│       │   ├── AnalyticIllumination/ # Editor/Development closed-form renderer (sun shadow map, Blinn-Phong, sky)
│       │   ├── GlobalIllumination/ # 1:1 port of the VehicleHMI ReSTIR GI renderer (distance field, cards, BVH, ReSTIR, SVGF)
│       │   ├── SceneViewport/      # SceneViewport + EnvironmentOverlay: the GI refresh with PlanetaryAtmosphere over it
│       │   │                       # and GPU picking (GlobalIllumination/PickProjection: the occupant under a click)
│       │   └── VirtualizedGeometry/ # 1:1 port of the VehicleHMI cluster nesting (build, cut, GPU cull, pages) + I1-I5
│       └── Shaders/           # Slang 2026 SPIR-V 1.3 compute & raster shaders, one folder per renderer above
├── UI/
│   └── Editor/                # Lifecycle-owned production editor UI
├── Projects/
│   └── <ProjectName>/         # Independent game/application projects (e.g. Projects/Everest/)
├── EngineContent/             # Shared content archives (icons, fonts, materials, environments)
├── Exhibits/
│   ├── <ValidationName>/
│   │   ├── README.md
│   │   ├── Code/
│   │   ├── Images/
│   │   └── Results/
│   └── EditorMirror/           # Comparison mirror of the C++ editor
├── Experimental/              # Prototypes and experimental HTML
├── EngineDocs/AgenticInstuctions/  # Four retained authoritative skills
└── _ClaudeScratch/            # Ignored logs/, build/, tmp/
```

This is a target map, not a claim that source migration is complete.
Create meaningful folders as their contents are needed; do not add empty placeholder trees.
Use technical validation names under Exhibits, not the banned naming placeholder from earlier discussion.
No new top-level ownership categories without approval. Update this map after approved structural changes.

## Inherited layout — migration review outcome (2026-10-22)

`Internal/`, `Executables/`, `Applications/`, and `memory/` were reclaimed from this branch; the
complete trees remain on `master` and in this branch's history. Their documents, together with
`Documentation/Prototypes/`, `Documentation/Research/`, and the inherited root-level plan and
reference notes, are retained under `Documentation/Plans/References/InheritedEngine/`.
`Automation/` is current tooling (see the target map). Existing diagnostics remain in
`Exhibits/Diagnostics/`. `EngineDocs/` retains existing governance references until separately
reconciled.
The linked source also has `Docs/`, `Editor/`, `Projects/`, `Tools/`, and dependency/content folders;
map their ownership and build dependencies before any import or relocation.

## Selective port boundary

`Exhibits/UIFoundation/` retains verification code, results, and actual SVG raster evidence.
`Automation/Patches/` owns the six inherited ImGui patches. Dependency pins live in
`Automation/UIDependencies.json`. No scene renderer is ported or built by the new CMake target.
No inherited `Internal/` code is linked into this target.

`Exhibits/ControlCentre/` owns current appearance interaction tests, native CPU images, and build evidence.
`Automation/FontSources.json`, `PrepareFonts.py`, and `FontRequirements.txt` define open-font recovery.
`Automation/WindowsToolchain.cmake` supports Windows cross-compilation on the editing host.
`Automation/PackageWindows.py` packages only Windows binaries and their runtime assets under ignored `Binaries/`.

## Shared content and dependencies

```text
EngineContent/
├── Icons/                     # Tracked original/compatibility SVGs
├── MaterialArchives/          # Tracked Texture Studio material library (.material + baked maps)
├── PatternArchives/           # Tracked Texture Studio pattern presets
├── EnvironmentArchives/       # Studio environments + CelestialMaps (6 real 2K moon maps + Hipparcos8.csv) + ProvingGround
├── ShaderArchives/            # Stored Scene Viewport SPIR-V: GlobalIllumination/ + Environment/ (StoreShaderArchives.py)
├── Defaults/                  # Tracked blank project space
└── FontArchives/<font name>/  # Ignored font downloads, OFL notices, TOML descriptors
ExternalPackages/
├── glfw/                      # GLFW 3.4 platform windows/input
├── tomlpp/                    # Native TOML formats
├── imgui/                     # Pinned UI + approved patches
└── thorvg/                    # SVG rasterization
```

ContentDirectory resolves bundled content from the executable independently of the launch working directory.
The font catalogue has a native TOML form; JSON remains only an automation/report interchange.


## Local media and atmospheric field integration (2026-11-02)

`Exhibits/LocalMedia/` retains native Vulkan wind/noise/shape/quality checks and real Everest baking/vector captures.
`Frontier/Engine/PhysicalDynamics/AtmosphericFlow` maps scene Wind coefficients onto the existing force-field solver.
`Frontier/Engine/PlanetaryAtmosphere/VolumeBounds.h` and its Slang counterpart own finite-volume envelopes.
`Frontier/Engine/Scene/EnvironmentDiagnostics.h` carries owned LUT/field readouts without exposing device resources to UI.
The user's functional-baking/local-media request expands the previous selective UI-only boundary for these integrations.


## Celestial workspace and camera inspection (2026-11-02)

`Exhibits/CelestialWorkspace/` owns native celestial parameter/camera/chrome checks and rendered evidence.
`Exhibits/EverestInterface/Content/Simulation.levelspace` retains the unrendered simulation demonstrations for regressions;
they no longer populate the default editor scene. Planet rendering descriptors are derived in
`Frontier/Engine/PlanetaryAtmosphere/CelestialAuthoring.*`; live planet previews reuse the native CPU evaluator.
Panel-local footer support lives in the existing `PanelDockspace` registration. The removed distance-slice UI/shader
has no replacement implementation; its uniform slot is reserved for compatibility with the remaining encoding.

## Virtualized geometry and analytic illumination (2026-11-02)

`Frontier/Engine/Rendering/VirtualizedGeometry/` is the 1:1 port of the VehicleHMI demo's cluster nesting (library
`FrontierVirtualizedGeometry`; the GPU host `NestingProjection` compiles into `FrontierGlobalIllumination`). Its Slang
units live in `Frontier/Engine/Shaders/VirtualizedGeometry/` and its stored archives in
`EngineContent/ShaderArchives/VirtualizedGeometry/`. The import step stores each scene's nestings beside its facet space
(`EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.nestingspace`).
`Exhibits/VirtualizedGeometry/` retains the parity, test, refinement, archive and GPU capture evidence, plus the
earlier builder comparison. The report and plan live in `Documentation/Plans/Ongoing/VirtualizedGeometry.md`.

`Frontier/Engine/Rendering/AnalyticIllumination/` is the editor's fast closed-form renderer (library
`FrontierAnalyticIllumination`, Slang units in `Frontier/Engine/Shaders/AnalyticIllumination/`, archive set
`AnalyticIllumination`). `Exhibits/AnalyticIllumination/` retains its offscreen capture against the global illumination.

## Ray-traced lighting evidence (2026-11-02)

`Exhibits/RayTracedLighting/` measures the RTX path's reflections and sun shadows under the demo's behaviour and the
measured refinements (`RefreshToggles::Refinements`), against converged references. Its new shader unit is
`Frontier/Engine/Shaders/GlobalIllumination/PenumbraResolve.slang`. The report lives in
`Documentation/Plans/Ongoing/RayTracedLighting.md`.

## Force Field and Particle editors (2026-11-04)

- `Frontier/Engine/PhysicalDynamics/ForceSourceCatalogue.*`: the 16 force types of the Force Field editor.
- `Frontier/Engine/Interface/ForceFieldVisualization/`: the field slice both editors draw (Map, Oblique, Camera).
- `Frontier/Engine/Interface/Inspectors/ForceField/`: the Force Field, Wind and force pages.
- `Projects/Everest/Source/EverestFieldViewport.cpp`: the Field view in the Everest viewport.
- `Frontier/Engine/PhysicalDynamics/ParticleDynamics/`: the ParticleSpace, its `.particlespace` codec, the preset
  library decoder, force taxonomy, uniform packing and lightning (library `FrontierEnvironmentRendering`).
- `Frontier/Engine/Rendering/ParticleSimulation/` and `Frontier/Engine/Shaders/ParticleSimulation/`: the GPU particle
  engine (library `FrontierParticleSimulation`, archive set `ParticleSimulation`).
- `Frontier/Engine/Interface/ParticleEditor/` and `ParticleEditor.*`: the editor window, its pages and field overlay.
- `Frontier/Extensions/Particle/ParticleExtension.*`: the engine on the editor's device.
- `EngineContent/ParticleArchives/ParticleLibrary.toml`: the 52 presets, exported by
  `Automation/ExportParticleLibrary.mjs`.
- `Exhibits/ForceFieldEditor/` and `Exhibits/ParticleEditor/`: parity tests, captures and their READMEs. The plan is
  `Documentation/Plans/Ongoing/ForceFieldParticleIntegration.md`.

## SolidArc CAD ownership

- `Engine/GeometryProcessing/`: reusable CAD B-rep, curves, surfacing, feature tree and exchange.
- `Engine/Render/OrbitCamera.*` and `Engine/Render/Shader/{CadStroke,MatCapLookup}.slang`: CAD camera/math.
- `UI/SolidArc/`: CAD document, interface, browser connection, viewport and stored shaders.
- `Experimental/SolidArc/Native/`: CAD executable, CMake integration, package configuration and shader compilation.
- `Exhibits/SolidArc/Code/` and `Exhibits/GeometryProcessing/Code/`: CAD verification and fixtures.
- `EngineContent/Icons/SolidArc/`: CAD SVG artwork in the existing content directory.

No parallel ImGui/interface/font framework is introduced. The existing scene renderer and project startup remain owned
by their current directories; CAD editing is a separate workspace launched by the project browser.

## Extensions and Spaces (2026-11-04)

- `Frontier/Extensions/<Name>/`: the bridges between engine systems and applications, one implementation for every
  workspace. The rule and the list of Spaces live in `CLAUDE.md` (Extensions and Spaces).
- `Frontier/Engine/Scene/WorldSpace.*`: the level's world (formerly `SceneWorld`).
- `Frontier/Engine/Interface/SceneViewportPresentation.h`: the Scene Viewport contract between a workspace and its host.
- `Frontier/Extensions/Interface/GizmoSpace.*` replaces `UI/SolidArc/Viewport/SolidArcGizmo.*`; SolidArc draws it with
  its GPU program, the Frontier editor with `GizmoRasterization`.
- `Frontier/Extensions/Rendering/` replaces `Projects/Everest/Source/EverestSceneViewport.*` and
  `EverestSceneGeometry.*`.
- The plan is `Documentation/Plans/Ongoing/ExtensionsAndSpaces.md`.

## X-ray scan, plume simulation and the Smoke & fire editor (2026-11-06)

- `Frontier/Engine/Rendering/XRayScan/` and `Frontier/Engine/Shaders/XRayScan/`: the X-ray scan (library `FrontierXRayScan`, archive set `XRayScan`): the
  clock, the feature edges, the particles, the profile records and the overlay on the device exchange.
- `Frontier/Engine/PhysicalDynamics/FluidDynamics/`: the gas catalogue with its presets and instruments (library `FrontierGasCatalogue`), and the tyre
  telemetry and source projections (library `FrontierPlumeSimulation`).
- `Frontier/Engine/Rendering/PlumeSimulation/` and `Frontier/Engine/Shaders/PlumeSimulation/`: the plume's schedule, its CPU half and its device host, the
  stand-ins it binds when no scene stands behind it (`PlumeStandIns`), and the five Slang files of its nine compute entries (archive set `PlumeSimulation`).
- `Frontier/Engine/Interface/FluidPlumeEditor.*`, `FluidPlumeEditor/` and `Inspectors/FluidPlumeInspector.*`: the Smoke & fire editor window (Outliner, viewport
  and Log Stream, Inspector, the Libraries sheet: `FluidPlumeEditorWorkspace`, `FluidPlumePages`, `FluidPlumeLibrary`, `PlumeViewportPresentation.h`) and the
  record's Inspector page, all behind `FRONTIER_EDITOR`.
- `Frontier/Extensions/Plume/PlumeExtension.*`: the editor application's presenter, which draws the plume in the editor's stage on the editor's device (as
  `Extensions/Particle` does for the Particle editor); the native application registers it in `Experimental/Frontier/Native/WindowsFrontier.cpp`.
- `Frontier/Engine/Scene/EngineConfigurations.h` and `WorldSpace.cpp`: the `FluidPlume` record, registered as "Solver · Fluid Plume".
- `EngineContent/ShaderArchives/XRayScan/` and `PlumeSimulation/`: the stored SPIR-V of the two sets. `Automation/XRayScanRendering.cmake` and
  `PlumeSimulationRendering.cmake` hold their targets and shader compilation.
- `Exhibits/XRayScan/`, `Exhibits/PlumeSimulation/`, `Exhibits/FluidPlumeEditor/` and `Exhibits/VehicleHmiPrototype/`: validation, captures and READMEs. The
  last holds the TLAS skip's proof and the Garage view's pictures.
- Planned homes, not yet written: `Frontier/Extensions/Rendering/` (the scan's vehicle binding), InterfaceSpace (the scan's HUD) and SimulationSpace
  (fluid, gas and plume).
- `Frontier/Engine/Effects/VehicleInspection.h` is superseded by `Rendering/XRayScan/` and stays until its users migrate.
