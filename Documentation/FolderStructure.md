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

### Room convergence diagnostic

- `Exhibits/LightCompute/Code/ReplayRoomConvergenceValidation.h` independently samples double triangle-area NEE
  and power MIS, compares the existing BSDF reference, and tests an analytic near-emitter rectangle.
- `RunReplayRoomConvergence.sh`, `ReplayRoomConvergenceImages.py`, `ReplayRoomConvergenceParity.py` and
  `ReplayRoomConvergence.md` own fixed cohort logs, plots, historical-prefix parity and diagnostic evidence.

### Temporal / spatial reuse ablation

- `Exhibits/LightCompute/Code/ReplayReuseAblationValidation.h` isolates temporal-only and spatial-enabled seed
  reuse using the independently checked CPU power reference. Existing transport derivations are unchanged.
- `RunReplayReuseAblation.sh`, `ReplayReuseAblationImages.py`, `ReplayReuseAblationParity.py` and
  `ReplayReuseAblation.md` retain paired error/cost evidence, feature-isolation checks and actual readbacks.

### Fresh D/I correlation and lower-budget whole-path controls

- `Exhibits/LightCompute/Code/ReplayCorrelationValidation.h` measures coupled/independent fresh D8/I8,
  Whole8, Whole6 and temporal-only reuse with fixed seeds and empirical MSE/covariance decomposition.
- `CompileReplayCorrelation.py` derives audit-only fresh-I seed control and six-path whole transport;
  production shaders, storage layouts and defaults are unchanged.
- `RunReplayCorrelation.sh`, `ReplayCorrelationImages.py`, `ReplayCorrelationParity.py` and
  `ReplayCorrelation.md` retain query/error frontiers, actual readbacks and cross-build verification.

### Secondary-vertex reconnection correctness gate

- `Exhibits/LightCompute/Code/SecondaryReconnectionValidation.cpp` reuses production `Reservoir` for bounded
  two-diffuse-vertex transport; independent area quadrature checks angular/area measures and visibility support.
- `RunSecondaryReconnection.sh` builds optimized and host-sanitized native checks without Vulkan dependencies.
- `SecondaryReconnectionEvidence.py`, `SecondaryReconnection.md`, retained results and measured plots document
  correct Jacobian placement, null-candidate accounting and deliberate proposal/target-support failures.

### Analytic secondary-vertex Vulkan gate

- `Exhibits/LightCompute/Code/SecondaryReconnectionGpu.slang` samples/reconnects bounded diffuse vertices using
  production RNG and descriptor definitions; receiver visibility and null/support controls are explicit.
- `SecondaryReconnectionGpuValidation.h` reuses the existing exhibit Vulkan uploader/submitter and checks actual
  readbacks against independent clipped-area quadrature; no BVH camera-render or persistent-history claim.
- `RunSecondaryReconnectionGpu.sh`, `SecondaryReconnectionGpuEvidence.py` and `SecondaryReconnectionGpu.md`
  retain release/sanitizer-host evidence, shader/tool hashes and exact raw-readback/PNG parity.

### Triangle secondary-vertex transport

- `Exhibits/LightCompute/Code/CompileTriangleReconnection.py` derives actual packed-triangle proposals, production
  BVH first hits/visibility and a diffuse directional-light suffix from the frozen analytic estimator template.
- `TriangleReconnectionValidation.h` authors the nonemissive patch/opaque strip and independently integrates
  the restricted indirect component; pointwise outgoing probes check the oblique source shadow separately.
- `RunTriangleReconnection.sh`, `TriangleReconnectionEvidence.py` and `TriangleReconnection.md` retain host-build
  parity, actual estimator/suffix readbacks, nearest-facet call counts and query/error comparisons.

### Persistent vertex/outgoing payload lifecycle

- `Exhibits/LightCompute/Code/CompileVertexCache.py` derives source-payload generation and receiver evaluation
  from measured triangle transport. Two float4 payload arrays persist across submissions; telemetry is separate.
- `VertexCacheValidation.h` tests eight lifecycle steps with light/geometry/material invalidation, same-seed
  inline pairing and deliberately stale caches. Production render storage and policies are not changed.
