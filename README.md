# Frontier

General-purpose, cross-platform editor with reusable engine code.
EngineUpdate establishes documentation, audits inherited violations, and repairs code in reviewed batches.

## Current VehicleHMI continuation

The earlier branch `arena/2ac8ddab-frontier` is continued on `arena/672a179a-frontier`.
See the [continuation report](Exhibits/ContinuationReport.md) and the detailed
[vehicle source audit and executed checks](Exhibits/VehiclePhysics/Continuation.md).
The actual demo Moon texture, mixed flares and selectable XPBD/solid tyre shapes are retained and tested.
Project Drive's vehicle core is separately available, but is not wired into the editor or a Jolt host. Long-impact
parity and validation against measured tyres, vehicles, sky and lens data remain open.

## Start here

- Read [agent instructions](CLAUDE.md) and the four mandatory skills before contributing.
- Follow the [folder map](Documentation/FolderStructure.md) and [workflow](Documentation/Rules/AgentWorkflow.md).
- Track all progress and findings in the [single tracker](Documentation/Tracker.md).
- Follow the [active plan](Documentation/Plans/Ongoing/EngineUpdate.md).
- Retain reproducible evidence under [Exhibits](Exhibits/README.md).
- Build the [Control Centre, fonts, and themes](Exhibits/ControlCentre/README.md).

## Baseline status

Approved source baseline: `SultanAladin/Frontier-` at `338293863d7df4e585dec0c4acae45fe9c524ed5`.
This checkout is `SultanAladin/Frontier`, initially at `4a77bfd17f23536e32781718553ec6a171e39021`.
A selective UI foundation and Control Centre port is now present; the rest of the linked engine has not been imported or merged.
See [port evidence and Windows build steps](Exhibits/UIFoundation/README.md).
That selective-port baseline predates the current VehicleHMI continuation above.

## Builds and binaries

The user runs Windows; Windows binaries are the default requested binary deliverable.
Do not build or package Linux binaries unless asked. Preserve cross-platform source portability.
MSVC is the current user toolchain; supported GCC, MinGW-w64, and Clang configurations are also permitted.
No macOS setup or binary downloads are currently in scope. No Android application exists in this scope.

The new root CMake target builds only the selected UI foundations and Windows UI exhibit, not the old engine.
Six local checks passed in Debug and with sanitizers. Windows x64 cross-compilation succeeded.
Windows runtime/GPU execution and native MSVC verification remain outstanding.
Do not mix inherited build instructions with this selective target.
Local verification is automatic; GitHub Actions require explicit permission.

## Fonts

Run `python -m pip install -r Automation/FontRequirements.txt`, then `python Automation/PrepareFonts.py`.
This downloads four OFL-licensed families and generates native TOML and tooling JSON font catalogues.
Downloaded fonts remain ignored; the Windows package includes them and their licences.
The Control Centre exposes only interface appearance settings, not GI or renderer controls.

Primary rendering: Vulkan with Slang shaders. GLFW owns windows/input; the isolated ImGui exhibit may use DX11.
The Control Centre uses the reference trapezoidal notch and upward swipe-to-close drawer.

## Rendering validation checkpoints

- [Power MIS control](Exhibits/LightCompute/ReplayMISPower.md): supported plane improvement at equal query counts;
  room quality evidence remains uncertain. Validation baseline only, not a production default.
- [Power-MIS indirect reuse](Exhibits/LightCompute/ReplayPowerReuse.md): stronger-baseline reuse is rejected;
  the room MIS-off reference-mean gate also fails. Exact hit-sharing checks do not establish quality acceptance.
- Production allocation remains 348.046875 MiB output-only at 1080p. Packing and physical-GPU/editor acceptance
  remain deferred or outstanding; software Vulkan query counts are not frame-time measurements.
- [Room convergence diagnostic](Exhibits/LightCompute/ReplayRoomConvergence.md): independent CPU/GPU controls
  agree at larger fixed budgets; rare large NEE contributions support the short-run uncertainty explanation.
  The original failed acceptance remains preserved.
