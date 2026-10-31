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

## Upstream synchronisation and reference engine branches

- **Main upstream branch**: `https://github.com/SultanAladin/Frontier/tree/arena/11f31835-frontier`
  (`SultanAladin/Frontier`, branch `arena/11f31835-frontier`).
- **Mandatory post-commit sync rule**: After every local commit, fetch `SultanAladin/Frontier`
  `arena/11f31835-frontier` and ensure the active branch remains synchronised with main (`git fetch` and merge any new
  upstream commits) so reusable UI, docking, Control Centre, and build updates never diverge. Remote pushes and PRs
  still require explicit user approval under the GitHub Actions rule above.
- **Reference engine branches** (retained for **Project Everest** Vulkan C++ porting):
  1. `https://github.com/streamlinkinbox/Frontier/tree/arena%2F08e7de48-frontier`
     (`streamlinkinbox/Frontier`, branch `arena/08e7de48-frontier`) — Slang compute kernels for solid and thin-pane
     dielectric glass (`DielectricBoundary.slang`, `SolidDielectricTransport.slang`, Beer-Lambert transmittance),
     6-pose `ControlVehicle` glazing, roughness-cone prior-screen exitance, and fence-published `FacetCdfSubmission`.
  2. `https://github.com/streamlinkinbox/Frontier/tree/arena%2F5ec53579-frontier`
     (`streamlinkinbox/Frontier`, branch `arena/5ec53579-frontier`) — Hardware RTX `PathReplayRayQuery`
     (`VK_KHR_acceleration_structure` + `VK_KHR_ray_query`, SPIR-V 1.4) with 16-bin SAH-2 `OrderedSoftwareBvh`
     fallback, motion SVGF (`PathReplayVarianceFilter.slang`), `beta = 2` Power MIS, blocked-NEE temporal visibility
     guard, and naming-compliant `EnvironmentRecords` / `RecordEditorDescription` / `SceneInspectorPanel`.
  3. `https://github.com/egoistm1inbox/Frontier/tree/arena%2F9ce908bc-frontier`
     (`egoistm1inbox/Frontier`, branch `arena/9ce908bc-frontier`) — `LogStreamPanel` / `Diagnostics`, 25-record
     Outliner grouping and Dedicated Studio UX reference, Jolt `v5.6.0` vehicle physics reference, 41k Hipparcos sky
     and Pyro fluid reference, and dual-tier GPU hardware contract (`HardwareRayTracingRtx` vs
     `SignedDistanceFieldGtx`). Never import its CPU-raytraced `SceneRenderer.cpp`, dummy SPIR-V
     `VulkanShaderCompiler.cpp`, older `PanelDockspace` / `ControlCentrePanel`, or banned `Entity` / `*Id` names.

## Project Everest — dual-tier GPU contract and editor scene architecture

- **Application identity & lifecycle loop**: `Frontier/Projects/Project-Everest/ProjectEverest.frontier`
  (**Project Everest**). Host entry lives in `Frontier/Applications/Editor/WorkspaceApplication.{h,cpp}`
  (`WorkspaceApplication::Initialize -> Loop -> Finalize`, `ExecuteWorkspaceApplication`) — never use `Main`,
  `FrontierMain`, or `MainLoop` in application/loop identifiers (`SKILL-Naming.md`). Single unified
  `RunWindowsApplication` loop transitions seamlessly between `ProjectBrowserPanel` and the 3-column docked workspace
  (`Outliner` | `Viewport` over `LogStream` | `Inspector`) governed by `ControlCentrePanel`.
- **Dual-tier Vulkan GPU rendering** (Slang `.slang` compute/ray-query shaders; never CPU raytracing):
  - **RTX tier** (hardware RT capable GPUs via `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`): hardware BVH
    traversal, ReSTIR DI/GI spatiotemporal reservoir reuse, closed/thin-pane dielectric glass transport, and motion
    SVGF reconstruction.
  - **GTX / lower-end GPU fallback tier** (non-RTX GPUs): Vulkan `.slang` port of the 4 nested toroidal 3D signed
    distance field cascades (`SceneClip0..3`) + `SurfaceCards` multi-bounce GI + nearest-depth screen-space pyramid,
    paired with 2-level SAH triangle **BVH reflections** (`OrderedSoftwareBvh`).
- **Editor scene item (`RecordEntry`) lifecycle and Inspector architecture** (naming-compliant; no banned `Entity`
  symbol names in C++):
  - Every scene item (`RecordEntry`, user-facing Outliner entry) instantiated in Editor/Development profiles owns a
    paired **constructor and destructor**:
    - **Constructor**: registers the item in `SceneDirectory` (`SceneDirectoryPanel` Outliner) under its enclosing
      `FolderRecord` / parent and registers its `RecordEditorDescription` (`SceneInspectorPanel`).
    - **Destructor**: detaches its `RecordEditorDescription`, clears active selection if selected, re-roots enclosed
      records cleanly, and unregisters the item from `SceneDirectory` with zero dangling Outliner rows or inspectors.
  - **Three-tier authoring split**:
    1. **Reusable engine components** (`Frontier/Engine/Scene/EnvironmentRecords.{h,cpp}` +
       `Frontier/Engine/Interface/SceneInspectorPanel.{h,cpp}`): global editor components shared across all projects —
       **Folders** (`FolderRecord` with cascade visibility & colour swatches), **Sun** (`SunRecord`), **Lens Flare**
       (`LensFlareRecord`), **Atmosphere** (`AtmosphereRecord` wired to `AtmosphereModel`), **Clouds** (`CloudRecord`
       wired to `CloudField`), **Wind** (`WindRecord`), **Fog** (`FogRecord` wired to `FogMedium`), **Stars**
       (`StarsRecord` Hipparcos planisphere), **Moon** (`MoonRecord` wired to `CelestialModel`), and **Weather / Storm**
       (`WeatherRecord` wired to `WeatherEffects` + chunked surface retention grid) — live in Engine, never trapped
       inside a single project.
    2. **User-created project Inspectors** (`Frontier/Projects/<Project>/Source/`): custom project designs (such as
       **Project Everest**'s 8-module `EverestVehicleRecord` `ControlVehicle` + Collision X-Ray overlay,
       `EverestTyreRecord`s, and `EverestIlluminationRecord` RTX/GTX tier + 6-AOV selector) author their own
       `RecordEntry` subclass and custom Inspector controls.
    3. **Dedicated domain editors / studios** (`Frontier/Engine/Interface/DomainStudioPanels.{h,cpp}` + `Engine/` +
       `UI/Editor/`): specialized domains open dedicated editors rather than crowding generic property cards —
       Fracture Voronoi Studio, Fluid & Plume Lattice Studio (`PlumeLatticeFootprint`), Particle Emitter Studio,
       Object/Mesh Studio, and Texture/Material Studio.

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