- `RunVertexCache.sh`, `VertexCacheEvidence.py` and `VertexCache.md` retain query savings, lifecycle readbacks,
  host-sanitizer parity and limitations of frozen sample banks.

### Bounded refresh and receiver-motion temporal error

- `Exhibits/LightCompute/Code/CompileCacheRefresh.py` derives correct refresh1/4/8 payloads and fresh cosine6/8
  component controls; sampler densities, source target and revision policy remain fixed.
- `CacheRefreshValidation.h` tests16 receiver poses,32 cohorts and a new .3 strip holdout. It measures trajectory
  mean-residual error, instantaneous error, residual differences/correlation, paired MSE and full query work.
- `RunCacheRefresh.sh`, `CacheRefreshEvidence.py` and `CacheRefresh.md` retain actual readbacks and the distinction
  between low redraw flicker and correlated estimation error. No production rendering change.

### Density-bearing candidate bank

- `Exhibits/LightCompute/Code/CompileCandidateBank.py` retains eight raw area-density-bearing source proposals,
  rotates4/2 slots, and compares full refresh with independently evaluated inline area-IS8.
- `CandidateBankValidation.h` checks current-receiver means, fixed8 null normalization, exact full-refresh/query
  identity and17,039,360 retained-slot comparisons; captures output plus actual candidate payloads.
- `RunCandidateBank.sh`, `CandidateBankEvidence.py` and `CandidateBank.md` retain cross-build/capture evidence,
  independent density/estimator reconstruction, memory tradeoffs and paired moving-receiver error comparisons.

### Receiver-anchored historical-density bank

- `Exhibits/LightCompute/Code/CompileReceiverBank.py` changes only generation anchors in the candidate bank;
  historical q remains stored per surviving draw, with the same8-slot/rotate4/2 payload and revision rules.
- `ReceiverBankValidation.h` tests moving-receiver means/temporal error and actual queries;the CPU counterfactual
  reweights captured rotate4 candidates with current instead of historical q and requires a detected mean error.
- `RunReceiverBank.sh`, `ReceiverBankEvidence.py` and `ReceiverBank.md` retain birth-pose density reconstruction,
  exact cross-build captures and the discovered inherited-control query redundancy;no optimized-speed acceptance.

### Exact-sample ray-budget safety gate

- `Exhibits/LightCompute/Code/CompileRayBudget.py` pairs legacy/query-elided receiver-bank arms and adds actual
  nearest-facet boundary probes. `RayBudgetValidation.h` rejects any first-hit/reconstructed-visibility disagreement
  before allowing the twelve-arm trajectory/work comparison.
- `RunRayBudget.sh` currently exits1 at that numerical gate in both release/host-sanitized builds. This is not
  accepted query-elision behaviour. `RayBudgetEvidence.py` verifies the rejection,not a successful runtime pass.
- `RayBudget.md`, retained attempt/result logs and `Images/RayBudgetEdges.png` identify three disagreements at one
  shadow-boundary configuration. Next:instrument original/reconstructed ray parameters and their visibility contract.

### Confirmed reconstructed-point visibility reuse

- `Exhibits/LightCompute/Code/CompileVisibilityReuse.py` preserves the reconstructed-point query once per valid
  new proposal,reusing its confirmed decision;historical entries retrace. Fresh null proposals alone skip recheck.
- `VisibilityReuseValidation.h` measures all12 legacy/confirmed arms against fixed32-cohort references,checks
  payload/estimate identity and exact null-query savings,and records original/reconstructed ray diagnostics.
- `RunVisibilityReuse.sh`, `VisibilityReuseEvidence.py` and `VisibilityReuse.md` retain both successful builds,
  4096 ULP/boundary probes,the three preserved original regressions,independent q reconstruction,136 raw hashes,
  actual estimate PNGs and fair corrected-control query/quality comparison. Old RayBudget rejection still stands.

### Positive-target weighted pair

- `Exhibits/LightCompute/Code/CompileWeightedPair.py` derives the confirmed-query sampler and compresses4 draws
  per selected sample with positive geometry h,birth Z and separate selection stream. Two-lane alternating refresh.
- `WeightedPairValidation.h` retains all12 controls plus full2/rotate1,checks epochs/history/empty-strip identity,
  and captures selected samples,source proposals,selection uniforms and weights for independent float32 replay.