- [Temporal/spatial ablation](Exhibits/LightCompute/ReplayReuseAblation.md): neither reuse arm passes quality.
  Room temporal-only trades 20.29% fewer traversal queries for 31.55% higher RMSE, not a proven speedup.
- [Fresh D/I correlation and Fresh6 controls](Exhibits/LightCompute/ReplayCorrelation.md): correlation loss does
  not explain the room reuse penalty; fixed Whole6 has 12.12% lower RMSE and 5.77% fewer queries than temporal-only.
  Query/RMSE evidence only, not physical-GPU timing or a promoted renderer configuration.
- [Secondary-vertex correctness gate](Exhibits/LightCompute/SecondaryReconnection.md): native production-reservoir
  checks validate area/angular transport and expose visibility-support failures.
  Native-only checkpoint; see the Vulkan gate below.
- [Secondary-vertex analytic Vulkan gate](Exhibits/LightCompute/SecondaryReconnectionGpu.md): Slang readbacks pass
  reference/support controls and cross-build parity. Analytic geometry only; real-scene/persistent reuse pending.
- [Triangle-BVH reconnection gate](Exhibits/LightCompute/TriangleReconnection.md): real triangle hits and directional
  outgoing lighting pass reference/negative controls. Occluded query/error tradeoffs reject the literal implementation;
  persistent payloads and general-scene acceptance remain pending.
- [Persistent vertex/outgoing cache](Exhibits/LightCompute/VertexCache.md): paired estimates preserved while warm
  source queries drop to zero; lifecycle totals use 20.67–27.31% fewer BVH queries. Not GPU timing or new samples;
  see the bounded-refresh checkpoint below; general rendering acceptance remains outstanding.
- [Bounded cache refresh](Exhibits/LightCompute/CacheRefresh.md): fewer queries do not preserve temporal quality.
  Fixed period4/8 caches have higher trajectory-averaging error than fresh6/8 across all four restricted fixtures;
  candidate-retention/refresh redesign is needed before a production reuse policy.
- [Density-bearing candidate retention](Exhibits/LightCompute/CandidateBank.md): rotate4 improves query count and
  trajectory-averaging error versus restricted component fresh6 in all fixtures;fresh8/memory holdouts remain.
  Nonpersistent area8 improves quality in all fixtures without candidate history;no production promotion.
- [Receiver-anchored proposals](Exhibits/LightCompute/ReceiverBank.md): rotate4 closes the component fresh8 temporal
  gap on all four fixtures while retaining historical densities. A redundant-query finding in inherited controls
  required the numerical/work validation below;inherited-control counts are not optimized-speed evidence.
- [Exact-sample ray-budget gate](Exhibits/LightCompute/RayBudget.md):unconditional visibility-query elision is
  rejected at a reproducible float shadow boundary. Both runtime builds exit1;trajectory/performance comparisons
  of that shortcut are blocked. The separate confirmed-reuse variant below preserves those decisions.
- [Confirmed visibility reuse](Exhibits/LightCompute/VisibilityReuse.md): preserves reconstructed-ray decisions;
  corrected rotate4 wins against equally optimized fresh8 in query work and temporal quality. Fresh6 remains
  cheaper;no physical-GPU or production/memory promotion. The original blanket-elision safety gate still fails.

- [Two weighted reservoirs](Exhibits/LightCompute/WeightedPair.md):48B logical candidate retention passes
  correctness and reduces queries,but fails two temporal-quality gates. Useful tradeoff,not a promoted
  quality-neutral replacement;production348.046875MiB unchanged.

- [Camera-space fresh RIS](Exhibits/LightCompute/CameraAreaReservoir.md):normal-mapped RGB receiver images pass
  same-query raw4 error gates in3/3 layouts per proposal type. Release/host sanitizers and CPU replay pass;
  finite-reference near-wall uncertainty is explicit. Shading patch only,not full-scene/physical speed acceptance.
  Production348.046875MiB unchanged.

