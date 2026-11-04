# Project Everest — Master Execution Plan

## 1. Architectural Ownership & Boundaries

- **Engine Independence:** `Engine/` and `Frontier/Engine/` remain 100% project-agnostic. Zero `Everest` identifiers,
  project-specific types, or domain strings belong inside Engine headers or sources.
- **Generic Project Identity:** `Projects/Everest/` is an independent top-level project named `Everest` — purely an
  arbitrary project codename with zero mountain, terrain, alpine, or weather-station theming.
- **Container Extensions (`*space` family):** All project, level, and world containers use Frontier `*space` extensions
  (`.projectspace`, `.levelspace`, `.worldspace`) — never compound `.toml` suffixes such as `.level.toml`.
- **Editor Compile-Time Boundary (`#ifdef FRONTIER_EDITOR`):** All Editor UI, `SceneDirectory` metadata,
  `DiagramControls`, `InspectorRuntime`, modular `*Inspector` classes, and standalone `<Domain>Editor` modals compile
  only when `FRONTIER_EDITOR` is enabled. Shipping/Runtime builds (`FRONTIER_EDITOR == 0`) compile only runtime
  `RecordEntry` types, `RecordStore`, `SceneWorld`, and pure C++ `*Configuration` structures.
- **Banned Vocabulary Compliance:** Enforce `EngineDocs/AgenticInstuctions/SKILL-Naming.md` including `2026-10-31`
  additions (`Model`, `Cache`, `Widget`, `Kit`, `Footprint`, `Shared`, `DedicatedStudio`, `SpecialtyEditor`, `Specialty`,
  `Actor`, `Beacon`, and avoiding `Buffer` unless strictly required by Vulkan/Slang API).

## 2. Project Creation C++ Auto-Generation (`ProjectGenerator`)

When a user creates a new project via `ProjectBrowserPanel::CreateProject`:
1. `ProjectSpaceCodec::CreateBlank` creates `<ProjectRoot>/<Name>.projectspace` and `Content/`.
2. `ProjectGenerator::GenerateProjectScaffold` auto-generates:
   - `Content/Levels/Default.levelspace` — ready-to-load default scene (`Sun`, `Lens Flare`, `Atmosphere`,
     `Volumetric Clouds`, `Stars`, `Render Pipeline`, `Diagnostics`, and a sample spatial/optical body).
   - `Source/<Identifier>Application.h` and `Source/<Identifier>Application.cpp` — C++ application class with
     `Initialize()`, `StepSimulation(double DeltaSeconds)`, `RunMainLoop(uint32_t FrameLimit)`, `Shutdown()`, and
     standalone `main()` entry point.
   - `CMakeLists.txt` — ready-to-configure CMake build definition for the generated project.

## 3. Composable Property Configurations, Generic `DiagramControls`, and `InspectorRuntime`

- **Composable Property Configurations (No Monolithic `Actor`):**
  Scene entries are `RecordEntry` instances that attach only the property domains they require, enabling rendering,
  rigid-body physics, thermal transfer, and chemical reaction simulations on one general-purpose engine:
  - `SpatialConfiguration` (`PositionMetres`, `OrientationDegrees`, `Scale`, `DistanceTo`) -> `SpatialInspector`
  - `MassConfiguration` (`MassKilograms`, `DensityKgPerCubicMetre`, `VolumeCubicMetres`, kinetic/momentum helpers)
    inside `PhysicsConfiguration` (`LinearVelocityMps`, `AngularVelocityRadPerSec`, `Restitution`, `Friction`) ->
    `PhysicsInspector`
  - `ThermalConfiguration` (`TemperatureKelvin`, `SpecificHeatJPerKgK`, `ThermalConductivityWPerMK`,
    `SurfaceEmissivity`, Celsius/Kelvin + sensible-heat + Stefan-Boltzmann helpers) -> `ThermalInspector`
  - `ChemicalConfiguration` (`MolarMassGramsPerMole`, `AmountMoles`, `ConcentrationMolPerLitre`,
    `ActivationEnergyKjPerMol`, `PreExponentialFactor`, `EnthalpyDeltaKjPerMol`, Arrhenius rate helper) ->
    `ChemicalInspector`
  - `SurfaceOpticsConfiguration` (`AlbedoRgb`, `Roughness`, `Metallic`, `IndexOfRefraction`) ->
    `SurfaceOpticsInspector`
