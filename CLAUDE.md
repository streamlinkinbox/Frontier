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

## VehicleHMI / Project-Drive source ledger (verified 2026-10-23, `arena/10a5e6ab-frontier`)

📝 Full evidence, fix list, and merge/port plan: `Documentation/Plans/Ongoing/VehicleHmiPortRecovery.md`.
XPBD build/run logs: `Exhibits/VehiclePhysicsRecovery/`. Read the ledger before any vehicle, tyre, or
renderer port work. Do not re-derive these pins from branch-name guesses.

| Role | Pinned source (`arena/…` branch) | Revision |
| --- | --- | --- |
| Main project (merge target) | `SultanAladin/Frontier` `649fea90` | `4ef9f213fb9f` |
| Last batch: native Jolt + vehicle | `c7egoist/Frontier` `ec6ca01d` | `d99f5e51aa11` |
| Original complete C++ vehicle (Project-Drive) | `SultanAladin/Frontier-` `215a6f39` | `f2cfc57daaaf` |
| Final native CPU XPBD tyre (pre-removal) | `Frontier-`, ancestor of 215 head | `38d28c39c202` |
| Full HTML demo: spring+XPBD+shell/solid FEM | `SultanAladin/Frontier` `e9bd7db5` | `6afb6f00b5f2` |
| Same demo, origin of that import | `c7egoist/Frontier` `86baabb5` | `819bd55` |
| Older demo copies inside 649/ec6 (`SlateReference`) | Slate pins A / B | `8a700498` / `c552cb26` |
| Native GPU XPBD (Vulkan/Slang, separate) | `SultanAladin/Slate` `ca517016` | `465b55a` |
| Historical rigid-body reference only | `SultanAladin/Frontier-` `9440f4d0` | `338293863d7d` |

Last 215 vehicle-source edit: `f1f0d72e54ef`. XPBD removal: `9ceb33cefa24` (child of `38d28c3`).
Full demo path: `Experimental/VehicleHmiPrototype/`. GPU XPBD implementation: `8e79925065f8`.

- 🔴 Lineage: Project-Drive C++ (`Frontier-` 215) → copied into the JS demo (`VehicleSolver.js` header
  states this; contact simplified to point + spring) → native C++/Vulkan port target is 649 + ec6.
  Jolt pin is one revision everywhere: `0373ec0dd762` (215 lock file = ec6 `Automation/JoltDependencies.json`, 5.3.0).
- 🔴 Tyre mechanisms: spring + reduced XPBD + shell FEM + solid FEM exist together only in the e9bd/86ba
  JS demo (`TyreModel` selector). Native today: trace-fan + Pacejka (215 head, ec6) and the recovered
  CPU XPBD lattice at `38d28c3`. No native C++ FEM tyre exists in any inspected repository; native FEM
  is new work from the JS mathematics, never a recovered port.
- 🔴 Renderer: native ReSTIR/SDF/card/SVGF exist as validated CPU portable cores + frame declarations
  (`Frontier/Engine/Rendering/`). Vulkan recording, Slang unit bodies, hardware ray tracing, and demo
  golden-image parity do not exist yet (SDK phase). Do not report the GPU rendering pipeline as finished.
- Merge needed: 649 already contains ec6 up to 2026-10-22 (`9b103cb`). Outstanding is ec6 Phase 7 only:
  7 commits `60b64db..d99f5e5` on shared base `38e6215` (44 added files + `CMakeLists.txt` + 2 Docs).
- Owner direction retained: cabin HMI display deferred; do not fixate on Jolt/vehicle physics ahead of
  the ReSTIR + ray-tracing SDK phase; improve on the demo where C++/Vulkan allows, with evidence.
