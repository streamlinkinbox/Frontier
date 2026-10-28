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
├── Engine/                    # Shared production code
│   ├── Interface/             # Selectively ported UI foundations
│   ├── Lifecycle/             # Shared activation sequence
│   └── Render/                # Blank; Git retention marker only
├── UI/
│   └── Editor/                # Lifecycle-owned production editor UI
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
├── EnvironmentArchives/       # Tracked baked equirect studio environments (+ user imports)
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


## Bounded path replay ownership — 2026-10-27

- `Frontier/Engine/Rendering/PathReplayFootprint.h` owns shared C++17-compatible GPU input footprints;
  `PathReplayDomain.{h,cpp}` owns C++20 bounded domain guards, emissive-facet indices and history-cap tracking;
  `FrontierPathReplayDomain` is their reusable CMake library.
- `Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang` owns the corresponding bounded shader.
  `Exhibits/LightCompute/` retains fixture generation, an isolated Vulkan submission host and verification.
- The root editor target does not yet submit this shader; the shared location is not a production-integration claim.

## Shared radiant-facet distribution — 2026-10-27

- `Frontier/Engine/Rendering/RadiantFacetDistribution.{h,cpp}` and CMake target
  `FrontierRadiantFacetDistribution` own area/luminance sampling and encoded-distribution validation.
- `Exhibits/LightCompute/` retains archive offsets, independent comparisons and the isolated Vulkan submission;
  this extraction does not connect the root editor to the GPU shader.

## VehicleHMI reference and celestial content — 2026-10-27

- `Experimental/VehicleHmiReference/`: selected immutable full-demo sources, blob manifest and original tyre tests.
- `EngineContent/CelestialMaps/`: retained demo lunar JPEG and provenance; generated runtime PPM stays ignored.
- `Exhibits/VehiclePhysics/Continuation.md`: source audit, renderer comparison, validation and remaining scope.

## Retained-seed reservoir continuation — 2026-10-28

- `Frontier/Engine/Rendering/PathReplayVulkan.*` owns the opt-in seed-reservoir contract and dispatch dependencies.
- `Shaders/PathReplayReservoir.slang` owns local seed RIS; `Shaders/PathReplaySampleHistory.slang` gathers and
  shifts unselected seed banks. Spatial resolution reuses the shared replay shader, not a copied implementation.
- `Exhibits/LightCompute/Code/ReplaySeedReservoirValidation.h` retains numerical/guard verification;
  `ReplaySeedReservoirImages.py` retains presentation from actual captures and logs.
- `Automation/PrepareReplayTools.py` and `ReplayToolDependencies.json` recover ignored Linux host tools only,
  not production application dependencies or Windows deliverables.

## Compact seed-history layout — 2026-10-28

- Shared `PathReplayVulkan.h` owns compact footprint, logical-slot indexing and address limits; its recorder
  enforces the layout's sample-history domain. Shared primary/replay Slang owns corresponding addressing.
- `Exhibits/LightCompute/Code/ReplayCompactStorageValidation.h` owns paired actual-allocation/state verification;
  `ReplayCompactStorageImages.py` presents retained Vulkan readbacks and measured allocation rows.
  This does not relocate production buffer ownership or establish editor integration.


## Eight-current-path replay comparison — 2026-10-28

- `Exhibits/LightCompute/Code/ReplayFreshBudget.slang` is a validation-only eight-fresh generation control;
  it includes the shared engine path evaluator, rather than copying transport code or adding a production mode.
- `ReplayPathBudgetValidation.h` owns equal-call masks, cold-control/reference verification and actual captures;
  `ReplayPathBudgetImages.py` and `ReplayPathBudgetParity.py` retain presentation and final evidence checks.
- Shared replay Slang contains default-off call/query telemetry; no footprint change or additional buffers.
  `RunReplayPathBudget.sh`, `ReplayPathBudget.md`, `Results/` and `Images/` retain reproducible evidence.


## Temporal seed measure and selected-source safety — 2026-10-28

- Shared `Shaders/PathReplayReservoir.slang` owns finite/canonical source validation and checked contribution
  accumulation; its unchecked compile definition is reserved for exhibit negative controls.
- `Exhibits/LightCompute/Code/ReplayTemporalMeasureValidation.cpp` owns the independent finite-space enumeration;
  `ReplayTemporalContractValidation.h` exercises the shared production shader and analytic current rectangles.
- `ReplayTemporalContractImages.py` and `ReplayTemporalContractParity.py` retain readback presentation and evidence
  checks. `RunReplayTemporalContract.sh` and `RunReplayTemporalRegression.sh` reproduce acceptance/regression.
  `ReplayTemporalContract.md`, `Results/`, `Images/` own the report and retained evidence; no prototype moved into Engine.


## Bounded selected-temporal validation prototype — 2026-10-28

