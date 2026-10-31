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
| `Frontier/Projects/`             | `.frontier` project descriptors and project content.  |
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

## Project Everest UI-first port (2026-10-30)

### Pinned branch references and scope

| Source | Pinned ref | Role and evidence limit |
|---|---|---|
| `SultanAladin/Frontier`, `arena/11f31835-frontier` | `71e4a17ed543325061b6e5f392476f5c9fd26699` | Production UI foundations, `ControlCentrePanel`, `PanelDockspace`, theme, and `.frontier` host/browser. Use this for the UI shell, not the separate `.projectspace` catalogue. |
| `egoistm1inbox/Frontier`, `arena/9ce908bc-frontier` | `f97b991440fc90476383ba3f5cf124addc590f18` | Native Vulkan Project-Drive reference and LogStream work. Treat renderer and hardware-path statements as claims until reproduced. |
| `streamlinkinbox/Frontier`, `arena/08e7de48-frontier` | `7bc60250e153a9b7d70c9740744723fde19fae22` | Exhibit-only ReSTIR GI studies; research source, not a production renderer base. |
| `streamlinkinbox/Frontier`, `arena/5ec53579-frontier` | `56059b393f2d1a6f4ecf73c0f7661aa74f2de442` | Ray-query/software-BVH and temporal-visibility audits. Its SwiftShader evidence does not establish hardware RT-core performance. |

These are role-specific references, not four interchangeable bases. The `.frontier` host is distinct from the `.projectspace` catalogue. The inherited `Exhibits/ProjectBrowser` and `Exhibits/LogStream` screenshots are source-branch references; name them as such, not as Project Everest proof. The branch-selection comparison and evidence limits are in `Exhibits/ProjectEverest/Results/BranchBasis.md`. Preserve the refs in `Frontier/Projects/Project-Everest/Upstream.json` and in exhibit provenance.

### UI-first native port

Keep scene rendering disconnected and do not deliver product binaries in this phase. The native flow is Frontier application browser → choose **Frontier Editor** → choose **Project Everest** (`ProjectEverest.frontier`) → native workspace. `Frontier/Applications/Editor/FrontierMain.cpp` uses the production Control Centre/theme and `PanelDockspace`; the editor shell places Outliner left, Viewport over Log Stream in the centre, and Inspector right. The descriptor intentionally has no `OpeningScene` or `CodeImage`, so the Viewport remains an honest empty canvas. The VehicleHMI/ReSTIR HTML is reference material, not a browser deliverable or substitute for native C++ UI.

The current native UI evidence is under `Exhibits/ProjectEverest/Images/` and is generated from C++ ImGui draw data by `Exhibits/LogStream/PanelCapture.cpp`. `FrontierNativeEntry` compiles the GLFW-backed entry point; `NativeProjectValidation` checks `.frontier` enumeration/decoding and the record→Outliner→Inspector lifecycle; the native browser capture drives the actual ImGui selection buttons. These are CPU-rasterized UI and local validation results, not an OS-window, Vulkan-renderer, or GPU proof. The project CMake/CTest instructions and results are in `Exhibits/ProjectEverest/README.md` and `Exhibits/ProjectEverest/Results/NativeValidation.txt`. No product binary is checked in.

Continue the editor work in these bounded steps:

1. **Implemented:** native application selection, `.frontier` project selection, and Project Everest workspace hand-off; keep the `.projectspace` catalogue separate.
2. **Implemented as the UI shell:** production-docked Outliner, Viewport, Inspector, and Log Stream; reuse the pinned egoistm1inbox Log Stream/Outliner/Inspector code and the main/UI Control Centre/theme. Project-specific scene loading and renderer-backed Content Browser behaviour remain future work.
3. **Component lifecycle contract:** constructing an entity registers it in the Outliner and creates its user-authored Inspector description; destroying it unregisters the entity and removes that Inspector description. Vehicle designs and their Inspectors are user-created, not mandatory built-ins. Reusable components such as Sun/Sky are available from the library and need not be recreated by every project.
4. **Dedicated editors:** fracture, fluid, and particle authoring each receive a dedicated editor through the same docking/theme/panel contracts; do not overload the generic Inspector.
5. Only after the UI slice is reviewed, connect project scenes, the C++ Vulkan renderer, and GPU-specific rendering paths.

The native workspace explicitly labels its scene canvas and renderer as disconnected. The CPU-rasterized images must not be represented as a GLFW/Vulkan window capture. Keep native captures, build results, and their generating exhibit code together under `Exhibits/ProjectEverest/`.

### Rendering and fallback claims

The high-end path is intended for RTX-capable hardware. A lower-end GTX path is intended to use SDF with BVH reflections. Current branch evidence does **not** independently prove that GTX fallback, physical RT-core execution, BLAS refit, swapchain presentation, or performance. The 5ec branch reports ray query plus an ordered software BVH (including SwiftShader); the 9ce branch's recorded shader reproduction is blocked by missing `slangc`. Do not describe the GTX fallback or hardware RT as verified until fresh, reproducible evidence exists.

### Standing upstream sync after every commit

After **each local commit**, fetch and merge the designated UI upstream into the current session branch before the next change. Never switch branches; remain on `arena/41ab06b4-frontier`. Use the exact source branch, record the fetched commit SHA in `Upstream.json`, resolve conflicts without dropping this branch's work, then rerun the relevant local checks and inspect `git status`/`git diff`:

```sh
git fetch --no-tags https://github.com/SultanAladin/Frontier.git refs/heads/arena/11f31835-frontier
git merge --no-edit FETCH_HEAD
```

This checkout may be shallow; if Git cannot find a merge base, deepen/fetch ancestry and inspect the graph first. Do not use `--allow-unrelated-histories` to force a sync. If complete histories still have no common ancestor, compare the selected shared UI files against the pinned source commit and record that no Git merge is possible; do not synthesize an unrelated merge. This feature branch and the pinned SultanAladin UI branch currently have independent roots; the post-commit fetch/check is recorded in `Exhibits/ProjectEverest/Results/BranchBasis.md`. A local commit is not permission to push or run GitHub Actions: both require separate explicit user approval.
