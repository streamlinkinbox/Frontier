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

## Latest rendering continuation

- [ControlVehicle lighting renders](Exhibits/LightCompute/VehicleLighting.md):three fresh exhibit-only previews,
  including props,emissive gantry,spot/synthetic IES,clouds,sun and restrained lens flare. Existing ReSTIR analytic
  direct/reuse,separate emissive sampling;no production wiring. CPU Vulkan,384×240,not converged-quality evidence.

- [Maintained facet CDF shader ownership](Exhibits/LightCompute/FacetCdfShader.md):generic profile decoding,
  validity,area/CDF/PDF preparation and cached sampling now live in maintained shaders,with typed16B header.
  Preparation compiles without exhibit includes. All seven captures/124 rows retain exact prior results;
  16 CTests/20,535 checks per build,Release/host ASan+UBSan. Fixture scores/transport stay in the exhibit;
  production scene/output wiring remains. Production348.046875MiB/defaults/32B unchanged.

- [Renderer-facing CDF submission lifetime](Exhibits/LightCompute/FacetCdfSubmission.md):maintained caller-owned
  Vulkan encoding and fence-published cache residency,164 focused assertions,244 actual submissions/build.
  All seven captures/124 rows/six SPIR-V binaries retain exact prior results;15 CTests/20,506 checks,Release/
  host ASan+UBSan. Bounded interface,not full renderer/editor activation;348.046875MiB/defaults unchanged.

- [GPU-resident facet area/CDF preparation](Exhibits/LightCompute/FacetCdfGpu.md):116 images/118,784 packets,
  all80 slabs bit-identical to paired uncached GPU output. Actual area calls25,656,526->466;30 preparations,
  warm reuse/invalidation/rejection controls and14 CTests/20,342 checks per build,Release/host ASan+UBSan.
  SwiftShader CPU Vulkan;work reduction is not FPS. Production348.046875MiB/defaults/32B unchanged.

- [Runtime-sized GPU facet profiles](Exhibits/LightCompute/FacetProfileGpu.md):support sizes1/3/7/16/31/65,
  108 full images/110,592 packets and1,179,648 visibility decisions per build pass native parity. All6,144
  malformed-profile pixels reject exactly;13 CTests/20,334 checks,Release/host ASan+UBSan. Profile16 retains
  all80 prior GPU slabs byte-for-byte. SwiftShader CPU Vulkan,no quality/speed/default promotion;348MiB retained.

- [Batched GPU-generated full-frame parity](Exhibits/LightCompute/FacetImageGpu.md):all36 images/36,864 packets,
  245,760 candidates and393,216 visibility decisions/build pass the unchanged native gates. Release/host
  ASan+UBSan,12 CTests/build;packet RGB max delta3.7253e-9,RMSE4.5525e-11. Actual32×32 native/GPU/error images
  retained. SwiftShader CPU Vulkan,not hardware/quality/speed acceptance;348.046875MiB/defaults unchanged.

- [GPU-generated deformed causal packets](Exhibits/LightCompute/FacetPacketGpu.md):no native selected facet,
  U/V,PDF or normalization weight enters the shader. All108 four-lane packets,720 candidates,144 selections
  and1,152 visibility decisions/build pass native parity. All36 real mutations and36 shader faults checked;
  eleven CTests/build,Release/host ASan+UBSan. SwiftShader software device,not physical GPU. Actual packet/path
  RGB retained;historical failures and348.046875MiB/defaults unchanged. No new quality or speed claim.

- [Deformed selected-proposal Vulkan parity](Exhibits/LightCompute/FacetCausalGpu.md):36 real-geometry tests
  and36 actual GPU visibility-bypass faults,108 executions/build. SwiftShader CPU Vulkan matches native RGB
  and coordinates exactly;ten CTests/build,Release/host ASan+UBSan. Fixed native-selected proposals,not fresh
  GPU sampling/selection or physical-GPU acceptance. Actual readbacks retained;historical gate stays HELD.
  Engine/defaults and348.046875MiB unchanged.

- [Deformed transport causal diagnosis](Exhibits/LightCompute/FacetCausal.md):all blocked paths in the three
  insensitive historical controls have zero unoccluded RGB. Fresh replay matches all41 historical slabs.
  All36 real added-triangle tests pass and36 visibility-bypass faults are rejected;nine CTests/build,
  Release/host ASan+UBSan. Actual renders/contribution comparisons retained. Historical acceptance stays HELD;
  this is targeted coverage,not a quality win or default promotion. Production348.046875MiB unchanged.

