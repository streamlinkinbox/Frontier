//============================================================================================================================================
// 📦 Frontier/CLAUDE.md — Architecture, Naming, Formatting and Compilation Directives for Frontier Engine
//============================================================================================================================================

# Frontier Engine Directives

## 1. Architectural Philosophy & Integrity
- **Decoupled Architecture**: Frontier is an independent, high-performance Vulkan simulation and rendering engine.
- **Strict Role-Based Nomenclature**: Every class, struct, module, and folder strictly follows the two-word `<Subject><Role>` format.
- **No Banned Words**: Never use OOP/AI tropes or vague abstractions.
- **Fail-Fast Error Handling**: Expected domain failures are returned as `Refusal` or wrapped in `Deliver<T>`. No raw C++ exceptions crossing module seams.
- **Linear Memory Management**: Monotonic extents via `ByteSpace` with reset at Phase ⑭. No uncontrolled heap allocations during simulation or rendering loops.

---

## 2. Definitive Closed Role Suffixes (19 Authorized Roles)
1. `Sequence`      : Deterministic multi-step ordered execution.
2. `Codec`         : Bidirectional bit-level encoding and decoding.
3. `Exchange`      : Low-level C-ABI boundary and hardware transport.
4. `Interchange`   : High-level platform interop and network datagrams (e.g. EOS).
5. `Extension`     : Optional hardware, platform, or engine feature set.
6. `Solver`        : Mathematical constraint satisfaction and integration (Jolt, XPBD, Fluid).
7. `Integrator`    : Numerical differential equation advancing (ReSTIR, Photometrics, Acoustics).
8. `Classifier`    : Decision tree and capability discrimination (Hardware, Orientation).
9. `Projection`    : Coordinate space transformation and visibility mapping.
10. `Specification`: Mathematical and structural declarations.
11. `Configuration`: Runtime tunable parameters and subsystem setups.
12. `Criteria`     : Validation thresholds and filtering conditions.
13. `Structure`    : Spatial and physical topology representations.
14. `Space`        : Continuous coordinate realms and scalar fields (LevelSet, Clusters).
15. `Index`        : Direct spatial lookup and fast addressing structures.
16. `Metrics`      : Quantitative profiling counters and microsecond telemetry.
17. `Scheduler`    : Fiber work-stealing execution graphs and clock loops.
18. `Queue`        : Lock-free atomic FIFO / double-ended work queues.
19. `Panel`        : Immediate mode UI rendering overlays (ImGui).
20. `Host`         : Root coordinator and executable lifecycle entry point.

---

## 3. Forbidden Words (Strictly Banned)
`Manager`, `Handler`, `Processor`, `Controller`, `Service`, `Utility`, `Helper`, `Node`, `Frame`, `Module`,
`Core`, `System`, `Backend`, `Pass`, `Stage`, `Harness`, `Shell`, `Entity`, `Element`, `Subsystem`,
`Hierarchy`, `Data`, `Info`, `Object`, `Item`, `Thing`, `Kind`, `Base`, `flag`, `state`, `value`,
`Parent`, `Child`, `Sibling`, `Table`, `Map`, `Block`, `Digest`, `Model`, `Handle`, `Store`,
`Bridge`, `Atlas`, `Substrate`, `Fabric`, `Cache`, `Evaluator`, `Evaluate`, `Journal`, `Resolver`,
`Mesh`, `Pool`, `Registry`, `Catalog`, `Repository`, `Directory`, `Vault`, `Arena`, `Inventory`,
`Ledger`, `Plan`, `Filter`, `Grid`, `Array`, `Dispatcher`, `Memory`, `Buffer`, `Pipeline`, `Flow`,
`Composite`, `Compose`, `Composition`, `Allocation`, `Tier`, `Nesting`, `Stratum`, `Mip`, `Messenger`,
`Probe`, `Blend`, `History`, `Bake`, `Stamp`, `Contract`, `Outcome`, `Prelude`, `Cadence`, `Binding`,
`Submission`, `Footprint`, `Region`, `Tree`, `Vacancy`, `Ordinates`, `Draft`, `Draught`, `Paint`,
`Depot`, `Ordinal`, `Actor`, `Source`, `API`.

