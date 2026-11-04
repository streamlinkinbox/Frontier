# Force Field and Particle editors — C++ integration plan

Status: **in progress** (2026-11-03, branch `arena/88b72faa-frontier`). The owner approved the HTML prototypes
(`Experimental/ForceFieldEditor`, `Experimental/ParticleEditor`, `Exhibits/ForceFieldEditor`) and asked for both
editors in the Frontier editor before the vehicle step. This plan records the phases and what each one delivered.

## 1. Decisions carried over from the prototypes

| Decision | Where it lands in C++ |
|---|---|
| The Force Field is the master component; Wind is its guest record | `ForceField` record with `Wind` and `ForceSource` records under it |
| Sixteen force types: 9 flows, 6 accelerations, 1 damping | `ForceSourceCategory` (`EngineConfigurations.h`), `ForceSourceCatalogue` |
| Flow m/s, accelerate m/s², damp 1/s never share a bucket | `ForceContribution`, `ForceFieldSpace::EvaluateContribution` |
| 100 m to 100 km, level of detail by cell span | `EvaluateStepSample(Point, Swirl, CellSpan)`, mesoscale in `PlanetaryWindProfile` |
| One visualization for both editors | `Interface/ForceFieldVisualization/` (phase B) |
| Render settings look: outliner + tab-switched inspector | `Interface/Controls/SectionRows`, `Inspectors/ForceField/` (phase C) |

## 2. Phases

| Phase | Content | Status |
|---|---|---|
| A | Engine port of `ForceFieldCatalogue.js`, renames, records, levelspace, parity test | 🚧 |
| B | Field visualization in the viewport (Scene · Field switch, map, oblique, minimap, timeline, camera overlay) | 🚧 |
| C | Force Field, Wind and Force source inspector pages, add-force library, Log Stream entries | 🚧 |
| D | Windows binary and Tracker | 🚧 |
| E | Particle editor: modal with outliner, inspector, library and its own Vulkan viewport (Slang port of the eos engine) | 🚧 |

## 3. Phase A — engine

- `Frontier/Engine/PhysicalDynamics/ForceSourceCatalogue.{h,cpp}`: the type table (parameters with their ranges and
  levelspace keys), activation (Always, Window, Trigger), moving centres (tornado track, puff drift, front travel), reach
  masks (Sphere, Column, Box with falloff), the 9 flow, 6 acceleration and 1 damping evaluations, the radial profile, the
  cascade budget and the Beaufort scale. Line for line with the prototype; the gradient noise hashes in uint32 as
  `Math.imul`.
- `ForceFieldSpace`: `ConfigureAuthoredSources(Sources, TimelineSeconds)` prepares the records once per refresh; their
  flows join `EvaluateStepSample`; `EvaluateContribution` reads any contribution, optionally one source alone. The
  eddies are the prototype's divergence-free 2D stream-function curl; gust bands, eddies and mesoscale fade below two
  cells. The authored sources run on the Force Field's timeline clock, the wind on the renderer's clock.
- Renames: `ForceSourceKind` → `ForceSourceCategory` (VortexColumn → TornadoColumn, TravelingGustFront → GustFront,
  RadialMicroburst → Microburst, LocalizedGustPuff → GustPuff, AerodynamicWake → VehicleWake, ThermalUpdraftPlume →
  ThermalUpdraft, DirectionalJetCorridor → JetCorridor, SphericalImpulseShell → BlastWave), `VortexColumnParameters` →
  `TornadoColumnParameters`, `ActiveForceSource::Kind/Vortex` → `Category/Column`, `Emitter*` → `Body*`.
- Records: `ForceSourceConfiguration` (`ForceSource`, group Forcefield, under `rec_force_field`). Levelspace keys:
  `category` (snake_case), `centre_metres` in scene axes [east, north, up], the type's own keys (`inner_radius_metres`,
  `inflow_mps`, `track_bearing_deg` …), `reach`, `reach_metres`, `falloff`, `activation`, `*_seconds`, `acts_on`.
  The firing time is runtime only.
- `Default.levelspace` carries the prototype's 14 demo sources. The GPU cloud synthesis keeps only triggered sources
  for now, so the renderer's sky is unchanged; the CPU field (vectors, camera wind, local volume drift) sees them all.
- Parity: `ForceFieldCatalogueValidation` against `Exhibits/ForceFieldEditor/Results/CatalogueReference.toml`
  (written by `ExportCatalogueReference.mjs` from the prototype).

## 4. Open points

- Authored flow sources in the GPU cloud records (funnels for authored tornadoes) — after the owner sees the field.
- The proposed cascades (4 × 128² × 32) are budgeted and drawn; the GPU lattice itself comes with the particles.