- **Multi-Inspector Attachment in `InspectorRuntime`:**
  `InspectorRuntime::AttachInspector(RecordToken, std::unique_ptr<RecordInspector>)` supports attaching one or multiple
  modular `RecordInspector` instances to a single `RecordToken`. User projects author custom `RecordInspector` classes
  directly in C++ rather than relying on `UPROPERTY`-style reflection macros.
- **Generic `DiagramControls`:**
  Reusable plotting/diagramming primitives (`ConstructArcPlot`, `ConstructStrataPlot`, `ConstructPolarDial`,
  `ConstructCurvePlot`, `ConstructSpectrumBar`, `ConstructPolygonDiagram`, `ConstructDiscRow`,
  `ConstructTimeSeriesPlot`) serve any inspector or simulation domain without domain-locked function names.
- **Standalone `<Domain>Editor` Classes:**
  `FractureEditor`, `FluidPlumeEditor`, `ParticleEditor`, and `PolygonClusterEditor` live in their own headers/sources
  and open directly via `RecordInspector` virtual editor hooks.

## 4. Phased Delivery Roadmap

1. **Phase 1 (Completed):** Project C++ auto-scaffolding (`ProjectGenerator`), `.levelspace` dynamic scene lifecycle,
   generic `DiagramControls`, composable Physics/Thermal/Chemical/Spatial/Optics + Environment + Render `*Inspector`s
   under `InspectorRuntime`, standalone `<Domain>Editor` modals, independent `Projects/Everest/`, Shipping
   (`FRONTIER_EDITOR=0`) + Editor (`FRONTIER_EDITOR=1`) CTests, and 1920x1080 CPU-rasterized PNG Exhibits.
2. **Phase 2 & Phase 2.1 (Completed):** Unified Multi-Instance & Portal-Routed Vulkan 1.3 PlanetaryAtmosphere &
   Celestial Rendering Stack across `Frontier/Engine/{DeviceExchange,GeometricRaster,PlanetaryAtmosphere,Shaders}/` —
   `AtmosphereProjection` + `AtmosphereScattering` (`2.80153% < 2.85%` vs FP64 oracle), `PlanetaryProjection` +
   `AstronomicalEphemeris` with all **6 real 2K moon maps** (`Luna`, `Ember`, `Glacier`, `Sulfur`, `Shroud`, `Shard`),
   41,359 Hipparcos stars, ringed planets, and multi-sun discs, non-tiled 3D `CloudSpace` + `VolumetricClouds` +
   `CloudShadowProjection` + analytic foreground cloud shadows, `OpticalLensFlare` GPU `1x1` visibility + chromatic
   aperture optics, and coupled `FogSpace` across arbitrary realms & `PortalAperture` transitions (UI Editor untouched).
3. **Phase 3A (Completed):** `AtmosphereProjection` rename + physical lunar atmospheric limb/corona haze, unified 3D
   `ForceFieldSpace` (`Frontier/Engine/PhysicalDynamics/ForceField.{h,cpp}` + `ForceField.slang`) with triggerable
   Burgers-Rott tornado vortex funnels, bow-echo gust fronts, radial microbursts, and multi-medium coupling (clouds,
   ground dust/sand/debris, precipitation & lofted grain ballistic integration), compile-time declarative
   `[engine.content_imports]` staging (`Automation/StageProjectContent.{py,cmake}`), glass-aware QEM `ClusterHierarchy`
   (`64 KB` pages, two-phase Hi-Z, 3-tier Material LOD virtualization, `ControlVehicle{Body,Rim,Tyre}.facet`),
   theme-bound `Experimental/AtmosphereEditor/` + `ThemeCardPalette.h`, and Editor/Development-only `DiagnosticRaster`
   strictly excluded from Shipping builds (`61/61` CTests passing).
4. **Phase 3B (Restarting):** ReSTIR removed on 2026-11-01 at owner direction (`SpatiotemporalIllumination`,
   `SurfaceScatteringAtlas`, their shaders, and exhibit). Lighting restarts from scratch with simple Blinn-style preview
   lighting and basic shadows; `.sdfspace` SDF, software SAH BVH, and `PreviewRaster` shadow modes are retained.
5. **Phase 4:** Physical, Thermal, Chemical, Weather, Fluid Plume, Fracture, Particle, and Mechanical Simulation Systems
   (including `VehicleInspector` once the restarted renderer is verified on GPU).