- `RunWeightedPair.sh`, `WeightedPairFinite.py`, `WeightedPairEvidence.py` and `WeightedPair.md` retain dual-build
  correctness,explicit quality rejection,full baseline/alternative comparisons and actual-buffer PNGs.48B is logical
  history only,not measured allocation savings;production output and default settings are unchanged.

### Positive-target weighted quartet

- `Exhibits/LightCompute/Code/CompileWeightedQuartet.py` reuses the pair/confirmed sampler,selects4 reservoirs from
  2 proposals each,and alternates2-reservoir refresh with birth normalization.80B logical scalar history only.
- `WeightedQuartetValidation.h` keeps all14 previous controls,adds full4w/rotate2w and new .45 holdout,checks
  epochs/retention/empty-strip identity,collects all mean gates,and captures source weights/uniforms/selections.
- `RunWeightedQuartet.sh`, `WeightedQuartetFinite.py`, `WeightedQuartetEvidence.py` and `WeightedQuartet.md` retain
  dual-build correctness,five-fixture acceptance,complete alternatives,actual-buffer PNGs and independent float32
  replay. No measured allocation/bandwidth/physical-time saving or production/Windows/editor/RGB acceptance.

### RGB facet descriptor and positive-support prerequisite

- `Exhibits/LightCompute/Code/FacetReservoirDescriptor.h` shares the unpacked32B C++/Slang descriptor definition;
  `FacetReservoirGpu.slang` reuses production facet interpolation through actual two-float4 wire transport.
- `FacetReservoirValidation.h` owns the eight-facet/six-phase native double reference and corruption/revision
  tests. `FacetReservoirLayout.cpp` compares native RGB packets;`FacetReservoirStride.slang` is a separate
  typed-buffer SPIR-V layout probe,not a runtime typed-buffer acceptance claim.
- `FacetReservoirFinite.py`,`FacetReservoirEvidence.py`,`RunFacetReservoir.sh` and `FacetReservoir.md` retain the
  finite RGB support checks,reproducible native runs,exact parity,charts and limits. Results retain logs,hashes,
  static probe SPIR-V and six lossless `.f32.gz` captures;Images retains12 exact arrays and two labelled charts.
  The prerequisite does not install RGB sampling or change production348.046875MiB/defaults.

### Integrated RGB quartet selection

- `Exhibits/LightCompute/Code/RgbQuartetGpu.slang` selects four shared RGB facet descriptors from eight finite
  proposals,writes wire slots,and reads/evaluates them in a second dispatch using the existing host barrier.
- `RgbQuartetValidation.h` owns independent double reconstruction,RNG replay,selected descriptor and RGB checks,
  sixteen receiver pairs and four epoch/null controls;`LightComputeValidation.cpp` exposes `--rgb-quartet`.
- `RunRgbQuartet.sh`,`RgbQuartetEvidence.py` and `RgbQuartet.md` retain native builds,exact cross-build parity,
  raw-eight and incorrect-normalization comparisons,and the explicit fresh-quality rejection. Results retain
  lossless captures/compiled SPIR-V,logs,generated table,audit and hashes;Images retains exact RGB arrays/charts.
  This exhibit does not install persistent RGB histories,trace suffixes or change production348.046875MiB/defaults.

### Persistent RGB quartet reuse

- `Exhibits/LightCompute/Code/RgbReuseGpu.slang` reuses RGB selector/production facet interpolation functions,
  persists four wire descriptors plus epoch metadata,rotates two lanes,and rebuilds changed epochs before decode.
- `RgbReuseValidation.h` independently replays descriptor identities/birth weights/RGB,compares retained bytes,
  verifies reset/work counters and exercises three32-step receiver tracks. `--rgb-reuse` exposes the isolated run.
- `RunRgbReuse.sh`,`RgbReuseEvidence.py` and `RgbReuse.md` retain reset-inclusive raw/fresh comparisons,exact
  cross-build parity,paired error intervals and the all-track rolling-quality rejection. Results retain96 lossless
  captures,two SPIR-V modules,logs,generated metrics/means and hashes;Images retains24 actual arrays/two charts.
  No traced RGB,visibility,BVH-refit,camera/TAA,physical-GPU timing or production-allocation promotion is implied.