---

### 3.1 SolidArc Vocabulary (Editor/EditorTools/ParametricSketcher/)
- The modelling tool is named **SolidArc** and lives in the folder **`Editor/EditorTools/ParametricSketcher/`** (one tool among the editor tools under `Editor/EditorTools/` — it is a tool, never a project).
- The three-letter acronym for computer-aided design is **strictly banned** anywhere in this repository's SolidArc code, comments, file names, targets, scripts, proofs and documentation. Say `SolidArc`, `modelling tool`, `sketcher`, `kernel`, `NURBS`, `B-rep` instead.
- Script files use the `.arc` extension; build targets are prefixed `SolidArc`.
- **No generic programmer prefixes**: `Get…`, `Set…`, `Is…`, `Was…`, `Has…` are banned as identifier prefixes. Read a quantity by its noun (`Radius()`), write it by a verb that says what happens (`Anchor`, `Arrange`, `Resize`, `Cap`, `MovePole`), test a condition by the adjective or participle (`Closed()`, `Selected`, `Given(x)`).
- **No web / framework jargon**: `API`, `Web`, `Family`, `Component`, `Widget`, `Session token`… stay out of names and comments.
- **Names come from the domain**: geometry, topology, drafting and rendering vocabulary only (`Figure`, `Pole`, `Coedge`, `Lattice`, `Grip`, `Pivot`, `Timeline`). No office / literary words (`Journal`, `Ledger`, `Item`, `Handle`, `Frame`, `Probe`, `History`, `Kind`, `Store`, `Source`).
- Compiled-in vocabulary: `SceneFigure` (not item/object), `FigureClassification` (not kind/type), `UndoSequence` + `timeline` (not history/ledger/journal), `GizmoGrip` (not handle), `PivotBasis` (not frame), `Lattice` (not grid), `Switch` (not flag), `Fit` (not frame), `inspect` (not probe).

---

## 4. Formatting Standards
- **File Headers**: Exactly 142 characters wide (`//` followed by 140 `=` characters).
- **Section Banners**: Exactly 122 characters wide (`//` followed by 120 `-` characters).
- **Indentation**: 4 spaces everywhere. Tabs are strictly forbidden.
- **Braces**: Allman style (opening brace on a new line at enclosing indentation level).
- **Namespaces**: Flat indentation inside `namespace Frontier { ... }` (declarations at column 0).
- **Unit Annotations**: Vertically aligned comments with bracketed physical units `[m]`, `[s]`, `[kg]`, `[lux]`, `[rad]`, `[Hz]`, `[-]`.
- **Unicode Math Glyphs**: Use real mathematical symbols ($α, β, γ, Δτ, ν, ρ, θ, ω, \nabla, \phi, \Sigma$).
- **Single Accessor Conversion**: Use C++20 templated conversion accessors (`template<typename T> T Query() const` or conversion operators) instead of duplicating multiple method variants.

---

## 5. Build & Compilation Commands
- **Linux**: `make clean && make run` or `cd BuildConfiguration && ./LinuxBuild.sh`
- **Windows (Direct Toolchain via PowerShell)**:
  `powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1 -Configuration Release`
  `powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1 -Configuration Debug -Rebuild`
- **Windows (CMake / MSVC)**:
  `cmake -B build && cmake --build build --config Release`
- **Development UI**: Compiled conditionally with `#ifdef FRONTIER_DEVELOPMENT`.

---

## 6. Engine vs. Game Project Decoupling Architecture
- **Engine Core Scope**:
  - Provides hardware-agnostic abstractions, mathematical types, and engine foundations (`DeviceExchange`, `PhysicalDynamics`, `VolumetricDynamics`, `GeometricRaster`, `PhotometricIllumination`, `PlatformInterchange`, `DisplayPresentation`).
  - Standard virtual key and mouse state enumeration (`VirtualKeyCategory`, `MouseButtonCategory` in `DeviceExchange/InputExchange.h`) provides a universal, standard hardware abstraction covering alphanumeric keys (A-Z, 0-9), function keys (F1-F12), navigation, modifiers, mouse buttons, and gamepad records.
  - Baseline camera projections (`GeometricRaster/CameraProjection.h`) declare standard viewport geometry, perspective matrices, and ray construction.