- [Fresh RGB-norm score study](Exhibits/LightCompute/FacetNorm.md):eight independent four-packet cohorts,
  eight arms,twelve cases. Correctness passes;norm MSE improves3.39% versus mean RIS and9.37% versus raw4 on
  average,but regresses in two cases against each baseline:the frozen promotion gate FAILS. All1,152 mean
  gates and31,457,280 independent visibility checks/build pass;eight CTests/build,Release/host ASan+UBSan.
  Actual baseline/reference images and failed-gate evidence retained;engine/defaults and348.046875MiB unchanged.

- [Mixed-source native holdout](Exhibits/LightCompute/FacetMixed.md):unequal source areas/colours,receiver materials
  and partial blockers. All864 analytic RGB means and3,145,728 visibility checks/build pass;seven CTests/build.
  Uniform RIS beats raw4 in only6/12 cases;area RIS beats area raw4 in12/12 but loses to uniform RIS throughout.
  Actual API/reference images retained;no universal quality/default promotion. Production348.046875MiB unchanged.

- [Opt-in composed native packet estimator](Exhibits/LightCompute/FacetPacket.md):transactional source-profile
  intake and RIS/raw4/raw8 evaluation. All12 captures match the prior native caller bytewise;216 analytic means
  pass. Six CTests/build,Release/ASan+UBSan;6144 audited calls allocate zero times. Actual API images retained.
  No renderer/default promotion or new GPU dispatch. Production348.046875MiB unchanged.

- [Fresh sensitive Slang/Vulkan parity](Exhibits/LightCompute/FacetAreaOcclusionGpu.md):983,040 exact proposals
  and1,572,864 exact visibility decisions/build;216 analytic means pass. Max native/GPU image RGB delta1.46e-10.
  Six CTests/build,Release/host ASan+UBSan,actual readback images. SwiftShader software device,not hardware.
  Previous acceptance HELD;no renderer/default promotion. Production348.046875MiB unchanged.

- [Analytic native visibility coverage](Exhibits/LightCompute/FacetAreaOcclusion.md):three layouts ×four variants;
  1,572,864 visibility checks/build,216 quadrature means and both isolated omission controls pass. Five CTests
  per Release/ASan+UBSan build;actual native ablation images retained. Previous transport acceptance stays HELD.
  Native diagnostic only;no new GPU/renderer promotion. Production348.046875MiB/defaults unchanged.

- [Fresh native area transport—held acceptance](Exhibits/LightCompute/FacetAreaTransport.md):221,184 new primary
  hits,1,179,648 new paths and2,359,296 visibility checks match retained GPU/independent double rays. Actual
  native images and54 mean gates pass;four CTests/build. Omitted connection/suffix sensitivity passes only1/3
  and2/3 fixtures:the frozen gate fails,no promotion. Production348.046875MiB/defaults unchanged.

- [Opt-in C++ target/compensated geometry port](Exhibits/LightCompute/FacetAreaScorePort.md):589,824 new native
  targets match1,474,560 retained GPU observations exactly;294,912 composed selections have zero differences.
  Release/host ASan+UBSan,three CTests/build,16,084 focused checks/build,18 RGB replays and54 means pass.
  Native arithmetic is not fresh transport;primary/path captures remain GPU inputs. Production348.046875MiB unchanged.

- [Opt-in C++ area proposal/RNG port](Exhibits/LightCompute/FacetAreaProposalPort.md):589,824 newly generated
  native proposals match1,474,560 retained GPU observations with zero facet/barycentric/PDF differences.
  Release/host ASan+UBSan,two CTests/build,15,024 focused checks/build and distribution/negative controls pass.
  Actual native point plots retained;not fresh tracing or renderer activation. Production348.046875MiB unchanged.

- [Opt-in C++ facet-area pair operator](Exhibits/LightCompute/FacetAreaPort.md):294,912 retained GPU pairs,
  zero selection differences,18 RGB replay images and54 reference mean gates pass. Release/host ASan+UBSan
  agree bytewise;14,831 focused checks/build. This ports selection/normalization only,not geometry or tracing.
  Production348.046875MiB/defaults unchanged;full sampler and renderer integration remain open.

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

- [Geometric-receiver fresh RIS](Exhibits/LightCompute/GeometricAreaReservoir.md):shared-BVH primary hits and
  receiver shadows now pass CPU replay;both proposal types win3/3 same-query image gates. An endpoint defect
  is repaired. All queries including primaries:36/pixel RIS versus68 raw8,not physical timing. Production
  348.046875MiB unchanged;MIS/temporal/general-scene and production acceptance remain separate.

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