- [Fresh continuous-area RIS](Exhibits/LightCompute/AreaReservoir.md):PDF-correct two-proposal reservoirs beat
  same-query raw4 in9/9 cases per uniform/area family,with16.71–32.51% lower RMSE. Against same-proposal raw8,
  calls halve but error is slightly higher;no quality-neutral speed claim. Camera/physical GPU and production
  acceptance remain separate;348.046875MiB unchanged.

- [Continuous-area PDF/reconnection gate](Exhibits/LightCompute/AreaReconnection.md):fresh sampling now spans
  triangle interiors with area PDFs,joint direction conversion and diffuse receiver BRDF. Independent double
  rays/quadrature validate the integral. Uniform wins7/9 cases;area wins2/9 and has24.05% lower exploratory
  pooled RMSE. Neither is a universal winner.
  Equal query counts are not physical time;no temporal/production promotion.348.046875MiB unchanged.

- [Held-out expected-query-budget control](Exhibits/LightCompute/QueryCalibration.md):independent pilot/fresh
  count allocation matches stationary/smooth calls within0.30%/0.12%,but rotate2 rolling error is8.09%/4.48%
  worse. Jumping has8 infeasible zero-contribution steps;no artificial ray spending or production promotion.
  Next:continuous area/PDF support on the fresh traced path. Production348.046875MiB unchanged.

- [Lossless packet-local query elision](Exhibits/LightCompute/QueryElision.md):zero/duplicate-query removal
  preserves all RGB/descriptor bytes and reduces calls on every arm/track. Optimized proposal-budget raw is
  cheaper than rotate2 on the jumping track,with lower rolling error. Previous3/3 quality rejection remains;
  counts are not physical speed or exactly matched query budgets. Production348.046875MiB unchanged.

- [Traced RGB quartet integration](Exhibits/LightCompute/TracedQuartet.md):selection/persistence now use
  actual suffix and segment queries;CPU replay and build parity pass. Rotate2 uses57.02% fewer calls than raw8,
  but fails rolling-quality acceptance on all3 tracks. Controls are not query/time matched or fully optimized;
  no physical speedup or production promotion.348.046875MiB unchanged.

- [Ray-tested RGB suffix/reconnection](Exhibits/LightCompute/RgbTrace.md):direct Lambertian RGB uses actual
  shadow/segment queries;cached and inline results match bitwise and the independent CPU reference agrees.
  Omitted visibility fails921 records. No warm saving,multibounce GI or production promotion is claimed.

- [Persistent RGB quartet reuse](Exhibits/LightCompute/RgbReuse.md):retained descriptors and revision rebuilds
  pass;instant error improves8.08–12.33% against matched raw budget,but rolling error worsens15.32–20.18%.
  All3 quality promotions rejected;fewer candidate draws are not measured rays/speed. Production unchanged.

- [Integrated RGB quartet selection](Exhibits/LightCompute/RgbQuartet.md):actual GPU selection/descriptor
  write-read passes327,680 CPU comparisons/build and96 mean gates. Against same-eight raw RGB candidates,
  fresh RMSE is9.06–67.82% higher;correct compression is not quality promotion. Production remains unchanged.

- [RGB facet descriptor/support gate](Exhibits/LightCompute/FacetReservoir.md):32B shared descriptor,144B RGB
  quartet,24,576 byte roundtrips/build and768 finite RGB checks pass. This is not integrated RGB sampling or
  new GI quality;the previous80B format was scalar-only. Production348.046875MiB unchanged.

- [Four weighted reservoirs](Exhibits/LightCompute/WeightedQuartet.md):80B logical scalar history passes all five
  corrected-Fresh8 quality/query gates,including new .45 holdout. Smaller logical history trades some raw-Rotate4
  quality;no production allocation/timing/RGB promotion.348.046875MiB stays unchanged.