- **Game / Project Scope (`Projects/<ProjectName>/`)**:
  - Individual games (e.g. `Project-F20`, `Project-Zero`) define their own gameplay mechanics, physics tuning, and input action bindings without altering engine core files.
  - Game action mapping (e.g. mapping `VirtualKeyCategory::KeyW` to forward movement, or `MouseButtonCategory::ButtonRight` to flight steering) and specialized camera controllers (e.g. `FlyThroughSolver`, `OrbitSolver`, `ChaseCamSolver`) reside entirely within the respective project codebase.
  - Prevents engine bloat, maintains clean module boundaries, and guarantees that any game project can implement arbitrary control schemes using the engine's universal input hardware abstraction.

---

## 7. 3D Coordinate System & Physical Units Standards
- **Right-Handed Coordinate System Convention ($+Z$ Up)**:
  - **$+X$ Axis**: Right / East (Red Color)
  - **$+Y$ Axis**: Forward / North (Green Color)
  - **$+Z$ Axis**: Up / Zenith (Blue Color) — **$Z$ is strictly the upward vertical axis across the engine and all games**.
  - **Vector Cross Products**: $\vec{X} \times \vec{Y} = \vec{Z}$ (Right $\times$ Forward $=$ Up), $\vec{Y} \times \vec{Z} = \vec{X}$ (Forward $\times$ Up $=$ Right), $\vec{Z} \times \vec{X} = \vec{Y}$ (Up $\times$ Right $=$ Forward).
- **Default Physical Units**:
  - **Distance / Coordinates**: **Meters ($[m]$)** strictly (never centimeters, millimeters, or feet).
  - **Time**: **Seconds ($[s]$)**.
  - **Mass**: **Kilograms ($[kg]$)**.
  - **Velocity**: **Meters per second ($[m/s]$)**.
  - **Luminous Flux / Illuminance**: **Lux ($[lux]$)** / Lumens ($[lm]$).
  - **Angles**: **Radians ($[rad]$)** for mathematical calculation, degrees ($[deg]$) only for human-facing UI input.
- **Vulkan Projection Mapping**:
  - Vulkan NDC has clip $Z \in [0, 1]$ and inverted $Y$. Projections in `CameraProjection` and shaders map from Right-Handed $Z$-up World Space into Vulkan clip coordinates with appropriate depth clamping and $Y$-inversion.

---

## 8. Standalone & Embedded Content Creation Tools Architecture (`Tools/`)
- Tools are structured as dual-target modular applications:
  1. **Standalone Executables**: Can be compiled and run as independent desktop creation tools (e.g. `Editor/EditorTools/TextureEditor/`, `Editor/EditorTools/ParametricSketcher/`).
  2. **Embedded Development Workspaces**: Can be invoked inside the engine editor while testing or simulating a game via `#ifdef FRONTIER_DEVELOPMENT` within `DisplayPresentation/WorkspacePanel`.
- Common Tool Suite:
  - **Texture Painting**: Interactive 3D surface texel painting, brush projection, PBR layer blending.
  - **Texture Baking**: High-to-low poly ray-traced baking (Normal, AO, Curvature, Bent Normals, Thickness).
  - **Parametric Sketching**: 2D/3D CAD constraint solving, spline/NURBS surfaces, profile extrusions, boolean solids.
  - **UV Unwrapping**: Conformal flattening (ABF++/LSCM), seam tagging, island packing, distortion metrics.
  - **Procedural Plants / Foliage**: L-system parametric branching, leaf scattering, wind binding.
  - **Material Shader Graph**: Node-based PBR graph authoring with live SPIR-V compute compilation.

---

## 9. Scratchpad Directory Directive for Temporary Work
- **Strict Anti-Pollution Rule**: Any temporary scripts, scratch experiments, intermediate log outputs, generation prototypes, or ad-hoc diagnostic tools MUST reside strictly within a dedicated `/Scratchpad` directory (`Scratchpad/`).
- Never write scratch files, loose temporary images, or ad-hoc test scripts into the repository root or project source folders (`Source/`, `DisplayPresentation/`, `Projects/`, etc.). Keep the working tree completely clean and organized.