- `Exhibits/LightCompute/Code/CompileReplayBoundedTemporal.py` derives bounded/cached and uncached validation
  shaders, failing closed on source drift; production shaders and default prohibition remain unchanged.
- `ReplayBoundedTemporalMeasure.cpp` owns overlapping finite-model checks; `ReplayBoundedTemporalValidation.h`
  owns current-call accounting, cache float/slab parity, independent references, faults and actual captures.
- `RunReplayBoundedTemporal.sh` builds/runs release or host sanitizer; `ReplayBoundedTemporalImages.py` and
  `ReplayBoundedTemporalParity.py` own presentation and provenance checks. `ReplayBoundedTemporal.md`,
  `Results/ReplayBoundedTemporal-*` and `Images/Bounded*` retain evidence; tools/builds/raw captures stay ignored.


## Independent emitter-enabled temporal holdout — 2026-10-28

- `Exhibits/LightCompute/Code/ReplayTemporalHoldoutValidation.h` fixes independent seeds, four motion/albedo
  conditions, geometric masks, CPU references and explicit correctness/promotion gates. A separate CLI entry
  in `LightComputeValidation.cpp` calls it without changing the preceding algorithm.
- `RunReplayTemporalHoldout.sh` builds release/host sanitizer and retains source/tool/binary hashes;
  `ReplayTemporalHoldoutImages.py` and `ReplayTemporalHoldoutParity.py` present/audit all first-batch captures.
- `ReplayTemporalHoldout.md`, `Results/ReplayTemporalHoldout-*` and `Images/Holdout*` retain the experiment.
  Existing derived shaders/defaults are unchanged; generated binaries and raw readbacks remain ignored.


## Fresh reset-budget correction — 2026-10-28

- `Exhibits/LightCompute/Code/CompileReplayResetBudget.py` derives two metadata-selected reset branches from
  the unchanged bounded prototype. The existing finalizer preserves the original uniform seed/bank contract.
- `ReplayResetBudgetValidation.h` owns exact Fresh8/reset and non-reset continuation comparisons, independent
  CPU reference and separately charged path work. `RunReplayResetBudget.sh` reproduces release/host sanitizer.
- `ReplayResetBudgetImages.py`, `ReplayResetBudgetParity.py`, `ReplayResetBudget.md`, result logs and
  `Images/Reset*` retain first-batch presentation and current provenance. No shared production shader changes.


## Fresh-direct / indirect-only replay — 2026-10-28

- `Exhibits/LightCompute/Code/CompileReplayIndirectSplit.py` derives first-bounce and deeper evaluators plus
  diagnostic entries from the existing reset-budget prototype, failing closed on source drift.
- `ReplayIndirectSplitValidation.h` owns same-seed decomposition, fresh-direct isolation, matched/whole controls,
  independent colored-room references and separately counted direct/replay nearest/shadow work.
- `RunReplayIndirectSplit.sh` builds release or host ASan/UBSan. `ReplayIndirectSplitImages.py`,
  `ReplayIndirectSplitParity.py`, `ReplayIndirectSplit.md`, result logs and `Images/Split*` own retained evidence.
  Current RGB is temporary direct scratch; no primary diagnostics, history layout or production shader changes.

### First-hop / MIS checkpoint

- `Exhibits/LightCompute/Code/CompileReplayFirstHop.py` derives invocation-local same-seed first-hit sharing.
- `ReplayFirstHopMISValidation.h` adds exact unfused equivalence and paired whole MIS/no-MIS controls.
- `RunReplayFirstHopMIS.sh` builds isolated release/host-sanitized controls; `ReplayFirstHopMISImages.py` presents
  captures and `ReplayFirstHopMISParity.py` checks logged artifacts and cross-build results.
- `Exhibits/LightCompute/ReplayFirstHopMIS.md` and matching result/image names retain no-promotion evidence.

### Power-heuristic checkpoint

- `Exhibits/LightCompute/Code/CompileReplayMISPower.py` derives complementary beta=2 weights in both the reset
  source and the actual FreshBudget evaluator; `ReplayMISPowerValidation.h` owns paired controls and references.
- `RunReplayMISPower.sh`, `ReplayMISPowerParity.py`, `ReplayMISPowerImages.py`, `ReplayMISPower.md` and matching
  result/image names retain isolated validation, readbacks and reproducibility checks. Production is unchanged.

### Power-weighted indirect reuse checkpoint

- `Exhibits/LightCompute/Code/CompileReplayPowerReuse.py` derives fixed power weights consistently in the fused
  split, unfused split and actual whole Fresh8 evaluator; the existing beta=2 function is reused verbatim.
- `ReplayPowerReuseValidation.h`, `RunReplayPowerReuse.sh`, `ReplayPowerReuseImages.py`,
  `ReplayPowerReuseParity.py` and `ReplayPowerReuse.md` retain isolated tests and evidence; production is unchanged.
