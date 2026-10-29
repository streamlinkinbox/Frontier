# Project Tracker

Single progress and violation tracker. Detailed evidence belongs in Exhibits, not competing progress reports.
The [renderer and GI evidence map](../Exhibits/RecoveryAudit/RendererStatus.md) separates software tracing,
hardware RT, non-ray paths, reflections, PBR and the statistical limits of the recent charts.

## Ray-tested RGB suffix and visibility reconnection — 2026-10-29

- [Completed bounded ray gate and actual buffers](../Exhibits/LightCompute/RgbTrace.md):production FacetSurface/
  NearestFacet reconstruct points and trace a directional Lambertian RGB suffix plus receiver visibility.
  Independent double all-triangle Moller-Trumbore checks40,960 binary reference rays;20,480 valid/4,096 stale
  descriptors per build pass. Cached/inline connected RGB match bitwise;max normalized CPU error3.1493e-8.
- Release/host ASan+UBSan pass,five CTests each;exact parity:eight rows,six buffers,two SPIR-V modules;
  54 runtime hashes/build verify. Lit blocked reconnections expose921 visibility-omission failures. Nonzero
  stale RGB poison is rejected without queries/contribution. BVHs rebuild after blocker geometry changes.
- Cold cache and inline paths each use one suffix+one segment call/valid descriptor. No warm saving,physical
  timing,hardware RT,multibounce suffix,quartet integration or quality promotion is claimed. Prior persistent
  rolling-error regression/rejection remains unchanged;production348.046875MiB/defaults stay untouched.
- Bounded ray gate100%;overall ReSTIR/GI~52%,~48% remaining,planning estimates only. Next:wire ray-tested RGB
  and visibility into quartet selection/refresh with actual query-budget and fresh/raw comparisons. Correlation
  acceptance,arbitrary proposals,multibounce/dynamic/camera/TAA/general-scene quality,physical GPU measurement
  and production integration remain. Packing/editor/vehicle/Jolt deferred;no PR or Actions.
- Results retain six lossless buffers,compiled modules,source/readback hashes and generated comparison/audit;
  Images retains18 exact64x64 arrays and two charts. Initial unused-function build failure is retained;warnings
  and acceptance thresholds were not weakened. Windows/MSVC and physical GPUs are untested.


## Persistent RGB quartet reuse — 2026-10-29

- [Completed persistence experiment and rejected promotion](../Exhibits/LightCompute/RgbReuse.md):four RGB
  descriptors persist across32 dispatch pairs;warm steps retain two/refresh two,epoch changes rebuild all four.
  Release/host ASan+UBSan pass five CTests each and independent CPU validation;786,432 quartet descriptor
  comparisons/build,178,176 exact retained-byte checks,zero identity exceptions and1,152 RGB mean gates pass.
  Exact parity:578 semantic rows,96 captures,two SPIR-V files;146 runtime hashes/build verify.
- Three tracks in one eight-facet scene:stationary,smooth and receiver jumps,with relight/geometry epochs at
  steps16/24. Reset-inclusive matched raw budget uses the same143,360 draws/track as rotate2,versus262,144
  raw8. Instant RGB RMSE improves8.08–12.33% versus matched budget;four-step rolling error worsens15.32–20.18%.
  All3 frozen promotion gates fail. Logical geometry evaluations including birth scores are10.25% above raw8;
  fewer draws do not establish ray-query savings or speed. Keep the prototype isolated from production.
- Bounded experiment100% complete;overall ReSTIR/GI~49%,~51% remaining,planning estimates only. Next:traced
  RGB suffix/visibility reconnection with fresh/raw controls. Correlation-aware reuse must pass before promotion;
  arbitrary surface proposals,dynamic/camera/TAA/general-scene acceptance,physical GPU and production integration
  remain. Production348.046875MiB/defaults and previous results unchanged;packing/editor/vehicle/Jolt deferred.
- Results retain96 lossless buffers,compiled modules,metrics/paired block intervals,all mean gates and hashes;
  Images retain24 exact32x32 arrays and two labelled charts. No Windows/physical-GPU test,PR or Actions.


## Integrated RGB quartet selection — 2026-10-29

- [Complete gate,raw comparison and actual buffers](../Exhibits/LightCompute/RgbQuartet.md):two-dispatch Vulkan
  selection/descriptor write-read/reconstruction passes327,680 independent CPU comparisons per build,with no
  selected-identity disagreements or ambiguous cases. Max normalized estimate/weight errors1.486e-7/1.793e-7.
  Both Release/host ASan+UBSan exit0,five CTests each;38 semantic rows,20 captures and two SPIR-V files match;
  68 runtime hashes/build verify. Stale epoch,rebuild,all-null and one-valid packets pass.
- All96 correct/raw RGB mean gates pass;wrong zero-support/valid-count/current-h arms fail6/12/9 of16 cases.
  Against same-eight raw candidates,the144B logical quartet has9.06–67.82% higher fresh RGB RMSE on nonzero
  cases. Raw8 wins equal-draw accuracy;no quality/speed promotion. Logical raw RGB baseline is272B,not old
  scalar144B. Debug slabs are not a compact allocation;production348.046875MiB/defaults stay unchanged.
- Bounded selector phase100%;overall ReSTIR/GI~46%,~54% remaining,planning estimates only. Next:persistent
  RGB quartet reuse with reset and matched quality/query controls. Traced suffixes,visibility,arbitrary surface
  proposals,dynamic/camera/general-scene acceptance,physical GPU measurement and production integration remain.
  Packing/editor/vehicle/Jolt deferred. No PR or Actions;commit/push backup authorized.
- Retained initial failure documents computed-RGB exact-equality assertion corrected to the predeclared2e-5
  precision contract;no target,seed or mean threshold tuning. Protocol centroid wording was incorrect:actual
  tested points have fixed barycentrics(.2,.3). Neither correction is concealed as extra geometric coverage.


## RGB facet descriptor and positive-support prerequisite — 2026-10-29

- [Complete gate,baselines and actual buffers](../Exhibits/LightCompute/FacetReservoir.md):shared32B descriptor,
 144B logical RGB quartet. Eight noncoplanar facets/production interpolation;24,576 exact32B roundtrips per build,
 corruption/revision rejection and independent double reconstruction pass. Release/host-sanitized runs exit0,
 five CTests each;six captures/eight semantic rows match;54 hashes/build verify. Static typed-buffer stride32
 is separately inspected;actual runtime uses two float4 wire slots.
- Positive target floor1/64 preserves768 finite RGB channel/configuration checks,max error1.388e-17.
 Zero support,valid-count divisor,current-target denominator fail7/12/9 of16 receiver-pair controls.
 Native layouts in both builds:quartet144B,expanded quartet208B,raw RGB octet272B. Prior scalar80B is not RGB.
- This bounded prerequisite100% complete;overall ReSTIR/GI~43%,~57% remaining,planning estimates only.
 Next:integrated RGB GPU selection and equal-format comparisons;then traced suffixes,visibility/reconnection,
 dynamic/camera/general-scene acceptance and physical GPU/production integration. No RGB sampler/quality
 promotion yet;production348.046875MiB/defaults unchanged;packing/editor/vehicle/Jolt remain deferred.
- Evidence includes losslessly retained raw captures,12 exact readback PNG arrays,a montage,a measured layout/
 negative-control chart,finite results and hashes. Local Linux validation only;Windows/physical GPU not tested.
 No PR or Actions;commit/push backup authorized.


## Subpixel diffuse path-seed retracing — 2026-10-28

- Added a default-off, one-candidate mode which retains up to three unselected fresh seeds and retraces them at
  the current diffuse surface. It does not accumulate old radiance and cannot be combined with reservoir reuse
  or colour reconstruction. A half-pixel SwiftShader fixture accepts 970 banks, rejects all 22 changed-token
  disocclusions, and reproduces 10 changed shadow-path answers at the current point. Scene changes reset history.
- Independent one-bounce rectangle checks, seven corrupt-bank and seven invalid-request controls, five CTests,
  release/host-sanitized rows and strict-camera regressions pass. On four-bounce half-pixel motion, `813,056`
  CPU paths and 16 GPU seed batches give red RMSE `0.1530` for four fresh paths versus `0.1517` for four current
  paths including retraced seeds. The latter also uses prior frames; equal path count is **not equal time or total
  work**. Explicit-facet sampling and source-off/relight resets pass in a separate one-bounce check.
  [Contract, uncertainty and readbacks](../Exhibits/LightCompute/ReplaySampleHistory.md).
- ReSTIR GI, hardware RT, non-ray-traced GI, general animated reconstruction, unified PBR and editor integration
  remain incomplete. The editor plan and vehicle dynamics/Jolt remain deferred.

## Dynamic camera, blocker and multibounce replay — 2026-10-28

- A simultaneous camera/blocker move now has a four-bounce, four-candidate SwiftShader CPU-Vulkan validation
  with temporal and spatial reservoir replay active. Of 88 changed surface-token pixels, all reject the old
  temporal donor; the frame still replays 904 temporal histories and 3,968 spatial links elsewhere.
- Against 835,584 brute-force CPU paths over 816 plane points, 16 batches give red means `0.116587` CPU,
  `0.116829` fresh and `0.116991` reused. The first changed and reopened colour frames reset and match raw.
  Reuse incurs extra evaluations and prior frames; no equal-time speedup, general unbiasedness or arbitrary
  dynamic colour reconstruction is claimed. [Methods, uncertainty and results](../Exhibits/LightCompute/ReplayDynamicCamera.md).

## Four-bounce diffuse camera reference — 2026-10-28

- The strict camera-only mode now also has a four-bounce SwiftShader CPU-Vulkan check against 1,187,840
  independent brute-force CPU paths at 290 exact points. Across 16 seed batches, raw/reprojected red RMSE
  against the per-point CPU means is `0.2986/0.1012`; the latter uses eight prior frames and is not equal-time.
- A paired one-bounce control exposes 35 pixels with additional indirect contributions; a simultaneously moving
  blocker and camera and a reopening both reset colour to the corresponding raw output. These are bounded
  fixture checks, not general dynamic reconstruction or a proof of unbiasedness.
  [Reference, uncertainty and results](../Exhibits/LightCompute/ReplayCameraReprojection.md#four-bounce-reference-and-repeated-camera-transitions).

## Strict diffuse camera reprojection — 2026-10-28

- Added a default-off colour-history path for certified camera-only transitions in static, untextured diffuse
  replay. It retains only exactly matching world points, normals and material guides; rough/textured modes,
  dynamic geometry and lighting changes remain reset-only. The Engine domain now also snapshots BVH bounds and
  corners, and the recorder rejects incompatible flags before recording.
- In a `32 × 32` occluded-area fixture, 290 points retained colour and 734 were fresh; red RMSE against an
  independent numerical area integral changed `0.03290 → 0.01055` after eight earlier frames. A half-pixel
  shift accepted zero, exposing the deliberately narrow coverage. Revision, exact-guide, policy and invalid
  input controls pass, with release and host-sanitized SwiftShader CPU-Vulkan readbacks.
  [Contract, caveats and diagnostic images](../Exhibits/LightCompute/ReplayCameraReprojection.md).
- This is not full ReSTIR GI, arbitrary animated/disocclusion-safe reconstruction, hardware RT, a finished
  non-ray-traced GI path, physical-GPU validation, unified PBR or an editor viewport. Editor implementation and
  vehicle dynamics/Jolt remain deferred.

## Bounded replay initial candidates — 2026-10-28

- Engine replay supports one to four fresh path candidates, with the previous single-candidate behaviour as the
  default. Candidate changes cap prior reservoir history at one and reset accumulated colour in the exhibit.
- SwiftShader CPU Vulkan and host ASan/UBSan pass an independent half-blocked area integral, 800,000 CPU
  four-bounce room reference paths, host reservoir checks, moving-blocker and rough-textured camera controls.
  Four candidates halve per-output SD in the tested fixtures but require four fresh paths: no equal-time claim.
  Twenty default-mode images match the pre-change shader bit-for-bit; 14 separate mixture-on captures match prior
  pixels. Five release CTests and reconstruction regressions pass.
  [Contract and results](../Exhibits/LightCompute/ReplayCandidates.md).
- GI is not complete: dynamic colour reconstruction, broad material/visibility cases, non-ray-traced GI,
  hardware RT, physical-GPU validation and editor linkage remain open. Vehicle dynamics and Jolt remain deferred.
  The [scene inspection/editor controls plan](Plans/Ongoing/EditorSceneInspection.md) is saved but not implemented.

## Bounded replay colour reconstruction — 2026-10-28

- Added opt-in Vulkan gather/finalize stages for the existing combined diffuse and rough-opaque replay output.
  In a static 32×32 half-blocked emitter fixture, 20-frame bounded reconstruction reduced RGB RMSE from
  `0.3193` to `0.07184` (diffuse) and `0.3740` to `0.08401` (rough opaque). Host recurrence, eleven
  malformed-guide rejections, out-of-bounds/background, reservoir composition and first-change resets pass.
- At this checkpoint camera, blocker, material and lighting changes reset colour, even while replay reservoirs
  may keep cap-one histories. The later strict diffuse camera exception is described above. This is **not**
  general animated/disocclusion-safe reconstruction;
  no physical GPU, hardware RT, editor viewport, dedicated full reflection denoiser or unified PBR is claimed.
  [Algorithm, readbacks, tests and limitations](../Exhibits/LightCompute/ReplayReconstruction.md).

## Caller-owned ReSTIR replay recording — 2026-10-28

- Moved normal primary/generate/resolve Vulkan command ordering into `Frontier/Engine/Rendering`, with a shared
  128-byte push ABI, descriptor-range/dispatch/uint-offset guards and internal compute dependencies. The exhibit
  now consumes it; diagnostic-only probe/tail scheduling and all resource creation/submission remain exhibit-owned.
- Recorder mocks, five Engine/exhibit CTests, ASan/UBSan-host checks and SwiftShader CPU-Vulkan moving-scene
  readbacks pass. Fourteen release and sanitized PNGs match the pre-change scheduler byte-for-byte; camera, rigid
  motion and blocker regressions pass. This is scheduling parity in bounded fixtures, not a physical-GPU or
  performance result. [API, verification and limitations](../Exhibits/LightCompute/PathReplayEngine.md).
- ReSTIR GI, dedicated reflection reconstruction, unified PBR and the editor viewport are **not complete**;
  no hardware-RT path or non-ray-traced GI pipeline is established. Vehicle dynamics and Jolt remain deferred.

## Matched gas-pressure prototype — 2026-10-28

- Added a separate Engine Slang face-flux pressure primitive with matching divergence, gradient and Laplacian.
  SwiftShader tests through wgpu/Naga pass analytic long-/near-grid modes, an independent 56-cell least-squares
  solve, disconnected regions, buffer guards and release/host-sanitizer checks. The long-wave case still needs
  1,024 Jacobi sweeps to reach `1.25%` remaining divergence; no performance claim is justified.
- This closed/static-solid prototype is not compatible with the demo's cell-centred transport and is not integrated
  into it. The original plume still retains `98.1%` divergence in its near-grid regression. Open/moving boundaries,
  compatible advection, scheduling, scene/editor integration and physical-GPU validation remain open.
  [Contract and evidence](../Exhibits/FluidReference/GasProjection.md).

## Standalone GPU curved-shell tyre — 2026-10-28

- Ported the demo's 160-particle, 128-patch curved-shell/membrane deformation to Engine Slang. Direct
  SwiftShader Vulkan checks compare 42,240 snapshots against unchanged JavaScript within `0.000102 m`;
  the resident particle field also feeds 24,576 surface probes. Release and ASan/UBSan-host readbacks match.
- Independent host counterexamples show that one source patch's cross-force derivatives differ by `65.9%`
  and a `0.3 rad` rigid rotation changes the source hinge curvature by `−0.0130 m`. Source parity is not a
  conservative/objective-shell or real-tyre validation. No physical GPU, graphics/editor consumer or standalone
  spring-contact solver is claimed. [Implementation and counterexamples](../Exhibits/VehiclePhysics/TyreMembraneDevice.md).

## Standalone GPU Hex8 tyre — 2026-10-28

- Ported the demo's 128-cell solid and checked cell/result footprints to Engine Slang/C++; the existing Vulkan
  recorder supplies exact counts. Reset/advance and the resident surface projection execute in bounded
  SwiftShader tests; 42,240 particle snapshots match JavaScript within `0.000102 m` and 4,096 surface probes
  match CPU projection within `5.97e-8 m`. Release and ASan/UBSan-host readback hashes match.
- A separate 64-gradient GPU constitutive check matches numerical derivatives of the Neo-Hookean energy for
  positive, unclamped Jacobians; identity and the tested rigid rotation have zero stress. This does not
  validate clamped/inverted cells, full-wheel mechanics or measured tyres. No physical GPU, graphics mesh,
  application/editor integration or spring-contact dynamics is claimed; the later membrane entry above adds
  the separate GPU shell model. Vehicle/Jolt remain deferred.
  [Contract and numerical evidence](../Exhibits/VehiclePhysics/TyreSolidDevice.md).

## GPU tyre invocation bound — 2026-10-28

- Fixed a dispatch-boundary defect: workgroups have 32 lanes, so previously a count smaller than the bound
  buffer could update unintended tyres. The Engine recorder now pushes the exact count and both projection
  entries exclude padded lanes. One requested tyre with 33 bound leaves the other 32 byte-identical in reset
  and advance; release and ASan/UBSan-host checks pass on SwiftShader. The earlier trajectory digest is unchanged.
  [Updated pipeline contract and evidence](../Exhibits/VehiclePhysics/TyreProjectionDevice.md).

## Plume footprints and reference transport — 2026-10-28

- Engine owns validated 256-byte lattice and 48-byte source footprints; four unchanged demo JavaScript grade
  records round-trip exactly. Restored the original scene/distance shader dependencies. A 15-dispatch demo
  transport step and independent source, buoyancy, vorticity, moving-SDF and light checks pass on SwiftShader
  CPU Vulkan; release and ASan/UBSan-host readback digests match.
- The chained pressure fixture is zero-divergence only. The nonzero high-frequency pressure defect remains;
  simulation shaders and scheduling remain reference/exhibit-owned, not connected to the production editor.
  No physical GPU, long-term fluid validation or liquid solver is claimed.
  [Footprints, numerical checks and limitations](../Exhibits/FluidReference/PlumeTransport.md).

## Standalone GPU tyre projection — 2026-10-28

- Ported the demo's 160-particle XPBD-labelled projection to Engine Slang, with a checked float32 ABI and a
  caller-owned Vulkan command recorder. Direct SPIR-V execution on SwiftShader CPU Vulkan matches 12 JS/CPU
  cases over 33 independent tyres × 8 steps within `9.95e-5 m` local and `9.69e-5 m` world; valid/invalid
  commands, reset, buffer and dispatch guards pass. Release and ASan/UBSan-host readbacks match exactly.
- Reusable spring/continuum surface functions now read the GPU particle layout. In one direct Vulkan command
  buffer, the XPBD advance feeds 24,576 surface probes without CPU re-upload; maximum position errors versus
  CPU are `9.70e-5 m` spring and `5.97e-8 m` continuum. A vertex-stage entry compiles but was not rendered.
- This is a sequential-per-tyre, prescribed-target model and its force is not an integrated reaction. No
  physical GPU, performance, measured-tyre, editor or mesh-renderer integration is claimed. A later entry above
  adds separate GPU Hex8 and curved-shell fixtures; spring-contact and long-duration stability remain open.
  Vehicle/Jolt work remains explicitly deferred.
  [ABI, commands and evidence](../Exhibits/VehiclePhysics/TyreProjectionDevice.md).

## Rigid GI rare-path audit — 2026-10-28

- The three largest uniform-spatial samples in the 2,048-seed set replay to large path radiance before
  reservoir scaling; their final scale factors are below one. Eight post-selected uniform/mixture cases
  match their recorded output, and an independent host reservoir reconstruction agrees exactly in this
  fixture. Release, ASan/UBSan-host and default-off regression checks pass.
- A checked mirrored breakdown places the dominant contribution in all three high paths at bounce `2`,
  hitting the same bright emissive triangle. It shares shader helpers and does not independently verify
  path values, PDF/MIS or PRNG behavior. [Mechanism and logs](../Exhibits/LightCompute/ReplayRigidTailAudit.md).

## Rigid GI tail follow-up — 2026-10-28

- A further 2,048 disjoint frame seeds compare uniform/mixture proposals with spatial reuse off/on against
  2,000,000 separately seeded CPU paths. Release and ASan/UBSan-host runs pass and their sample tables
  are identical; the low rigid-red mean does not recur. Shader/driver code remains uninstrumented.
- The old versus new uniform means differ by `3.33` nominal combined SE; the red mixture/uniform SD ratio
  reverses from `1.468` to `0.587` as rare large samples dominate the new set. These finite-sample intervals
  and measured variance are tail-sensitive, not proof of bias or of a general speedup. The original
  discrepancy remains unexplained. [Evidence and failed wrapper audit](../Exhibits/LightCompute/ReplayRigidVarianceRepeat.md).

## Disjoint moving-GI convergence repeat — 2026-10-28

- Four disjoint seed groups (256 batches) compare fresh uniform/mixture and cap-one temporal/spatial reuse
  against six independent 240,000-path CPU references. Release and ASan/UBSan host runs pass the existing
  tolerances and produce byte-identical sample tables; restricted/full primary probes match bit-for-bit
  in a three-seed audit. Shader/driver code is not sanitizer-instrumented.
- The earlier rigid-green mixture/uniform SD ratio `1.515` varied from `0.716–1.480` across groups and is
  `1.125` overall; fresh rigid red is `3.84` nominal combined SE below the CPU reference under uniform
  sampling despite passing the six-SE tolerance. The follow-up above does not reproduce the low mean,
  but its cause remains unresolved; neither a general improvement nor convergence is claimed.
  [Data, chart and limits](../Exhibits/LightCompute/ReplayFacetMixtureMotionRepeat.md).

## Moving emissive-mixture GI reference — 2026-10-28

- Independent primary-ray and four-hit CPU-path checks now exercise the optional facet mixture through camera,
  rigid-texture/material, translated emissive-triangle, source-power, blocker and reopening changes. Both
  selected primary pixels pass all 30 RGB comparisons over 96 SwiftShader CPU Vulkan batches against six
  separately seeded 240,000-path CPU references. Cap-one transitions retain temporal and spatial reuse.
  Full release and ASan/UBSan host runs pass; shaders and the external driver are not sanitizer-instrumented.
- The mixture/uniform fresh batch-SD ratio is `0.671–0.799` across floor channels, but `1.515` in object green.
  These two-probe measurements do not establish universal variance improvement or general unbiasedness.
  Physical-GPU/editor integration and general deforming-motion coverage remain open. A 14-image SwiftShader
  gallery now captures the translated sources; release/sanitized RGB hashes match, and the default capture
  helper remains bit-identical for two submissions. A 20-image prior-shader regression also remains bit-identical
  across previous uniform/power and cosine/GGX modes. These are not production or hardware renders.
  [Fixture, uncertainties and results](../Exhibits/LightCompute/ReplayFacetMixtureMotion.md) ·
  [moving-light readbacks](../Exhibits/LightCompute/ReplayFacetMixtureMotionCaptures.md).

## Exhibit ownership audit and lighting extraction — 2026-10-27

- Reviewed all 27 top-level exhibit directories against both Engine trees and production UI ownership. Existing
  CPU atmosphere/cloud/fog, celestial/optics and demo XPBD/Hex8 code is already shared; shader/editor integration,
  the demo continuum shell and full gas remain incomplete. Vehicle dynamics and Jolt work are explicitly held.
  [Ownership evidence and scope](../Exhibits/RecoveryAudit/EngineOwnershipAudit.md).
- The previously exhibit-local area/luminance radiant-facet distribution and duplicate host validation now live in
  `Frontier/Engine/Rendering` and are consumed by LightCompute. On 4,096 facets the exact former builder and Engine
  output agree byte-for-byte; 18 guard cases pass in release and ASan/UBSan. Four actual SwiftShader fog-area cases
  and their analytic/empty/invalid controls pass. This is a global CDF, not clustered emissive selection or editor
  integration. [Reproduction and limits](../Exhibits/LightCompute/RadiantFacetDistribution.md).
- Shared cloud/fog float32 payload compilers and guards now live in Engine rather than the exhibit host. Previous
  builders match byte-for-byte across 64 cloud cases and the fog fixture; 37 guards pass release and ASan/UBSan.
  A small SwiftShader sky-only scene matches homogeneous cloud/fog closed forms (maximum `6.83e-7`); it does not
  cover shadows, cloud noise, physical GPUs or editor integration. The full-size fog run was stopped, not passed.
  [Medium payload evidence](../Exhibits/LightCompute/MediumRadiancePayload.md).
- Made the existing demo XPBD/Hex8 models and spring/continuum visual vertex projection independently consumable
  without vehicle/Jolt dependencies. Release and ASan/UBSan JS-reference checks pass; 22,528 sampled native
  projections are byte-identical to the prior Engine header and agree with the actual demo WGSL on SwiftShader
  within `0.097 mm` position / `5.73e-5` normal-component maxima. Spring contact dynamics remain vehicle-coupled,
  and hard-drop convergence and measured tyre calibration remain open.
  [Standalone tyre evidence](../Exhibits/VehiclePhysics/StandaloneTyreProjection.md).

## Dynamic path reuse continuation — 2026-10-27

- An opt-in 50/50 uniform/power emitter proposal uses matching selection probabilities for explicit sampling and
  BSDF-hit MIS. SwiftShader area, colour-coded one/three/five-source, empty-source and four-hit textured-room
  tests cover proposal, blocker and relighting transitions. In a bright-blocked fixture its per-sample SD is
  `1.708×` uniform versus `20.586×` for pure power; with both lights visible it is `0.597×` uniform. The ideal
  fixed-receiver second-moment bound is twice uniform, **not** a variance or adaptive-reservoir guarantee.
  Previous modes remain default and their readbacks match the prior shader. A separate 128-batch seed repeat
  investigates an unusually low 24-batch uniform result; the repeat approaches the independent area integral,
  but independence and monotonic convergence are unproven. Physical-GPU/editor integration, equal-time
  performance, general motion and general unbiasedness remain open. Four settled and three first-transition
  SwiftShader readbacks now show the mixture through camera, object, source and blocker changes; release and
  sanitized-host decoded RGB hashes agree. A two-frame default-capture check matches the previous submission
  arguments bit-for-bit; the prior full-gallery recapture was stopped, not passed.
  [Mixture evidence](../Exhibits/LightCompute/ReplayFacetMixture.md),
  [readbacks](../Exhibits/LightCompute/ReplayFacetMixtureCaptures.md).
- Optional area/luminance-weighted emissive-facet replay now uses the shared Engine CDF and matching MIS
  probabilities; proposal, source-power and blocker changes retrace capped history. Independent area and
  four-hit room references pass on SwiftShader, while default-off cosine/GGX readbacks remain bit-identical.
  Visibility-blind weighting reduced unblocked sample SD to `0.181×` uniform but increased it to `18.26×`
  when the bright emitter was blocked. A separate 256-batch rare-event repeat is recorded; neither it nor
  the fixed-receiver room establishes general unbiasedness, hardware performance or clustered emitters.
  [Weighted replay evidence](../Exhibits/LightCompute/ReplayFacetPower.md).
- An optional Engine-shader cosine/GGX mixture improves the measured rough-metal environment SD to `0.138×`
  cosine and the moving textured floor's blue batch SD to `0.309×`; a nearly matte environment and one object
  channel get worse. Independent quadrature, bounded four-hit CPU references and changed-proposal/moving-blocker
  reuse pass on SwiftShader. Default-off readbacks match the prior shader bit-for-bit. This is not complete
  ReSTIR GI, general unbiasedness, a universal speedup or hardware/editor integration.
  [Proposal, controls and limits](../Exhibits/LightCompute/ReplayImportance.md).
- The bounded replay footprints, BVH/material/receiver guards, emissive-index compiler and exact-input history
  policy now have a single authoritative home in `Frontier/Engine/Rendering`. The exhibit links the reusable Engine
  library; the exhibit-local implementation was removed. Independent C++20 domain and C++17 legacy-archive tests
  pass in release and ASan/UBSan. This is not an editor-integrated GI pipeline or a scalable revision system.
  [Engine ownership and verification](../Exhibits/LightCompute/PathReplayEngine.md).
- Guarded single-stratum rough GGX and albedo-texture replay can now be opted into together. SwiftShader release
  and sanitized hosts pass independent hemisphere quadrature, two textured primary-pixel references and four-hit
  atlas/blocker/reopening references with capped temporal/spatial reuse. Two rough-textured moving-primary probes
  also pass independent four-hit references after camera, rigid and material changes and blocker insertion. An
  initial fresh-blue difference of 3.65 combined SE did not recur in a four-times-larger disjoint-seed follow-up
  (0.68 SE); its GPU SE rose, so convergence and seed correlations still need study.
  General motion/materials, hardware GPU and editor integration remain open. Four settled and three first-transition
  renders are captured; previews blur noise and are not ghosting proof.
  [Rough-textured evidence](../Exhibits/LightCompute/RoughTextureReplay.md),
  [moving reference](../Exhibits/LightCompute/RoughTextureMotion.md),
  [render gallery](../Exhibits/LightCompute/RoughTextureReplayCaptures.md).
- Independent double-precision primary rays and four-hit CPU paths now check actual textured pixels during a
  combined camera shift, rigid cuboid rotation, atlas change and subsequent blocker insertion. Release and
  sanitized-host runs match within stated statistical bounds on SwiftShader; both probes retain one temporal
  candidate in every transition. Four settled and three first-transition textured-motion readbacks were captured
  and inspected. This strengthens fixture evidence, not production integration or a general unbiasedness claim.
  [Textured motion reference](../Exhibits/LightCompute/ReplayTextureMotion.md),
  [readbacks](../Exhibits/LightCompute/ReplayTextureMotionCaptures.md).
- An opt-in opaque Lambertian texture subset now evaluates the existing albedo pyramid at secondary hits. Atlas
  contents trigger seed retracing and a one-candidate history cap; unrelated pipelines retain their hard reset.
  Release and sanitized-host checks cover 33 invalid inputs, primary and secondary filtering, and two-hit relighting.
  Four-hit textured rooms pass independent CPU references and paired textured/untextured controls while retaining
  capped history across atlas and blocker changes. Four settled and three first-transition SwiftShader readbacks
  were generated and inspected. After correcting an ignored reserved optics component, all 14 refreshed raw and
  preview PNGs were byte-identical. [Texture replay audit](../Exhibits/LightCompute/ReplayTexture.md),
  [captures](../Exhibits/LightCompute/ReplayTextureCaptures.md).
- A finite-domain audit exposes missing-support bias when previously dark paths become visible. A positive
  selection floor recovers the exact toy mean when random-number-domain paths are replayed.
- An isolated SwiftShader Vulkan experiment now retraces temporal and spatial reservoir seeds in current opaque
  triangle scenes. Moving-occluder, source, receiver and bounce-budget fixtures match independent area integrals
  or bounded plane results through four hits; changed visibility is recomputed. An opt-in rough GGX/diffuse mode
  also passes hemisphere quadrature and a four-hit moving-blocker CPU comparison within statistical bounds. This
  is not the demo's geometric reconnection and does not integrate general materials or a production/editor frame.
  Nearby-position and normal guides retain history across a tested camera shift and authored rigid-object motion;
  this is not general deforming-mesh reprojection. [Motion audit](../Exhibits/LightCompute/ReplayMotion.md),
  [four camera/object/blocker captures](../Exhibits/LightCompute/ReplayMotionCaptures.md).
  Optional explicit emissive-facet sampling passes occlusion, source-change, rough and four-hit reference checks;
  a reproduced near-source light leak is fixed in a bounded open-segment test, subject to floating-point limits.
  [Endpoint audit](../Exhibits/LightCompute/ReplayEndpoint.md). Optional balanced facet/BSDF sampling passes
  four-hit references but helps a large-light variance fixture and worsens a small-light one; no timing claim.
  [Sampling comparison](../Exhibits/LightCompute/ReplayBalance.md),
  [six Vulkan comparison captures](../Exhibits/LightCompute/ReplayBalanceCaptures.md).
  [Facet sampling](../Exhibits/LightCompute/FacetReplay.md), [four new Vulkan captures](../Exhibits/LightCompute/FacetReplayCaptures.md).
  Four earlier noisy SwiftShader readbacks also show bounce and blocker changes. [Audit and scope](../Exhibits/LightCompute/PathReplay.md),
  [captures](../Exhibits/LightCompute/ReplayCaptures.md).

## Analytic spot and IES cells — 2026-10-27

- Non-overflow SwiftShader cell lists match exhaustive summation for a spot, a synthetic IES point and an
  IES spot, including moved sources, source disabling and opaque shadows. Capacity-one and outside-grid
  fallback controls also pass; native RGB discrepancy is at most `1.23e-5`. Release and sanitized hosts pass.
  [Fixture and scope](../Exhibits/LightCompute/ClusterSpotIes.md).
- This is analytic-light clustering only. Emissive triangles use a separate global distribution or bounded
  replay-facet sampling; clustered emissive selection, physical-GPU and editor acceptance remain open.

## Gas and plume source audit — 2026-10-27

- Retained the exact full VehicleHMI smoke/fire JS and five WGSL programs, including its distinct moving-grid
  helper. All 28 retained full-demo blob hashes match; eight Node reference and stencil checks pass.
- The unchanged reference pressure WGSL also runs on SwiftShader CPU Vulkan and matches independent CPU stencils,
  including ground/open/solid probes and shifted-window clearing. In one bounded 3D fixture a near-grid velocity
  mode retains `98.1%` of its centred divergence despite a small six-point pressure residual after 256 sweeps;
  this is not a complete animated-plume measurement. Smoke/fire remains unported to the native renderer; advection,
  full solver, physical GPU and liquid-fluid scope remain open.
  [Gas reference](../Exhibits/FluidReference/README.md),
  [actual pressure-shader results](../Exhibits/FluidReference/PlumePressure.md).

## Vulkan solar optics and GI audit — 2026-10-27

- Isolated SwiftShader readbacks now verify the existing analytic starburst: six blades make six spikes, five
  make ten, aperture rotation moves them, and opaque source cover removes them. Starburst, warm streak and
  blue anamorphic layers add together. Three inspected images and release/sanitized results are in the
  [starburst audit](../Exhibits/LightCompute/SolarStarburst.md). This is not the demo's exact burst, editor
  integration, physical-GPU validation or measured-lens calibration.
- Opt-in finite-pixel Sun and mixed warm/anamorphic flares execute in the LightCompute Vulkan validation
  host, with limb-weighted opaque triangle and geometric-horizon visibility. SwiftShader readbacks pass flux,
  Cartesian integration and partial-occlusion checks. The editor remains unconnected; no physical GPU,
  Windows, real-time, Moon masking, per-ray clouds/fog or measured-lens calibration is claimed.
- An on-axis sensor-FWHM conversion accepts supplied measurements; the checked sensor example is synthetic.
  NASA's mean solar diameter checks angular size only. A 640×360 synthetic scene capture shows actual Vulkan
  solar readback, not a physical GPU or editor image. [Detailed scope and results](../Exhibits/LightCompute/SolarOpticsAndReuse.md).
- Direct-light reservoirs now retain dark proposal domains, normalize by selected support and store pre-spatial
  history. Changed receiver/view/material domains conservatively reject temporal history and match cold in
  tested fixtures; this is not general dynamic reuse.
- A separate Vulkan diffuse-path experiment supports identical-domain pooling and multiple opaque Lambertian
  hits, with revision resets. A second, one-hit experiment carries a translated rectangle endpoint and shifts
  reservoirs between moving/different receiver positions with an area Jacobian. Independent area integrals
  and an omitted-Jacobian negative control validate that strictly unobstructed, common-support fixture only.
  Neither experiment is integrated into the editor or scene frame.
- Full ReSTIR GI, general dynamic endpoint carry, occlusion-aware spatial reconnection and general multibounce
  reuse remain unfinished. The retained-demo temporal equation omits the audited Jacobian; this is not WGSL
  execution or a completed GI fix.

## Reference daylight and lunar bounce continuation — 2026-10-27

- Opt-in CPU reference-renderer surface Sun now follows atmospheric extinction and planet/triangle visibility;
  the atmosphere LUT rebakes when parameters change. Day/night HDR backgrounds include the actual demo Moon
  texture and a local-direction Hipparcos catalogue. Daylight naturally reduces star contrast.
- Opt-in lunar surface illumination and a static one-bounce, same-receiver temporal SDF reservoir are wired to
  the reference renderer. This is **not** full ReSTIR GI: no GPU, dynamic reprojection, GI spatial shifts or
  multibounce. Indirect shadows see analytic SDF primitives rather than general triangle meshes.
- The new release and ASan/UBSan suite passes 37/37; 6,228 star directions differ from Astropy/ERFA by at most
  `0.007170°` on the selected 2026 cases. Vertical atmospheric optical depth, lunar flux and a lunar-lit wall
  also have scoped analytic checks. Existing VehicleReference CTests pass 7/7 and the recorded default-off
  binary64 probe remains `04da4f54d91705d0`. Details and limitations: [Daylight evidence](../Exhibits/NightSky/DaylightReference.md).
- No physical sky/lunar photometry, measured observations, editor/Vulkan or lunar texture licensing approval is
  claimed. This entry supersedes the older reference-renderer Moon integration status immediately below.

## VehicleHMI, Project Drive and finite-pixel Sun continuation — 2026-10-27

- Demo 160-particle XPBD and Hex8 solid deformation modes are ported with controlled JavaScript comparisons;
  spring carries chassis loads. The 720-step extended impact diverges after step 645. Synchronized one-step checks
  match on that path; same-source perturbations show strong impact sensitivity, but do not prove the sole cause.
  The hard-drop timestep sequence is not converged. Shell is unported.
- Project Drive at `3f14808` is imported separately: six units preprocess identically and source/import traces match.
  The visual-only demo-tyre bridge passes a mock host; no editor/Jolt integration or measured-vehicle validation.
- The CPU reference renderer now uses finite-pixel solar filtering and an opt-in mixed-flare layer, with full
  mesh/planet occlusion checks. Seven-camera flux error ≤ `0.000439`; normal and ASan/UBSan results pass.
- NightSky Moon position now includes the Meeus periodic series and topocentric parallax. Against JPL DE421,
  max Moon direction error `0.003118°` over 960 selected 2026 cases (old exhibit `2.912255°`); this is a model
  comparison, not measured sky calibration. At that checkpoint the texture was NightSky-only; the later CPU
  reference-renderer integration is recorded above.
- An opt-in static one-bounce SDF-emitter temporal reservoir passes analytic tests. The existing CPU direct-light
  spatial reuse was corrected for tested static discrete-light domains (the old sparse mean was `0.502607` vs
  exhaustive `0.352533`; corrected `0.355025 ± 0.002030`). The retained demo's separate spatial path shader gives an
  exact 1.25 mean for a fixture with expected 1.0; no general GI fix is claimed.
  [GI evidence and scope](../Exhibits/LightCompute/StaticIndirect.md).
- [Celestial evidence](../Exhibits/NightSky/README.md); [vehicle evidence](../Exhibits/VehiclePhysics/Continuation.md).
  No physical lens/sky/tyre calibration, complete GI/ReSTIR, GPU execution or Windows/editor claim. Partial solar
  clipping at the planet limb, general subpixel occlusion, editor/GPU Moon integration and Moon-map licence remain open.
- This entry supersedes the stale XPBD/solar status in the earlier 2026-10-27 entry below.

## Sun disc, lens-flare optics, measured Moon — 2026-10-26

- `LensFlareModel` radiance layer: limb-darkened sun disc (flux 1.00001 vs analytic), aperture
  ghosts (centroids ≤0.01 px), 6-blade starburst, anamorphic streak, halo, glow, veiling glare.
  Demonstration audit: it contains no flare code and no lunar texture — neither is claimed as a port.
- `CelestialModel` measured lunar albedo (`EngineContent/CelestialMaps`), Lommel–Seeliger disc,
  pole orientation + Meeus optical libration cross-checked vs pole geometry (0.29°).
- Atmosphere: demonstration-constant parity pinned by check; Mie extinction split corrected
  (4.40e-3/km total); multiple scattering sun-gated and calibrated (was ~190%, sun-independent).
- Evidence: [NightSky](../Exhibits/NightSky/README.md) 33/33 with rendered images;
  EnvironmentPipeline regression gate 73/73. CPU reference only — no GPU/Slang, Windows,
  cloud, moonlight-in-scene or parallax claim.

## Prior-screen exitance, rigid motion and unified moving vehicle — 2026-10-26

- Roughness-cone exitance pyramid, verified natively (max mip RGB error 6.7e-7) and on SwiftShader; pinned 3e8259c control resets cold on authored motion; guarded lighting refresh matches cold build.
- Rigid prism reuses 318 pixels under motion; unified six-pose fixture: 14k–15k opaque reuse per moving pose, zero caps/invalid, 23,040 source-off black pixels.
- DistanceFieldSpace is a certified clearance bound (0 violations); it does not accelerate traversal. Traversal-acceleration claim removed.
- Sharp-strip estimator bias 5.7% (noise 1.2%) recorded as a limitation. [Report](../Exhibits/LightCompute/PriorExitance.md).
- Still open: physical RTX/GTX, Windows/editor, production scene, full denoiser, moving general authored geometry without conservative reset.

## Derived finite-thickness vehicle glazing — 2026-10-26

- Three exact-edge-connected components from 74 retained window triangles become 284 closed triangles at
  nominal 6 mm thickness. Native saved IOR1.52 / RGB absorption [1,4,12] feed the existing volume atlas.
- Fresh restored-build strict and ASan/UBSan:52 assertions,16 malformed cases rejected, six-pose closure;
  native saved-material39 assertions and four CTests passed. Current 13 SPIR-V entries rebuilt.
- Fresh SwiftShader320x200 /16 samples:887 glass pixels,1851 absorption-changed channels, cap/unsupported0,
  exact repeat, moved/cold equality and64000 source-off black pixels. Pinned250616f control fails59/16.
- Ray-aligned shared-edge intersection correction; finite2048-step guided-TIR budget; diagnostic TIR saturation.
  Material-buffer/descriptor ABI unchanged. Actual captures and limitations: [VehicleGlazing](../Exhibits/LightCompute/VehicleGlazing.md).
- Full retained transport chain completed0:camera/mirror/rough/window/diffuse, pinned controls, four CTests,
  retained640x400 vehicle901probes(max4.5973e-6), reconstructionRMSE.0103498→.00585726 and six poses passed.
  No physical RTX/GTX, Windows/editor importer,
  glass denoiser, general CAD offset certification or unified moving sun/sky/cloud/fog completion claim.

## Saved smooth-volume material spectra — 2026-10-26

- Optional native TOML IOR/RGB absorption survives disk round trips and material compilation without changing
  the shader material layout. Actual Vulkan slab uploads use the compiled spectrum; no shader arithmetic changed.
- Strict and sanitizer native authoring checks, pinned previous-decoder rejection and actual saved-material
  Vulkan slab/repeat/source-removal/rejection evidence: [SavedDielectric](../Exhibits/LightCompute/SavedDielectric.md).
- Vehicle edge audit: 74 glass triangles, 68 unmatched exact-position edges; thin-sheet assignment retained.
  No vehicle closed-volume geometry, glass studio preview, editor controls, general renderer importer,
  physical GPU or Windows-editor completion claim.

## Pole shadows and authored-reflection history — 2026-10-25

- Reported previous-project half-shadow not reproduced: 72 directional/point/spot, receiver-view, caster-route and light-selection combinations; 3,538,944 analytic comparisons, zero mismatches, including 111,408 far-half shadow checks. Previous project/screenshot still needed; no speculative shadow fix.
- Angular-aware authored rough-reflection reconstruction; very smooth/mirror/glass routes preserve fresh directional detail. Independent finite-reference RMSE 0.246571 → 0.0927787 (62.4% lower); 88 native filter probes, max RGB error 1.19058e-6.
- Separately reproduced geometry-history invalidation defect: unchanged emitter colour/token after movement retained 1,521 pixels (RMSE 0.156373); packed-facet revision tracking rejects history and matches fresh scene exactly. Camera accepts 1,339 / rejects 17 rough pixels; 427 smooth pixels remain exact; source-off is black.
- SwiftShader/Linux, 13 compute entries, strict Release C++ and four native CTests passed. [Actual captures, repro and scope](../Exhibits/LightCompute/ShadowHistory.md). No physical GPU/Windows/full-editor, full specular denoiser or old shadow-bug fix claim.


## Sampled rough-lobe reflections — 2026-10-25

- Plastic now integrates the shared BRDF with cosine/GGX rays; accepted screen proposals verify the actual reflected-ray/facet intersection and clear segment, otherwise use BVH. No separate sharp-reflection budget.
- SwiftShader: 14,941 accepted screen rays / 204,392 fallbacks; same-seed BVH-only HDR matches exactly. Off-screen source contributes; source-off is black; moved source matches fresh submission; exact repeat.
- Actual pinned sharp/new rough captures and two-position source animation: [RoughReflections](../Exhibits/LightCompute/RoughReflections.md). Four uniform-light checks, 384 native/34 Vulkan energy cases and four native CTests passed.
- Finite 16 samples/proposal retain visible noise. No screen-colour mip history, temporal specular reconstruction, full sampling/PDF acceptance, speedup or physical RTX/GTX/Windows/viewport claim. Cellular and all archive assets retained.










## Lower cloud budgets and material reuse audit — 2026-10-25

- User requested faster iteration: working fog scenes now use cloud32/fog-shadow32, self-shadow6 and view32.
  Actual 160×100 comparison against cloud64/shadow64 at two winds retained in `CloudBudgetComparison.png`.
  RMSE 0.00207406 / 0.00187121; suitable for iteration by visual inspection, not final cloud appearance.
- Comparison and low-only execution passed 702 native fog and 354 sky-cloud probes; changed-default fog
  regression passed 654 camera + 270 surface probes, history/toggle/bounds checks; native CTests 4/4.
  Release only this turn. Half the outer sampling budget, not a measured frame-time or hardware speed-up.
- `RunCloudPreview.sh` renders low-only by default; `COMPARE=1` opts into reference renders. Older high-budget
  fixtures remain explicit verification tools. See `Exhibits/LightCompute/README.md` for reproduction/scope.
- Audited user material source at upstream be1d0a8e45d39738f37ccf711522a745e326e149: 399/399 material archive
  files, 6/6 material ContentInterchange files and 47/50 Render files identical. Three vector-gradient files
  differ (176-byte upstream versus 112-byte local instruction layout); no partial import performed.
- Reuse existing material library; glints/flakes/glitter are the requested rewrite exception. Existing research
  and isolated software comparison found and reviewed; not a production winner or physical-GPU speed claim.
  Details: `Exhibits/TextureStudio/MaterialReuse.md`, `Results/MaterialReuse.json`. No glint rewrite implemented yet.

## Emissive-volume single scattering — 2026-10-25

- Shared Slang fog now gathers emissive triangle radiance with area/luminance CDF sampling, solid-angle
  conversion, HG phase, triangle visibility and analytic source-to-volume extinction. Both analytic lighting
  and emissive surface lighting remain. Optional `Fog[15]` budget 0–64; default 0 preserves prior presets.
- Original ControlVehicle/ground plus a disclosed 2×2 m calibration panel: four actual 160×100 Vulkan frames,
  468 native camera probes, maximum RGB discrepancy 1.59192e-5. Combined capture restores original analytic
  sources. `Exhibits/LightCompute/Images/FogFacetCombined.png` and `FogFacetComparison.png` show actual readbacks.
- ASan/UBSan checks add 448 native camera probes; independent 131,072-sample native rectangle integration
  matches analytic solid angle within 0.0446%. Cold repeat, surface isolation, zero density, empty CDF,
  occlusion and invalid budgets/CDF checked. External-driver leaks excluded; no global leak-free claim.
- Reproduction: `RunFogFacets.sh`; measured parameters and scope: exhibit README and `Results/FogFacets.json`.
  Still single scattering and finite/noisy sampling, not temporal fog, multiple scattering, final sky/cloud
  appearance or physical-GPU/Windows/real-time acceptance. Sun/flare/stars preserved; Moon planned.

## Fog shadow quadrature refinement — 2026-10-25

- Controlled 16/32/64/128 cloud-shadow budgets at fixed 32 view midpoints, two wind positions and original
  ControlVehicle; eight actual 160×100 Vulkan captures, 936 matching-budget native probes.
- 64 samples reduced full-image linear RGB RMSE versus finite 128-step quadrature by 96.57% / 96.39%.
  Changed the exhibit preset from 16 to 64; this costs 4× shadow samples, with no speed or convergence claim.
- `Exhibits/LightCompute/Images/FogSamplingComparison.png`: columns16/64/128, rows wind0/120.
  Full measurements, independent comparisons and serial reproduction: exhibit README and `Results/FogSampling.json`.
- Fresh ASan/UBSan: promoted preset 654 camera + 270 surface probes, additional budget study 1,040 probes;
  zero-density, occlusion, history, toggles and bounds passed. Four native CTests passed; external-driver leaks
  excluded. Subset sensitivity now uses shadow64→128, not a self-comparison.
- Corrected historical day-medium prose to the actual source: 0.0015/m, 450 m, not 0.0007/m, 650 m.
  Old 320×200 captures and numeric logs remain unchanged and clearly archived at 16 shadow steps.
- Regular view marching, temporal fog, multiple scattering, final cloud appearance, Moon and physical-device /
  Windows / production viewport acceptance are not completed by this refinement.

## Cloud-shadowed fog and light shafts — 2026-10-25

- Clarified provenance: inherited four-octave cloud density was ported and its rendering integration improved;
  it was not replaced by a new cloud-shape model. Earlier cloud/ground shadows did not establish air light shafts.
- Added new native/GPU height-fog single scattering, exact camera/light optical depth, HG phase, cloud visibility
  at each volume sample and triangle occlusion. Original point/IES/spot profiles illuminate the fog. Analytic
  arrivals and sampled emitter-to-surface segments receive fog extinction; both lighting paths remain.
- Actual 320×200 captures: `Exhibits/LightCompute/Images/FogSkyComparison.png` (cloud shadow off/on in fog),
  `FogLocalComparison.png` (local-light fog off/on), `FogComparison.png` (original ControlVehicle), plus wind120.
  The sky fixture uses an explicit thinner 800–1100 m broken-cloud slab to expose soft shafts, not a hidden effect.
- 654 numeric camera probes, 390 capture camera probes and 270 native surface-arrival probes. Daylight numeric
  RGB maximum discrepancy 1.14637e-5; local-light 1.04968e-5; capture maximum 5.48751e-5; surface maximum 5.21472e-5.
  Uniform Beer/ambient, near-horizontal stability, bitwise zero density, geometry occlusion, bounded steps,
  dry-history isolation and enable/disable cold equality passed. Focused checks passed ASan/UBSan.
- Sampling is not converged: on 12 numeric rays, view32→128 changed red radiance by up to 0.00311955 and
  shadow16→64 by up to 0.0293056. Remaining: alias/banding, temporal fog, multiple scattering, emissive-volume
  scattering, full inter-bounce/coupled-media transport and final cloud shapes/appearance.
- Thirteen entries, eight SSBOs, 128-byte pushes; no new output slabs or Int64 shader feature. SwiftShader CPU
  type 4 only, not physical-GPU/Windows/real-time acceptance. Existing native Sun/flare/stars remain; Moon planned.
- Reproduction, exact checks and scope: `Exhibits/LightCompute/README.md`, `RunFog.sh`, `Results/Fog.json`.

## Spatial cloud-shadow checkpoint — 2026-10-25

- Matching directional sunlight now integrates cloud extinction at each primary receiver and secondary hit,
  not at one observer position. Exact, reservoir-target and spatial-reuse evaluations use current-position T;
  proposal mass stays unattenuated with support retained. Both analytic and emissive lighting remain.
- Five cases × 433 native surface probes (2,165): maximum RGB discrepancy 8.87267e-6. Secondary transport:
  117 native probes, maximum 7.40833e-8. Each single-Sun case exercises 16,000 temporal and 63,438 spatial
  reuses; maximum exact/reused RGB discrepancy 9.53674e-7. Non-Solar analytic lighting is bitwise unchanged.
- Wind revision matches cold reconstruction. Reproduced an additional cloud-disable stale-history defect,
  then tracked out-of-prefix cloud selection: disable/re-enable now match cold direct/indirect output bitwise.
- Fixed omitted ambient RGB initializers from the previous cloud upload. The strengthened uniform check
  requires nonzero ambient. All 2,230 cube and 396 camera probes and corrected volume captures passed ASan/UBSan;
  maximum composite cube RGB error 8.32318e-6 and camera RGB error 8.63495e-6.
- Four actual 640×400 wind times (0/60/120/180 s), original ControlVehicle and original ground, are retained in
  `Exhibits/LightCompute/Images/CloudShadowVehicle.png`, `CloudShadowGround.png` and `CloudShadowComparison.png`.
  Cameras and vehicle are stationary; wind moves. The overhead direct-only view isolates spatial shadows.
- Final spatial/toggle ASan/UBSan and fresh-sampling checks passed. Legacy Vulkan, indirect reconstruction and
  native CTest 4/4 passed; their exact execution chronology is retained in `Results/CloudShadowRegression.txt`.
  Concurrent full-size software runs hit allocation errors; serial volume checks and resumed final view passed.
- Still twelve entries/eight SSBOs/128-byte pushes; no added slabs or Int64 shader feature. SwiftShader CPU
  type 4, not physical GPU, Windows or real-time acceptance. No new Moon/Sun/flare/star presentation integration.
  Remaining: flat-slab horizon artifacts, approximate ambient, indirect cube seams and finite-Sun penumbrae.
- Detailed evidence and reproduction: `Exhibits/LightCompute/README.md`, `RunClouds.sh`, `Results/CloudShadows.json`.

## Vulkan cloud environment checkpoint — 2026-10-25

- Added GPU cloud density, 64-step per-camera-ray integration, six-step Sun self-shadowing and a cloud radiance
  cube for indirect rays. Native 64-bit noise hash is emulated with paired 32-bit arithmetic; cloud/primary/
  transport SPIR-V requires only Shader capability, not Int64. CPU atmosphere precomputation remains.
- Added optional native/GPU uniform ambient cloud radiance (default zero preserves the earlier model). Sun at
  ground/cloud altitude uses atmospheric transmittance; one observer-to-Sun cloud factor shades local receivers,
  not a spatial cloud-shadow map. Cloud parameters and loop extents are checked before dispatch.
- 2,230 native cube probes: maximum T error 1.186e-5, RGB error 1.06147e-5. 396 direct camera-ray probes: maximum
  RGB error 9.02517e-6. Uniform Beer-Lambert, ambient closed form, clear-path bitwise equality, wind and bounded
  march checks pass. Full cloud cases passed ASan/UBSan; native CTest 4/4, recovered/native sanitizer checks,
  clustered ControlVehicle, legacy Vulkan and indirect-reconstruction regressions also pass.
- Actual clear/cloud/wind/3° low-Sun readbacks: `Exhibits/LightCompute/Images/CloudWeather.png`, `CloudSkyWeather.png`
  and `CloudComparison.png`. Fixture Sun angles/irradiance and camera are explicit, not observer/time ephemeris.
- Twelve compute entries, eight SSBOs, push ABI 128 B; cloud cache adds 221,200 B. Device remains SwiftShader CPU
  type 4; no physical-GPU/real-time/Windows claim. Existing native Sun/flare/stars and both light paths remain.
- Remaining cloud appearance: flat-slab/regular-step horizon structure, approximate ambient, indirect cube seams
  and spatial shadowing. Moon, GPU atmosphere and night/catalogue work remain in `Documentation/EnvironmentTodo.md`.

## Vulkan indirect reconstruction checkpoint — 2026-10-25

- Recovered saved branch from GitHub checkpoint `0a4560c2` after sandbox reset; restored pinned ignored tools.
- Added reprojection, current-neighborhood RGB clamp, capped temporal mean and surface-compatible 5×5 spatial
  filtering of indirect only. Current visible emission and sky misses stay separate; direct histories are unchanged.
- Isolated emission at 160×100: independent 16-seed reference, eight updates; RGB RMSE 0.0103498 → 0.0058573,
  **43.41% lower**. This is not a full-resolution moving-quality or equal-cost speed result. Independent native
  spatial kernel: 210 probes, maximum RGB discrepancy 4.8961e-8.
- Six full-resolution moving vehicle/camera poses exercise reuse and the pose-3 light reset. Emission revision and
  camera cut match cold reconstruction bitwise; all sources off is black. Visible emission is preserved; extracted
  direct output differs by at most 3.8147e-6 from final float arithmetic.
- Actual readbacks: `Exhibits/LightCompute/Images/TransportFiltered.png` and
  `TransportReconstructionComparison.png`. Legacy exact/cluster/reservoir/static-history, native primary and
  emissive-estimator checks passed; focused ASan/UBSan numeric/spatial/reset checks passed.
- Eleven compute entries, eight SSBOs, push constants 128 B; 21 float4 history/output slabs (86.016 MB at 640×400).
  SwiftShader CPU type 4 only. Clamping/smoothing are biased; moving emitter/shadow changes are not exhaustively
  revalidated. Physical RTX/GTX/Windows, glossy transport, GPU sky/clouds and Moon rendering remain open.
- Evidence/reproduction: `Exhibits/LightCompute/README.md` and `RunTransportReconstruction.sh`. Historical missing
  private scratch digests are explicitly labelled after recovery, not presented as current-byte verification.

## Vulkan emissive-area sampling checkpoint — 2026-10-24

- GitHub preservation verified before work: local/remote `3dfbc2d3` matched and the working tree was clean.
- Explicit area/luminance sampling over 1,130 emissive triangles replaces hemisphere-hit emission only. Analytic
  point/IES/spot/directional secondary shading and sky misses remain on the hemisphere route; visible emission
  is unchanged. Both lighting paths remain; no emissive clustering or authored proxy-energy accounting claimed.
- 225 native probes, 16 area draws: mean RGB error 7.2128e-9, max 1.36771e-6. Against independent 8,192-draw
  native reference probes, emissive RMSE 0.206886 → 0.00906289 (**95.62% lower**). This is not equal-cost speedup,
  a full-image quality statistic, or universal unbiasedness evidence.
- Six moving poses preserve current indirect bitwise versus fresh evaluation. Geometry-CDF rebuilds do not
  invalidate direct-light history; the changed analytic light at pose 3 still does. Probability ticks preserve
  dim emitter support; invalid CDF entries are rejected; empty-emitter and disabled-environment cases pass.
- Actual readbacks: `Exhibits/LightCompute/Images/EmissiveComparison.png` and `EmissiveMotion.png`. Coarse displaced
  bright footprints are substantially removed; residual grain remains. Indirect filtering is not implemented.
- Legacy exact/cluster/reservoir/history, native primary/cosine transport and new ASan/UBSan checks passed.
  Device remains SwiftShader CPU type 4. RTX/GTX/Windows acceptance, glossy transport and GPU environment are open;
  native Sun/stars/flare remain unchanged and Moon remains planned.
- Detailed source/device/sampling scope and reproduction: `Exhibits/LightCompute/README.md`, `RunEmissive.sh`.

## Vulkan one-bounce diffuse transport checkpoint — 2026-10-24

- `TraceSurfaceTransport` traces full-BVH secondary rays from Vulkan-generated camera surfaces. Secondary hits
  evaluate material emission and shadowed analytic point/IES/spot/directional lighting; misses sample a shared
  native-atmosphere cube. The primary response is diffuse; direct GGX lighting remains intact.
- 3,603,424 secondary rays; 901 independent double-reference receiver probes give mean RGB error 2.08237e-8,
  maximum 4.6271e-6. Analytic-only, emission-only and environment-only routes all contribute; isolated sum agrees
  within 8.44442e-6. All sources off is exactly black. Proxy-versus-emissive physical accounting remains open.
- Six moving frames retain current indirect/emission bitwise versus fresh evaluation, including the light-change
  reset. Actual readbacks: `Exhibits/LightCompute/Images/TransportMotion.png` and `TransportComparison.png`.
- Complete static/moving direct/primary regression and focused ASan/UBSan transport execution passed. Device is
  SwiftShader CPU type 4. Eight compute entries, eight SSBOs, 128-byte push constants; no GPU/Windows speed claim.
- Atmosphere is CPU-precomputed into six 48×48 float RGB cube faces, not CPU per-pixel lighting and not a GPU
  atmosphere/cloud port. Native Sun, flare and stars remain unchanged; Moon remains planned.
- Fixed 16-direction diffuse quadrature visibly creates coarse illumination footprints. Indirect denoising,
  emissive importance sampling, glossy/multi-bounce transport, environment appearance and physical RTX/GTX/Windows
  integration remain open. Numeric agreement with the finite quadrature is not a convergence/quality proof.
- Detailed reproduction, checks and source/device limits: `Exhibits/LightCompute/README.md` and transport reports.

## Moving Vulkan-primary history checkpoint — 2026-10-24

- Six authentic vehicle/camera poses now generate surfaces and rigid-motion metadata on Vulkan with CPU raster
  disabled and all receiver channels poisoned. Persistent temporal/spatial reservoirs and reconstruction execute
  over those generated receivers. Actual animation: `Exhibits/LightCompute/Images/PrimaryMotion.png`.
- Moving RMSE sum 0.651907 → 0.286237, **56.09% lower**; fixture quality, not equal-cost speed or GPU performance.
  Native motion projection agrees within 0.00012207 px. Uploaded per-pose rotation coefficients fix a measured
  shader trigonometry discrepancy; no CPU per-pixel visibility or motion buffer is supplied.
- Native ray/shading checks at all six poses; zero hit/miss disagreement. Eleven triangle identities differ at
  edges or within float-depth tolerance. Worst all-pixel RGB outlier is 0.960438; this remains a recorded
  robustness limitation. Matched-facet and all-pixel errors are separately reported, not conflated.
- Camera-cut versus cold restart, light-off invalidation and current facet-emission refresh pass bitwise checks.
  Full retained history, clustered lighting and primary clipping/fallback regressions pass.
- Device is still SwiftShader CPU type 4. This is direct plus visible material emission against black, not GPU
  indirect/emissive-source transport, sun/sky/cloud/Moon integration, physical RTX or Windows acceptance.
- Detailed evidence and reproduction: `Exhibits/LightCompute/README.md`, `Results/PrimaryMotion.txt` and JSON.

## Vulkan camera-ray visibility checkpoint — 2026-10-24

- Executed `TracePrimarySurfaces` on the authentic ControlVehicle, with native raster scene setup/render skipped
  and all 36 receiver floats poisoned before dispatch. Geometry, camera, material and light export remains CPU work.
- Independent double-precision rays: 225,214 hits / 30,786 misses, zero hit/miss or first-triangle disagreement;
  maximum relative depth error 5.00174e-6. Native barycentrics, BRDF, imported IES profiles and full-BVH shadows
  give mean RGB error 2.03235e-6, maximum 6.93947e-5. All-light and clustered output is identical.
- Near/far, nearest overlapping surfaces, all-miss and 256 forced-stack fallback checks passed. Exact shared-edge
  ties in the synthetic fixture are classified individually; nearest depths and shaded values remain unchanged.
- `Exhibits/LightCompute/Images/Primary.png` is the actual Vulkan readback, not an illustration. Device remains
  SwiftShader CPU type 4; no physical GPU, RTX query, Windows or production viewport proof is claimed.
- Seven shader entries / eight SSBOs / 128-byte push constants; private exhibit archive FLC3. Complete existing
  static/moving history regression and native sanitizer/CTest suite rerun successfully after the shared ABI change.
- This checkpoint is direct plus material emission against a black environment. Both analytic point/IES/spot and
  emissive lighting remain. GPU indirect transport, environment integration and moving GPU-primary history remain
  next integration gates. Preserve existing sun/lens flare/stars; Moon remains in `Documentation/EnvironmentTodo.md`.
- Reproduction, scope and measured results: `Exhibits/LightCompute/README.md`, `Results/Primary.txt` and manifest.

## EngineUpdate status

| ID  | Finding or task                        | Status      | Next action                            |
|-----|----------------------------------------|-------------|----------------------------------------|
| 001 | Checkout differs from source baseline  | Partial     | UI imported; rest deliberately absent. |
| 002 | Root agent rules used obsolete workflow| Implemented | Keep entry instructions consistent.    |
| 003 | Prototypes live in Documentation       | Open        | Inventory paths and launcher links.    |
| 004 | Inherited Internal tree remains        | Open        | Audit ownership before further ports.  |
| 005 | Old maps/backlog conflict              | Partial     | Historical pointers added; reconcile.  |
| 006 | Windows binary and GPU checks         | Partial     | Cross-built; Windows runtime pending.  |
| 007 | Four skills retained at old paths      | Intentional | Preserve until approved relocation.    |
| 008 | UI foundations and SVGs ported         | CPU checked | Windows integration still unverified.  |
| 009 | Six ImGui patches                     | Checked     | Applied, verified, reverted/reapplied. |
| 010 | Shared activation lifecycle           | CPU checked | Integrate into further scene classes.  |
| 011 | Scene renderer                        | Excluded    | Blank new Render directory by request.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |
| 014 | Control Centre and quick panel        | CPU checked | Windows interaction validation next.  |
| 015 | Open fonts and catalogue              | Checked     | Four families, 30 faces, OFL retained. |
| 016 | Appearance persistence                | Checked     | Reload and invalid-save checks passed.|

## Verification

`Exhibits/ControlCentre/README.md` contains current commands, results, evidence, and limitations.
All six local tests passed in Debug and with address/undefined-behaviour sanitizers.
All 151 SVG symbols remain ready. Native Control Centre images are CPU raster output, not Windows GPU captures.
Windows x64 cross-compilation succeeded using Zig 0.13.0 / Clang 18.1.6 obtained through PyPI.
The unsigned Windows executable is packaged with fonts, SVGs, licences, and integrity metadata under ignored Binaries.
Native MSVC and Windows runtime/GPU execution are still unavailable. No Linux product binary is delivered.
No GitHub Actions are authorised.

## Known rule findings

The port preserves existing APIs for this bounded batch; it is not a clean naming/formatting certification.
Examples requiring repair: `VectorGlyphRecord`, short local names, comment vocabulary,
namespace brace layout, header banners, and non-approved decorative glyphs in inherited source comments.
The earlier official Windows example is now GLFW-hosted; its original display-adapter provenance remains explicit.
The four mandatory skill documents have not been weakened or edited to waive these findings.

## Scope history

The earlier documentation-only validation script was removed when that phase was clarified.
Subsequent user instructions authorise source porting, local validation code, proof images, and commit/push.
No full baseline replacement or inherited C++ relocation has occurred.

The selected theme/control configuration APIs now use `Configure...` rather than the banned `Assign...` verbs.
The new appearance files use Allman braces, flat namespace indentation, and descriptive file headers.

## Drawer and content correction

- Replaced the floating settings window with the reference top-edge trapezoidal drawer and inherited spring motion.
- Actual upward mouse swipe closes the sheet; added flick, axis lock, resize, focus-loss, and scaled-geometry tests.
- Renamed ContentDirectory and moved shared content into EngineContent/Icons and EngineContent/FontArchives.
- Native discovery reads metadata from TOML; Unicode file loading and corrupt-face rejection are exercised.
- GLFW 3.4 is pinned under ExternalPackages and used by the Windows host. toml++ remains the C++ format dependency.
- CLAUDE.md records Vulkan/Slang primacy, GLFW window ownership, content conventions, and TOML runtime formats.
- Windows runtime/GPU checks remain pending; no Actions are authorised.

#### Native Float32 vector surface map recovery (2026-10-19)

- Pattern scope remains approximately 68%; Stamp and full Pattern parity remain deferred/open.
- Added `VectorSurfaceCompute` with a typed `rgba32f` Vulkan storage image; the runtime derives format from SPIR-V,
  requires storage/sampling/linear-filter/transfer capabilities, and refuses unsupported devices rather than downgrading.
  Existing vector coverage and selection remain RGBA8.
- Compiles ordered layer/channel contributions from editable source, including visibility and layer masks. Separate
  RGBA32F maps cover colour, height, normal, roughness, metalness, opacity and occlusion. Contribution alpha is a
  separate mask; disabling a channel produces a neutral map and zero contribution. Layer-specific golden-thread
  metalness remains isolated from fabric metalness.
- Shared PFM encoding preserves IEEE Float32 bits, emits little-endian bottom-first `PF`/`Pf`, and does not clamp,
  gamma-transform or renormalize samples. The ZIP contains 14 actual GPU-readback fixture files, 32x32 each; it is
  not a document export. `Exhibits/TextureStudio/NativeSurfaceEvidence.html` presents the actual capture and GIF library.
- Seven real Vulkan channel dispatch/readback assertions pass strict and ASan/UBSan builds on SwiftShader. A cache-path
  regression now verifies 512x512 retired-slot submission and reuse of equivalent programs; it caught and fixed a reset
  that discarded the selected surface program before dispatch. The regression verifies authored gold/fabric separation,
  default neutral normals, finite Float32 samples, masks,
  PFM byte layout and strict/sanitized byte identity. Shader compiles with Slang WASM 2026.19 and reflects 24-byte
  surface push constants plus `Rgba32f` image storage.
- Native Texture Workspace now exposes the Pattern editor with persistent TOML, Vulkan coverage/selection, and a distinct
  dockspace. `PatternWorkspaceValidation` passes 606 checks on SwiftShader; its capture/result artifact is linked in the
  native evidence HTML. This runner did not execute a Windows desktop session.
- Float32-to-PBR bridge progress: 95%. `VectorSurfaceMaterialBridge` compiles five channels from a Pattern document,
  records their Float32 dispatches and image-to-host readbacks in one caller-owned submission, and exposes maps only after the
  caller's completion serial retires. No queue wait is inserted. Equivalent documents preserve the map cache; changed channel
  programs reconfigure and regenerate it. Five pyramids upload as RGBA32F authoring textures to the existing PBR shader.
- The native Texture Workspace host now routes its 512x512 Pattern document through that bridge on the shared Vulkan
  submission. Retired maps replace the selected material's live-preview channels; the material view shows the result without
  persisting source changes. The Windows x64 host cross-links, and packaging verifies all 13 surface/vector SPIR-V entries;
  native Windows GPU execution/capture remains a release gate.
- `MaterialRasterizationValidation` passes 224 checks on SwiftShader using 512x512 Float32 authoring maps and 128x128 PBR
  evidence captures. It exercises a single-submit five-map transaction, rejects premature retrieval, verifies
  unchanged/changed document caching, uploads actual GPU output, and compares the PBR centre capture with an independent
  scattering reference. The integrated bridge tests separate fabric and gold-thread layers: changing fabric Roughness to
  0.61 leaves gold Roughness at 0.18 and gold Metalness at 1.0 in the Float32 maps; reducing fabric SurfaceRelief from
  0.8 to 0.35 changes its derived tangent normals and PBR capture while keeping the gold-thread normal response unchanged.
  The three-frame C++ Vulkan GIF and provenance were regenerated from this 512x512-map run and remain embedded in
  `Exhibits/TextureStudio/NativeSurfaceEvidence.html`.
- `MaterialAtlasValidation` confirms changed Float32 authoring maps invalidate and repaint the same PBR preview selection,
  and that stale preview readiness is hidden until the replacement submission retires.
- Native Pattern channel enable toggles and strength controls are active for selected shapes; multi-selection applies each
  edit to every selected shape, and the document history preserves channel toggle Undo/Redo. Strength accepts 0–1 for
  ordinary channels and 0–4 for Normal; stitch/weave Height and Normal strengths cannot be reduced to zero. `PatternWorkspaceValidation`
  passes 626 checks, including selected-shape roughness-strength and surface-relief edits that leave the unselected layer unchanged
  and reach compiled GPU contributions. `VectorSurfaceValidation` confirms a GPU roughness disable removes only the fabric contribution
  while preserving the independent golden-thread roughness response.
- Native layers now persist a `surface_structure` classification (`freeform`, `stitch`, or `weave`). Selecting stitch/weave
  in the Pattern inspector enables and locks both Height and Normal channel toggles; document validation rejects missing,
  disabled, or zero-strength required contributions. TOML round-trip and invalid-weave rejection are covered in the native
  workspace validation, which passes on SwiftShader. This is explicit layer metadata and enforcement; automatic stitch/weave
  geometry generation and Windows interactive capture remain outside this phase.
- Native TOML now maps the web textile keys directly: `stitch_role` values `holes`, `thread`, and `highlight` resolve to
  NeedleEntry, StitchThread, and UpperThreadPass; `weave_role = "yarn"` resolves to WeaveYarn. The Pattern inspector can
  author these classifications, and validation enforces their parent stitch/weave structure and required channels. TOML
  compatibility exercises all four mappings; full Pattern JSON geometry import is still open.
- `VectorSurfaceValidation` runs the weave-classified fabric through all seven actual RGBA32F Vulkan channel dispatches,
  rejects disabled Height before compilation, verifies a per-layer Roughness strength of 0.23, and scales fabric relief by
  0.5 in GPU readback while preserving separate gold-thread height and normal response. `PatternWorkspaceValidation` confirms the inspector edit reaches the compiled
  contribution program. Both SwiftShader CTests pass; native Windows interactive preview capture remains open.
- Native evidence gallery now includes a three-frame C++ Vulkan PBR GIF: baseline, actual GPU-authored Float32 fabric/gold
  maps, and revised fabric relief/roughness with the gold response isolated. The linked JSON records source, executable and frame hashes; the fixture is SwiftShader, not a
  Windows desktop session.
- Still open: execute and capture the interactive Pattern 1:1-to-material-preview workflow on the native Windows host,
  including a per-shape channel edit; convert web Pattern JSON geometry and construction into native vector instructions;
  calibrate SurfaceRelief against measured height units and profile performance.
  Stamp remains deferred. The persistent/scene-level Apply action stays disabled until integrated native GPU submission and
  capture pass.
  GIF display is 8-bit evidence only; numerical Float32 checks are authoritative. No pixel stream is used.
- Reproduce: install pinned UI dependencies via `python Automation/PrepareUI.py`; compile the surface shader with
  `node Automation/CompileVectorSurface.mjs <Slang-WASM-JS> <shader-directory>`; run
  `python Exhibits/TextureStudio/Code/VerifyVectorSurface.py --driver <Vulkan-driver> --shader <VectorSurfaceCompute.spv> --destination <scratch>`.

#### Session rebuild on `arena/db2f9953-frontier` (2026-10-20)

- Restored pinned UI deps + fonts in a fresh sandbox, configured with `-DFRONTIER_HOST_VERIFICATION=ON`,
  built all host targets (GCC 12.2, Debug) and ran the full CPU suite: **21/21 CTests passed** (~70 s).
- Repaired four stale validation harnesses (no production-code changes): font inventory 4/30 → 5/35
  (`ControlCentreValidation`), per-domain icon rasterization (`InterfaceValidation`, Texture/Fluid symbols are
  denied to the Editor domain by design), canvas hover before preview F/arrow keys plus a non-vacuous F check
  (`MaterialPreviewConfigurationValidation`), and the full `###`-suffixed dockspace window name plus a null guard
  (`DockingValidation`, previously a null-`ImGuiWindow` SEGV). Docking also passes under ASan/UBSan.
- New `Exhibits/index.html` hub embeds the recorded HTML/native captures, per-workspace port status vs the Alloy
  reference (`Experimental/TextureStudio`), and this rebuild log; served from `Exhibits/` for preview.
- Still blocked in sandbox: SwiftShader/Vulkan readbacks (no `slangc`/driver), Windows link/run, Alloy `npm`
  verification. Carried UI defects (Project Browser 1:1, zoom FPS, layer cards, toggles, dropdown reuse,
  slider/ramp/gradient editors, rotation math, drag latency) remain queued after this rebuild.


## Windows preview 39483329514 (2026-10-20, branch arena/db2f9953-frontier)
- Commits: ba9b105 (settle-gated Pattern Float32 bridge, preview clip rects, dark-slate default stroke) + 42c9e54 (CI trigger for this branch).
- CI run 39483329514: Windows + Publish jobs success; Release unsigned preview at frontier-windows-39483329514.
- Linux host verification on ba9b105: full ctest 21/21 green; VectorSelectionValidation rotation/resize 1431/1431 pass (CPU).
- Confirmed open shader gap: PreviewPbrSurface samples albedo only; height/normal have no bindings (next round).

## Windows preview round 2 (branch arena/db2f9953-frontier, commits ba9b105..2fd6ea4)
- ba9b105: settle-gated Pattern Float32 bridge (drag perf), preview clip rects, dark-slate default stroke.
- 6b13015: parked material drafts across selection, full-bleed cover-fit preview + title scrim, exposure card, auto-size layer cards.
- 2fd6ea4: draped cloth preview shape, six-seamless-preset Pattern gallery, redesigned close dialogs.
- Linux host verification: full ctest 22/22 green (incl. 638 pattern-preset checks, cloth fold assertions).

## Windows preview round 3 (branch arena/db2f9953-frontier, commit e8755ee)
- Gesture-gated Pattern bridge (no regen while a gesture/pen stroke is in flight).
- Single-axis orbit rewrite (yaw about world-up, sway-tested old-fail/new-pass).
- Styled tooltip cards everywhere in Material; Open Library dialog restyled.
- TOML-backed preset gallery (six shipped .vector.toml files, built-in fallback).
- Apply-to-material gating with persistent snapshot per material.
- Linux host verification: full ctest 22/22 green (669 preset checks).

## Windows preview round 4, batch 1 (branch arena/db2f9953-frontier, unreleased)
- Pattern gallery preset thumbnails: CPU-drawn ImDrawList vector previews replaying each preset's
  instructions with VectorCoverage.slang corner/rotation conventions; click-to-open + hover tooltip.
- Project Browser Details rail: full projectspace metadata (identity/template/format/version, content
  roots, level/catalogue/pin counts, startup level, workspace theme/accent/font/scale) + Open button.
- Editor activation: opening a project launches EditorWorkspace (proven WindowsUI pattern ported into
  WindowsFrontier) with a top bar (back, project name/stats, Control centre / Theme / Settings buttons
  driving the ControlCentre drawer), a Viewport summary tab, and return-to-browser teardown.
- Materials: procedural detail textures (Automation/GenerateSurfaceDetailTextures.py) wired into five
  presets — tour/practice golf-ball dimple normal+ORM (TextureMask 6, 4x repeat), three solar-module
  cell/busbar albedo+ORM+normal sets (TextureMask 7, 2x repeat, shared ORM/normal).
- Correction to the round-2 note: the material preview already renders through MaterialSurface with full
  texture bindings (normal/height included) whenever a material residence exists; PreviewPbrSurface is
  only the albedo-only fallback. Golf/solar relief was a data gap, not a shader gap — no shader port needed.
- Height remains pattern-authored (AuthoringMaps[2] via Apply-to-material); .material files have no height slot.
- Linux host verification: full ctest 22/22 green (Docking close-all updated for the Viewport tab;
  MaterialLibraryValidation now compiles all five texture-wired presets incl. PNG decode).
- Still open: rotation-axis mapping vs user expectation, asset-browser HTML 1:1 (HTML file not in repo),
  remaining surface-detail ports (orange-peel, scratches, weaves need shader/UBO targets), zoom-FPS check.

## Windows preview round 4, batch 2: orbit fix (branch arena/db2f9953-frontier, unreleased)
- User report: yaw spins fine, but vertical drags roll "as if on Z" and the pivot feels off-centre.
- Root cause: orbit composed yaw-after-pitch (Ry*Rx), so pitch increments rotated about the yawed local
  X axis (a view-axis roll at 90 yaw) and yaw leaned the object up-axis sideways by sin(yaw)*sin(pitch).
- Fix: LookAt-style orbit (Blender/three.js convention) — yaw about screen-vertical world-up through the
  pivot, pitch about the fixed screen-right axis. Pure-yaw and pure-pitch poses are bit-identical to
  before, so the approved feels are untouched; only combined yaw+pitch changes.
- Correction to the round-3 sway test: zero-ellipse turntable yaw is incompatible with a screen-vertical
  object up-axis (proven: fixed front height + vertical up forces no yaw). Replaced with three
  old-fail/new-pass assertions: up-axis never leans, pitched tips never roll, pitched yaw traces the
  standard screen ellipse; plus a pivot-exactness guard across all five shapes.
- Linux host verification: full ctest 22/22 green; old matrix fails the new lean/roll/ellipse assertions.

## Windows preview round 4, batch 3: Alloy preset parity (branch arena/db2f9953-frontier, unreleased)
- User rejection: the six native-invented presets (and their gallery) did not match the Alloy Studio
  HTML. "Alloy Studio" is the web TextureStudio itself (schema alloy.pattern.v1); source of truth is
  Experimental/TextureStudio/src (PatternEditor.jsx gallery, patternDocument.js catalog of 103 families).
- Converter upgrades (Automation/ConvertWebPattern.mjs): per-copy instructions stay lean when they fit
  the 128-instruction budget, else tile-wrapped copies concatenate into one tile-space contour range
  (identical union under the same fill rule); minimal adjacent same-paint merging rescues the 65-layer
  Graduated lattice; half-drop/single-axis tiling accepted as documented straight-XY equivalents (baked
  tile pixel-identical, neighbour arrangement differs); miter/bevel joins and butt/square caps substitute
  round; anisotropic strokes use the larger axis; non-sized flowers import. All 178 family x palette
  variants convert (was 143 OK / 35 fail); deviations attach to each conversion for the sidecar.
- GPU/host budget raise: cubic segments 4,000 -> 32,768 (128 KB -> 1 MB storage buffer; shader needs no
  change since segments are an indexed buffer), vector source 600 KB -> 4 MB (codec, gallery loader,
  workspace save/load, import validation). Instruction cap stays 128 (shader stack array). Catalog peak
  is 18,954 segments (weaves); 25,600 is the editing-API maximum, so the 32,768 boundary is pinned by a
  struct-level coverage-validation test instead of the join fixture.
- Batch export (Automation/ExportPatternLibrary.mjs): 103 families / 178 palette TOMLs (37 MB) +
  Library.toml sidecar (families, 8 collection groups, 4 palettes, per-palette files, 24 deviation
  notes) under EngineContent/PatternArchives/TextureStudio/starters/. Serialized floats round to 7
  decimals (float32-parse within 1 ulp, invisible). The six invented TOMLs are deleted; the C++ fallback
  builders remain only for a missing preset directory.
- Gallery ported 1:1 from PatternEditor.jsx: kicker, two collection shortcuts, collection dropdown,
  search (name/group/description/study), conditional print-palette dropdown, heritage note, result
  count, two-column cards (76px cover-fit previews, wrapped names, description tooltips, selected
  ring), 24/page paging, trailing notes. Decoding is lazy per page (96-entry FIFO cache). Two
  documented deviations: DM Sans has no arrow glyphs, so paging/shortcut arrows render as ASCII
  < >; "Generate a layout" generator section is deferred (self-contained follow-up, not presets).
- Card tessellation: paint-order layer walk resolving Over/boolean composition; cubics flatten,
  RDP-simplify (closed-loop farthest-point split) and ear-clip to triangles once per preset; strokes
  replay as single polylines; fills submit batched with off-thumb culling. Fixed two real bugs found
  by headless harnesses: RDP collapsed closed loops (degenerate chord), and 16-bit ImDrawIdx overflowed
  past 64K verts per window draw list — 32-bit indices are now a PUBLIC compile definition on
  FrontierImGui (tracked; the vendored imconfig.h is untouched because CI recovers imgui fresh).
- Validation: PatternLibraryPresetValidation is Library.toml-driven (pins 103/178, decodes and
  coverage-validates every file, 1954 checks); new PatternLibraryGalleryValidation pins collection
  counts (30/16/10/10/10/10/10/7) and search counts, tessellates all 178 variants into finite geometry
  (830K triangles); headless card-render harness draws all 103 cards with draw-list invariants intact.
- Linux host verification: full ctest 23/23 green; converter node --test 7/7; ASan/UBSan clean on
  decode, tessellation and card rendering.
- Still open: rotation-axis follow-up already shipped in batch 2; asset-browser HTML 1:1 (HTML file
  still not in repo); "Generate a layout" section; remaining surface-detail ports; zoom-FPS check.

## Slate layer-stack port (2026-10-21, Slate arena/4006465a-slate cb8dcf0 onto db2f9953@34537e1)

- HTML first: `Experimental/TextureStudio` gains the Alloy material workspace (MaterialPanel PBR layer
  stack with per-channel sources, MaterialSpecification authoring document, FieldSpecification coded
  generators, FieldProjection Float32 atlas, editable camouflage palettes); catalogue 127 -> 131.
  Verified by production build and 7/7 Node model checks; Playwright specs ported but not run
  (browser download blocked in the sandbox).
- C++ port: coded fields in MaterialProceduralBaking (double-precision JS parity, 8 new ops on
  Albedo/Normal, palette text params), `Engine/ContentInterchange/MaterialAuthoring` document model with
  strict normalize, `[Authoring]` TOML round trip in SurfaceMaterial, pool text edits
  (ConfigureProceduralText), panel palette colour editors, deterministic catalogue export of the four
  camouflage presets with coded Albedo channels. Evidence: `Exhibits/TextureStudio/Results/LayerStackPort/`.
- Linux host verification: full ctest 26/26 green (new MaterialAuthoringValidation pins JS goldens,
  normalize rejection, link/designer projection, TOML stability, catalogue camouflage bakes).
- Boundaries still open: coded fields bake no ORM channel; woodland fabric weave relief is not yet
  composited under the camouflage field; Float32 field atlas stays HTML-only (native path is the CPU
  baker); panel renders strata rather than the full HTML authoring sheet; Windows runtime unverified.

## Inherited tree reclamation (2026-10-22, branch arena/1b8b280c-frontier)

- Reclaimed the inherited engine code trees from this branch (`Internal/`, `Executables/`,
  `Applications/`, `memory/`, `PaintingSurface.bat`; 736 files). Complete trees remain on `master`
  and in history; nothing in the current build referenced them (verified by configuration census
  and a full host build + test run).
- Retained the inherited design documents under `Documentation/Plans/References/InheritedEngine/`
  (170 files: `Prototypes/`, `Research/`, root-level plan/reference notes, and the notes that lived
  inside the reclaimed trees). `Documentation/FolderStructure.md` records the review outcome.

## Orbit staging and LED radiance correction (2026-10-22, branch arena/158c63c8-frontier)

- Specimen-group pitch (0.18 rad, baked into ball+pedestal) removed: pedestal lathe is level
  (rim height spread .34 -> ring-exact), Pivot/Radius re-measured. Preview key light, sky up and
  studio softboxes are world-locked through the orbit (identity at the default pose).
- LED radiance sampled through the first stratum's UV transform instead of raw coordinates.
- MaterialPreviewConfigurationValidation pins ring level + rigid light/up carry; shaders compile
  via the pinned Slang toolchain; host suite 26/26.

## Environment archives and layered inspector (2026-10-22, branch arena/7aa23edd-frontier)

- Baked five neutral equirect studio environments (`EnvironmentBake`) into
  `EngineContent/EnvironmentArchives/`; the shader samples them as a world-frame texture array
  (set 0 binding 3), replacing the tinted analytic presets and the analytic softbox integral.
  Key light + strength are derived per archive at scan time; equirect PNG drops import by rescan.
- Viewport: environment dropdown is now a card with CPU sphere previews, Exposure and Light
  intensity; the standalone exposure card is removed.
- Inspector: recipe controls categorized per stratum under LAYER headers (groups preserved as
  cards), uncovered stratum scalars editable per layer (incl. Thickness), profile-less generic
  editors on all layers. Host suite 26/26 (645,546 preview-config checks incl. card gestures);
  Windows GPU confirmation pending the user's hardware test.
- Still deferred: sand op binding, thin-film, glass depth, camouflage/ballistic underlays.

## Environment-switch freeze correction (2026-10-22, branch arena/71dc0cca-frontier)

- Windows test log: `PreviewConfiguration[2]` (now the environment layer) was still pinned to
  zero by `ValidateMaterialScattering`, so every non-default environment threw inside
  `ConfigurePreview`/`ConfigureSelection` and the viewport froze on its last good frame.
  Contract now bounds the layer slot; pinned by CPU checks (645,550).
- Light intensity missed the smoothed-camera discrete copy (applied only on settle snap); fixed.
- Environment sampling at mip 2 (firefly stipple), strength target 0.3 -> 0.2, key weight
  0.4 -> 0.32 (tonemap flooding), catalogue cap 24 -> 8 (pyramid layer contract). Suite 26/26.

## Fuzz/sheen BRDF and inspector card layout (2026-10-22, branch arena/b36a0fa5-frontier)

- Fabrics (velvet, wool, denim, cotton, silk, satin, …) and the leather/paper/foliage recipes that
  already carried `sheen`/`sheenRoughness`/`sheenColor` sliders were missing the grazing-angle rim
  lobe the HTML reference renders; native output looked flat. Added a Charlie-distribution +
  Ashikhmin-visibility sheen term (glTF `KHR_materials_sheen`-style), identical math in CPU
  `ScatteringSequence.h` and GPU `ScatteringEvaluation.slang`, additive on the opaque stratum only.
- ABI: `ScatteringStratum` gained `Sheen`/`SheenFinish` float4s (64 -> 96 bytes); cascaded through
  `PreviewScattering` (672 -> 928) and `MaterialScattering` (1504 -> 1760), every offset re-asserted
  in `SurfaceMaterial.h` and the GPU-reflection parity guard in `Automation/CompileSurfaceShaders.mjs`
  (this guard caught a stale hardcoded size before it could ship, the exact failure mode PR #7's
  dormant-contract freeze taught us to watch for).
- `SheenColor`/`SheenIntensity`/`SheenRoughness` are now live TOML keys, wired end to end: the colour
  swatch and the recipe sliders both write through `MaterialLibrary::ConfigureStratumColour` /
  `ConfigureStratumCoefficient`. The "Sheen colour" swatch is data-driven — any recipe profile that
  exposes a `sheen` control gets it, not a hardcoded per-material list.
  37 shipped `.material` files seeded with their HTML-authored default sheen values (fabrics, leather,
  paper, foliage) so the lobe is visible without hand-tuning. `MaterialLibraryValidation` gained a
  coefficient/colour/compile round-trip check for the new fields.
- Inspector cards: added an 8*Scale inset so cards no longer sit flush against the panel edge and
  scrollbar; fixed the per-card row count (profiled Finish cards no longer pad in 3 phantom rows,
  unprofiled Finish cards now count their real 6 generic sliders) so a card's background no longer
  under/overshoots its own controls; every card (not just the first per layer) now carries a small
  "LAYER N" tag so grouping stays legible while scrolling past several cards in the same layer.
  Host suite 26/26.
- Deferred, unchanged: sand op binding, thin-film, glass transmission depth, camouflage/ballistic
  coded-field underlays.

## Save-dialog modal chrome (2026-10-22, branch arena/8c806870-frontier)

- The "Unsaved material", "Open material library" and "Material destination" (create/duplicate)
  dialogs in TextureWorkspace, plus Pattern's "Unsaved pattern" close prompt, inherited the docked
  workspace's zeroed `WindowRounding`/`WindowPadding` and rendered as flush, square-cornered boxes
  with text crowding the edges.
- Added a local `MaterialModalScope`/`PatternModalScope` RAII helper per panel (mirrors the existing
  `MaterialTooltipScope` and the Editor's `ConfirmationDialogue` convention already used elsewhere)
  that scopes 20x18 padding, 10px rounding and a faint 1px border to just these popups, restored
  before the surrounding workspace's zeroed style resumes. No layout/behaviour change otherwise.
  Host suite 26/26.

## Fork merge: c7egoist/Frontier arena/ec6ca01d-frontier (2026-10-23, branch arena/c8d99312-frontier)

- Merged `c7egoist/Frontier@arena/ec6ca01d-frontier` as-is, same handling as the earlier
  `c7egoist`/other-session fork merge (PR #8): its unique commits beyond the shared `5a8c1b8`
  ancestor are a standalone-engine GGX material response port and an atmospheric-environment MIS
  integration, plus the `Exhibits/CompleteRenderer`, `EnvironmentPipeline`, `IlluminationChain` and
  `IndirectIllumination` reference exhibits and the `Frontier/Engine/Rendering/*` portable renderer
  tree they build on. Each exhibit carries its own standalone `CMakeLists.txt` and is not wired into
  the root build (same as the prior fork's `Frontier/`, `VisualProof/`, `Docs/` overlay) — merge was
  clean with no conflicts, since this branch's commits since the shared ancestor don't touch the
  files this session's sheen/inspector/dialog work changed.
- Verified: full root build + `ctest` 26/26, confirming the merge is inert for the shipped build.

## Pattern layer stack visual parity + selection-bounds regression coverage (2026-10-23, branch arena/649fea90-frontier)

- HTML Pattern Studio's layer rows (`PatternEditor.jsx`) now reuse the Texture workspace's
  `tp-layer-row` chrome verbatim: a drag handle, a per-layer preview thumbnail (new
  `PatternLayerThumbnail.jsx`, rendering the layer's own vector contour sized to just that shape's
  own bounds via `patternSizedPath`, falling back to the shape-kind glyph for kinds with no
  contour), the name/kind/opacity meta line, then eye/lock icon buttons — same classNames, same
  look, as requested, instead of the retired bespoke `se-layer-row` styling. Retired the now-dead
  `se-layer-row`/`se-layer-name`/`se-layer-toggle` CSS across `shapeEditor.css`, `studioTheme.css`
  and `artworkAuthoring.css`; added narrow overrides so the legacy `.pe-layers button > span` 12px
  swatch sizing doesn't clobber the ported thumbnail/kind-icon/name slots.
- Investigated the reported native (C++) Pattern Studio selection-hitbox issue ("highlight spans the
  whole canvas instead of hugging the selected shape"). Traced `QueryVectorSelectionExtent` /
  `QueryVectorAuthoringSelectionBounds` (`VectorAuthoringSelectionEditing.cpp`) and the pen tool's
  `EncodePath` (`CubicPathEditing.cpp`): both already compute tight, shape-local bounds for a single
  selected primitive or hand-drawn/library cubic-contour path in every scenario exercised, including
  a tile-normalized library-path simulation. Found and fixed a real gap instead: `VectorSelectionValidation.cpp`
  (1431+ assertions covering this exact selection-bounds/resize/reorder surface) existed but was
  never wired into CMake/ctest, so it never ran in CI. Added it as a real ctest target and added two
  new regression checks pinning a hand-drawn motif and a tile-normalized imported path to their own
  tight bounds, not the full 512 canvas. Host suite 27/27 (up from 26). If the on-canvas symptom
  still reproduces after this, it is most likely the multi-select case (selecting an entire
  array-placed group, whose true combined bounds legitimately span the tile) rather than a bounds
  bug — flagged for the user to confirm against the next preview.
- Ported the same layer-row preview into the native Pattern panel. `ConstructLayers()`'s row no
  longer draws a static placeholder swatch: it now renders that layer's own shape, cropped to its
  own tight extent (new `EncodePatternLayerThumbnailSvg`/`RasterizePatternLayerThumbnail` in
  `PatternThumbnailRaster.h/.cpp`, sharing the existing per-instruction SVG emitter with the gallery
  encoder so the two never drift). A second, smaller GPU atlas (`PatternWorkspacePanel::LayerAtlasTile`
  = 64px, 12×12 grid) carries every row's thumbnail for the open document and rebakes only when the
  document's edit revision moves; `WindowsTextureStudio.cpp` wires it up exactly like the existing
  library gallery atlas (`PatternLayerAtlas`/`PatternLayerAtlasDescriptor`, stage/upload/confirm/
  finalize, `Pattern->QueryLayerThumbnailAtlas`). Before the atlas is baked (or if no host wires it,
  e.g. in a headless/test build) the row falls back to a CPU vector replay of just that one layer via
  the existing `TessellatePresetDocument` tessellator, and only falls back further to a bare outline
  if that layer has no renderable geometry at all. `PatternThumbnailAtlasValidation` gained two new
  headless test blocks (single-layer thumbnail crop + the open-document layer atlas revision/bake
  contract) — 58 checks, up from 45, no new exhibit/CMake target needed. Host suite stays 27/27.
  The Vulkan wiring in
  `WindowsTextureStudio.cpp` is Windows-only and cannot compile-check in this Linux sandbox (same
  constraint as the rest of that file); it is a line-for-line structural mirror of the already-shipped
  `PatternAtlas` plumbing, reviewed by hand, and will be confirmed by the next Windows CI build.
- Pattern library motifs: the composable procedural modifier stack the user asked for (their own
  worked example — Diamond weave as one ring array/inset, mirrored top/bottom, then aligned to the
  tile edges) now exists in both HTML and C++, proven against the "Diamond weave"/"Diamond Weave"
  starter in each:
  - HTML: new `Experimental/TextureStudio/src/patternProcedural.js` exports three pure, reusable
    operations over a plain layer array — `arrayInset(layerFactory, kind, base, {count, sizeStep,
    stepX, stepY, colors, roles})` (repeats/shrinks one base layer, optionally role-tagging each
    copy), `mirrorMotif(motif, {axis, through})` (reflects x/y about a line, negating rotation) and
    `alignToEdges(motif, anchors)` (translates a motif cluster to tile-relative anchors, optionally
    recolouring by the `role` tag an anchor's `recolor` map names). `patternDocument.js`'s "Diamond
    weave" starter branch now calls these three operations instead of three nested hand-written
    loops; verified bit-for-bit identical output against the old flat implementation (same 16 diamond
    layers, same coordinates/colours, via a direct comparison script) before and after the rewrite.
    `validatePattern()`'s field-by-field layer rebuild already drops unknown properties, so the
    generation-only `role`/`proceduralRole` tag never reaches the saved/exported document.
  - C++: three analogous free functions added to `Engine/ContentInterchange/PatternLibraryPresets.h`
    — `ConstructInsetMotif` (runs the existing, already-shipped `GenerateVectorAuthoringArray` engine
    against a throwaway scratch document and harvests the instructions as a plain list, so Array/Inset
    reuses one tested placement engine rather than a new shrink loop), `MirrorVectorAuthoringMotif`
    (reflects a plain instruction list about an axis) and `AlignVectorAuthoringMotifToEdges`
    (translates copies to anchors, recolouring instructions that match a sentinel accent colour). A
    new 7th library preset, "Diamond Weave" (`ConstructPatternLibraryPreset(6)`,
    `QueryPatternLibraryPresetCount()` now 7), is built from exactly these three calls: one 4-ring
    inset array (242/189/136/83, matching the HTML sizes), mirrored across the tile's vertical
    midline, then aligned to the left/right edge quarters with two different accent colours.
    `PatternLibraryPresetValidation` gained a dedicated structural check (ring count, inset sizes,
    per-edge accent count) plus the count-change assertion bump to 7 — now 2065 checks, all existing
    index-0..5 preset checks untouched. Full build + `ctest` → 27/27 PASSED.
  - Still open: this pattern (one base shape + array/inset + mirror + align-to-edge) has only been
    applied to the Diamond weave/Diamond Weave starter so far, as the proof of the approach — the rest
    of the ~7,874-line, 9-module HTML pattern library and the remaining C++ presets have not yet been
    converted. Scoped but not started; large enough to warrant explicit user sign-off before an
    unattended full-library pass (risk of silently regressing 100+ shipped pattern designs without
    per-pattern visual verification).


## Vulkan illumination history — 2026-10-24

- Shared Slang temporal/spatial reservoirs, rigid reprojection rejection, temporal moments and edge-aware
  reconstruction now execute in native Vulkan validation on SwiftShader CPU. Six moving ControlVehicle poses,
  exact-light references, rejection diagnostics and light/residual invalidation checks are retained together in
  `Exhibits/LightCompute/README.md` and `Results/History.json`.
- Local quality checks: 69.08% lower static RMSE after sixteen updates; 56.04% lower mean per-image moving RMSE.
  These are fixture measurements, not equal-cost timings or general unbiasedness claims.
- Still open: physical GTX/RTX and hardware ray queries, Windows execution, production viewport, deforming motion,
  wider history/bias regressions and GPU primary/indirect transport. Filtering remains visibly imperfect.

Emissive-volume regression checkpoint: legacy fog ASan/UBSan checks, Vulkan exact/cluster/overflow,
static/moving history, light-off/residual refresh and all four native CTests passed. The first legacy history
attempt used the primary-motion archive (no uploaded raster receivers), correctly failed its coverage assertion,
and was rerun successfully using native `--vulkan-input` and `--motion-input` archives. Chronology is retained
in `Exhibits/LightCompute/Results/FogFacetRegression.txt`; this was a fixture-selection error, not a renderer fix.

## Closed-glass vehicle motion — 2026-10-26

- Six native posed ControlVehicle views with284closed glass/4mirror triangles each pass SwiftShader CPU Vulkan:
  exact fresh directional glass/mirrors, conservative geometry reset,17718stationary opaque history reuse,
  emission revision and23040black source-off pixels. Actual GIF/report: `Exhibits/LightCompute/GlazingMotion.md`.
- Original pose4 failure preserved (2capped/0unsupported paths); faint-path compensated survival addresses the
  capped absorbing continuations without raising2048/128limits or relaxing zero-cap tests.240production-block
  GPU arithmetic cases pass; all5previous static PNGs remain byte-identical.
- Complete retained optical chain after the shader change passed(exit0), including vehicle transport and
  old-material moving-history regressions. No physical GPU, Windows/editor,
  continuous moving authored indirect reuse, glass denoiser or completed unified renderer claim.

## Thin-pane transmittance, volumetric shafts and multi-stratum mixtures — 2026-10-26

- `SegmentShadowTransmittance` and `CrossedPanes < 8` close the remaining thin-sheet (`Category == 2`) transmittance
  gap across compact surface area-emitter quadrature (`SampleEmissiveFacet`), emissive volumetric fog (`SampleFogFacet`),
  analytic volumetric fog (`IntegrateFogScattering`) and 8-pane BSDF continuation, while keeping surface baking out of
  shadow/fog loops. Report and actual 4-panel Vulkan captures: `Exhibits/LightCompute/PaneTransmittance.md`.
- Verified on SwiftShader CPU Vulkan (`type=4`) + ASan/UBSan native C++: 8/8 native checks (`7.37e-7` balance-heuristic
  vs cosine identity), 2,016 surface emitter probes (`1.54e-8` max), 922 analytic fog probes (`7.09e-7` max), 541 emissive
  fog probes (`1.45e-6` max), 4 reused multi-stratum preset mixtures (`0.00969 -> 0.00745 -> 0.00730`), pinned `f5d99d4`
  negative control (`0.9937` floor leak vs `0.6563` accepted), 4/4 CTests, and 5/5 static `VehicleGlazing` PNGs byte-identical.
- Still open: roughness-mip prior-screen exitance/denoising, unified moving `ControlVehicle` + baked distance-field +
  sun/sky/cloud/fog scene, physical RTX/GTX + Windows/editor integration, legacy half-pole defect and native Jolt dynamics.


## VehicleHMI reference correction and low-cost raster continuation — 2026-10-27

- Continued `ae0bb7d` on `arena/2711375f-frontier`; corrected the incomplete-source audit, not a new physics rewrite.
- Full reference at `6afb6f00b5f25c45f7cda92c7da65b64845768f3` contains lunar textures, separate warm/anamorphic
  streaks, 160-particle XPBD and continuum tyres. Selected originals + hashes: `Experimental/VehicleHmiReference`.
- Restored actual Luna 2K JPEG; fixed missing ignored texture and sRGB decoding before filtering. Added independent
  warm streak and Cornette–Shanks aerosol phase. Sky 41/41; environment 73/73 release and ASan+UBSan.
- Existing production physics 29/29; full-demo original tests 5/5; recovered XPBD audit 12 cases verifies prescribed
  spring-force output, not integrated continuum reactions. Existing 84-particle lattice is not the full-demo port.
- Exhibit raster retains Lambert/Blinn + planar shadows. Improved hemispherical contrast, sky-coloured shadow fill,
  exposure-before-tone-map; skip matte highlights and reuse view distance. Same-camera paired medians 60.994–66.354 ms
  before, 44.072–47.157 ms after, unchanged 25,804,800-byte image buffers. Not a Vulkan renderer performance claim.
- Evidence and reproduction: `Exhibits/VehiclePhysics/Continuation.md`; actual before/after, Moon and flare captures.
- Open: full native continuum/XPBD parity, measured tyre/lens/sky validation, GPU/editor integration, Windows,
  unresolved solar-disc PSF, Moon parallax/scene illumination, cloud appearance and lunar-map licence confirmation.
- This entry supersedes earlier statements that the owner's demo had no flare, Moon texture or XPBD sources.

## StreamLinkInbox native recovery and port audit — 2026-10-28

- Recovered relevant source/evidence from `eosclient0001-rgb/Frontier` checkpoint `3de9061` into
  `streamlinkinbox/Frontier`, session branch `arena/08e7de48-frontier`, initially README plus an uploaded patch.
- Selective recovery: shared/native source, automation/rules, UI source and LightCompute/environment/night-sky
  exhibits; not all content, prototypes or unrelated exhibits. No editor package/build readiness claim.
- Fresh GCC 12.2/CMake 4.4.4 host verification: LightCompute Release build, 5/5 Release CTests,
  5/5 host ASan/UBSan CTests and 73/73 native environment checks. GPU captures remain inherited.
- Replay runtime blocked by absent Slang compiler and SwiftShader. No shader/estimator changes or GPU rerun claimed.
- ReSTIR remains highest priority and incomplete: seed retracing excludes reservoir reuse; production viewport,
  physical GPU/Windows acceptance and equal-time game-performance evidence remain open.
- Audit, retained actual captures, scope boundaries and completion gates: `Exhibits/LightCompute/PortAudit.md`.

## Retained-seed RIS and spatial replay — 2026-10-28

- Restored checksummed Slang 2026.14.1/SwiftShader Linux host tools; fresh isolated recovery and tamper rejection
  verified. Reproduced the inherited full sample-history reference/capture baseline before estimator changes.
- Added default-off `PathReplaySeedReservoir` to shared C++ Vulkan recorder and Slang: gather before generation,
  RIS over current-surface retraces of unselected seeds, optional spatial replay, fresh-only seed-bank shifting.
  Selected-reservoir temporal reuse, colour reconstruction and rough/textured intake remain rejected.
- Final Release and host ASan/UBSan: 5/5 CTests each, 2,445 local + 2,425 spatial rectangle/selection comparisons
  (zero maximum error), corruption/policy/scene reset guards, 16-batch one/four-bounce independent references,
  actual readbacks and identical numerical/decoded-image results. External driver/shaders are not instrumented.
- Four-bounce RMSE: seed mean 0.152946, local RIS 0.153750, spatial RIS 0.097017. Spatial uses up to eight current
  paths versus four; not equal work, eight-fresh-path comparison, game-GPU timing or a speedup claim.
- Corrected a new test-fixture environment mismatch found in review; retained the invalid first comparison and
  tightened the gate to reject it. Final GPU/reference environments match; no acceptance relaxation.
- Default sample-history regression matches fresh pre-change baseline: 15 rows and 4 decoded PNGs identical.
- ReSTIR is still incomplete for games: general motion/material estimator, memory reduction (existing combined
  output slab ~1.24 GiB at 1080p), production viewport, Windows and physical-device acceptance remain open.
- Evidence, actual captures, source hashes and reproduction: `Exhibits/LightCompute/ReplaySeedReservoir.md`.

## Compact seed-history output storage — 2026-10-28

- Added opt-in `PathReplayCompactStorage` to shared C++ footprint/recorder and Slang addressing, with an eleven-slab
  output range: image, five replay slabs, four bank/scratch slabs and primary hit/miss diagnostics. The legacy
  layout remains default. Compact mode requires the bounded sample-history contract; reset on layout/allocation change.
- Validation host actually allocates/binds the reduced range and records Vulkan memory requirements. At 64x64,
  output allocation is 2,868,240 -> 720,896 bytes (74.87% lower); all eight buffer allocations are
  3,501,492 -> 1,354,148 bytes. No shader work, timing or physical-GPU VRAM claim.
- Final Release + host ASan/UBSan: 5/5 CTests each, fourteen paired 17x17 frames, four paired 64x64 frames;
  exact output, all nine logical slabs, primary guides/strikes and counters. Includes corruption, camera motion,
  geometry, source off/on, policy/arithmetic/RIS transitions, 289 primary misses and descriptor-excluded tail guards.
- Final source/driver/SPIR-V hashes and decoded captures agree across configurations. The full legacy replay
  reference regression matches fifteen pre-seed-RIS rows and four decoded PNGs; prior spatial capture also matches.
- Remaining memory work is not closed: compact output alone extrapolates to 348.05 MiB at 1080p, not allocated
  or rendered here, and can exceed device storage-range limits. Next: equal-budget fresh-path control, broader
  temporal/motion/material validation, reconstruction and production submission/viewport; Windows/GPU acceptance open.
- Evidence, remaining path, layout contract and reproduction: `Exhibits/LightCompute/ReplayCompactStorage.md`.


### Eight-current-path quality gate — 2026-10-28

- User accepts **348.046875 MiB output-only at 1080p for now**, extrapolated, not allocated/rendered;
  keep reporting it. Further packing is deferred. Footprint/recorder source and default SPIR-V are unchanged.
- Completed a single-dispatch eight-fresh control using the shared path evaluator, compared at the same 702
  geometric floor points with four local plus four donor current path calls. Three-frame reuse warm-up is separate.
- Release + host ASan/UBSan: 5/5 CTests each, 818 independent rectangle comparisons with zero RGB error,
  poisoned old-history exactness, four instrumented/plain state-parity frames, all four numerical/reference gates.
  Final 23 budget rows, 39 logged hashes per configuration and six decoded capture images agree.
- Observed reuse RMSE changes versus eight fresh: one bounce off/on emitter sampling +21.13%/+30.54%;
  four bounces off/on -12.55%/+6.00%. The observed benefit is not a clear 2-SE paired result; no general win.
- Full 32x32 image: fresh 8,192 current calls versus reuse 7,902 current + 18,048 warm-up calls per batch.
  Equality is at measured points, not whole-image work, time, triangle tests or physical-GPU performance.
- Keep seed RIS opt-in. Next: broader seed/scene/motion variance validation and a specified temporal estimator;
  production viewport/reconstruction, physical-GPU equal-time quality and Windows acceptance remain open.
- Evidence and reproduction: `Exhibits/LightCompute/ReplayPathBudget.md`.


### Temporal seed measure and selected-source safety — 2026-10-28

- Specified the conditional weighted seed-measure identity, current-target proposal weights, and matched
  retained-count normalization. Correlation does not require a mean bias but does invalidate independent-Spp claims.
- Native exhaustive validation: 120 configurations / six submissions / 6,868,800 outcome visits; weighted basis
  error <=1.36881e-17. Four negative controls expose visibility-dependent rejection, W clipping, count-only caps,
  and stale-RGB light leakage. This is a finite ideal-randomness model, not a universal GPU unbiasedness claim.
- Reproduced actual malformed selected-history failure before repair: +infinite W accepted, count=4, RGB NaN.
  Shared Slang now validates finite canonical seed/count/W descriptors and finite positive contribution/sum.
  No new production mode/interface/buffer; maximum legitimate M=68 remains accepted.
- Final Release + host ASan/UBSan pass: 5/5 CTests each, 18 GPU conditions / 14,040 independent rectangle pixel
  comparisons (84 reported boundary exclusions), ten source-corruption controls, valid four-fresh count-68 boundary.
  All 48 temporal result rows, 39 logged digests per configuration and two decoded captures agree.
- Valid checked/unchecked outputs, five used slabs and counters are exact; unchecked SPIR-V retains the five
  pre-repair shader digests. Unchecked control fails eight corruption cases, two with nonfinite images.
- Release compact regression: seven logged rows, 14 paired 17x17 plus four paired 64x64 frames and both decoded
  captures unchanged. Compact output stays **348.046875 MiB at 1080p**, extrapolated, not total VRAM; packing deferred.
- Temporal audit still uses small legacy storage. Selected-temporal + seed-bank RIS remains forbidden; next is a
  bounded validation prototype with actual-call-matched fresh/emitter controls, not automatic general enablement.
  Physical-GPU equal-time quality, Windows/editor, broader materials and reconstruction remain open.
- Evidence/reproduction: `Exhibits/LightCompute/ReplayTemporalContract.md`.


## Bounded selected-temporal weighting and duplicate evaluation cache — 2026-10-28

- Implemented an isolated, count-matched temporal + seed-bank RIS derivation; history represented share ≤50%,
  current radiance only, one fewer spatial donor. Production prohibition/defaults remain unchanged.
- 24 finite-model configurations/491,520 visits pass. Exact current-surface seed caching preserves 18 paired GPU
  frames and nine reservoir/bank slabs, saving 11,960 calls; it does not discard correlated proposals.
- Eight-warm/32-paired-batch results: observed four-bounce red RMSE 13.06% lower without explicit emitter sampling,
  4.65% higher with it. One-bounce emitter case is 26.27% worse. Mean-gate pass is not quality acceptance.
- Release/host sanitizer, source corruption/reset and five CTests are audited with retained cold/uncached controls.
  Keep prototype only: physical-GPU equal-time tests and stronger emitter-enabled fixtures remain acceptance gates.
  Output-only 348.046875 MiB extrapolation stays accepted; packing, Windows/editor and other scope remain deferred.
- Evidence: [bounded temporal report](../Exhibits/LightCompute/ReplayBoundedTemporal.md).


## Independent emitter-enabled temporal holdout — 2026-10-28

- Fixed weighting, 32 independently seeded paired batches, four-bounce emitter sampling and four conditions:
  stationary, camera pan, camera/blocker transition, higher diffuse albedo. All visible plane pixels measured.
- Observed red RMSE is 5.80%, 5.70%, 41.42%, 5.85% worse than Fresh8; none meets the fixed promotion gate.
  Mean agreement passes. No shader/default changes; thirteen shader digests match the preceding phase.
- Blocker change globally resets history: 110 newly exposed pixels stay measured, all 3520 pixel/batch checks
  have no selected-history contribution. Approximate four-call fallback versus Fresh8 explains much of this loss;
  do not attribute it to temporal weighting alone or claim equal work/time.
- Release and host sanitizer, five CTests, previous cache/guard controls and retained captures are audited.
  Next implementation targets: restore the fresh budget on reset; separate direct lighting from indirect reuse.
  Physical-GPU timing, broader materials, Windows/editor and full-game GI remain open; packing stays deferred.
- Evidence: [emitter-enabled holdout](../Exhibits/LightCompute/ReplayTemporalHoldout.md).


## Full-budget fresh reset correction — 2026-10-28

- Implemented an exhibit-derived metadata-only Fresh8 reset branch. Consume the eight-path mean once without
  spatial RIS; persist first uniform seed, unit weight/count and the existing one-fresh-seed bank update.
- 64 initial/moving-blocker and four extra explicit/dark/relight resets exactly match Fresh8. 225 non-reset
  outputs and nine slabs remain byte-identical. Eight source/bank slabs match on all 293 paired submissions.
- Independent 32-batch emitter-enabled reset red RMSE: 0.015512304→0.011005205 (29.05% lower), costing
  128512→262144 current path calls and 1675782→1809414 warm-up calls. Not equal time or a GPU speedup.
- Release/host ASan/UBSan, five CTests each, 56 logged digests per configuration and four decoded captures pass.
  Five production-default shaders remain unchanged. Only derived prototype Generate/Resolve change.
- Keep prototype only: non-reset emitter-enabled quality deficit remains. Next implementation is direct-lighting
  separation from indirect-only reuse; hardware timing, Windows/editor and full-game GI remain open.
  Accepted 348.046875 MiB output-only extrapolation stays unchanged; further packing remains deferred.
- Evidence: [reset budget correction](../Exhibits/LightCompute/ReplayResetBudget.md).


## Fresh direct / indirect-only reservoir split — 2026-10-28

- Implemented an isolated shader derivation: fresh eight-sample first-bounce lighting, reservoir weights only on
  deeper transport. Consume first-bounce light RNG draws without its indirect shadow query; no stale RGB history.
- Reuse current RGB as temporary direct scratch between Generate/Resolve. Primary diagnostics and 11N output
  footprint remain intact; no production shader, renderer flags or default changes.
- Fixed 32-batch plane/colored-room comparisons include matched split fresh and cheaper whole Fresh8.
  Direct-only RGB is exact with/without history; nine indirect slabs match, one-bounce indirect is zero.
- Observed RMSE: plane +1.03%; room −72.45%, but room paired MSE change is only 0.99 nominal SE. No promotion
  gate passes. Direct separation is implemented, not a reliable end-to-end GI-quality acceptance.
- Query work rises 42.83% on plane / 17.36% in room versus whole Fresh8; warm-up is charged separately.
  Fewer replay calls do not guarantee fewer queries. Next: balance MIS baseline and first-hop cost removal.
- Evidence: [direct/indirect split](../Exhibits/LightCompute/ReplayIndirectSplit.md). Windows/editor, physical-GPU
  timing, broader materials and full-game GI remain open. Accepted 348.046875 MiB output extrapolation unchanged.

### Balance MIS and same-seed first-hop sharing

- Isolated prototype and fixed 32-batch comparison: [report](../Exhibits/LightCompute/ReplayFirstHopMIS.md).
- Exact fused/unfused cache equivalence passed 36 submissions; fresh split matches whole transport-query counts.
- Stronger MIS control removes the apparent room reuse advantage: reuse regresses both fixtures. No promotion.
- Production unchanged; 348.046875 MiB output-only at 1080p retained, packing deferred. No physical-GPU speed claim.

### Fixed power-heuristic MIS control

- [Whole Fresh8 power comparison](../Exhibits/LightCompute/ReplayMISPower.md): 32 paired batches, identical query
  budgets for no-MIS/balance/power. Power beta=2 improves plane RMSE 24.17% versus balance and 2.72% versus off.
- Room paired uncertainty prevents broad quality acceptance. No production/default change and no reuse promotion.
- Active-effect and MIS-disabled exact-output assertions guard the actual FreshBudget evaluator substitution.

### Power-weighted first-hop / indirect reuse — rejection reproduced

- Previous recovered workspace and completed checkpoints pushed as `4d39616` on `arena/08e7de48-frontier`.
- [Power reuse experiment](../Exhibits/LightCompute/ReplayPowerReuse.md): fixed beta=2, no reservoir retuning,
  paired whole-power/no-MIS controls and independently referenced plane/room fixtures.
- Release fused/unfused preflight passed 36 submissions with exact output/nine slabs/primary and 49,664 saved
  nearest queries. Reuse regresses plane/room RMSE by 1.17%/75.70%, costs 37.05%/15.07% more final queries.
- Release and host ASan/UBSan benchmarks both exit 1: room MIS-off fails the fixed mean/reference gate;
  correctness=0/promote=0. Do not relax the threshold or call full numerical validation passing.
- Both five-CTest sets pass; no host sanitizer diagnostics observed. Rejection-evidence parity passes 44 rows,
  64 logged hashes/build, ten exact RGB captures and deterministic source rederivation.
- Follow-up: independent room control/reference convergence and sampling audit; completed in the diagnostic below.

### Room control/reference convergence — diagnostic agreement verified

- [Independent convergence diagnostic](../Exhibits/LightCompute/ReplayRoomConvergence.md): new CPU area-NEE/power
  references, 64 cohorts per method/depth, 256 paired GPU batches, analytic near-emitter control.
- Original 32-batch four-bounce means reproduce exactly; earlier failed gate is not regraded.
- Both host builds pass agreement checks at one/four bounces. Rare NEE contributions and short-run underestimated
  uncertainty support a high-variance explanation; no systematic CPU/GPU transport mismatch demonstrated.
- No production correction or reservoir promotion. Lower-variance independently checked CPU power reference
  is retained for future fixed holdouts. Host ASan/UBSan exit zero, both five-CTest sets pass, no host sanitizer
  diagnostic observed. Audit passes 1454 matching rows, 29 hashes/build, four exact readbacks and exact old prefixes.

### Temporal / spatial ablation — verification complete, neither quality arm accepted

- [Reuse ablation](../Exhibits/LightCompute/ReplayReuseAblation.md): 32 paired batches per scene, unchanged weights,
  independent CPU power reference, isolated histories, separate warm work and matched whole/fused fresh controls.
- Both host builds reproduce correctness=1; both quality gates fail. Temporal-only raises plane/room RMSE 1.56%/31.55%; spatial-enabled
  raises them 1.75%/47.52%. Spatial is not the sole cause of the loss.
- Room temporal-only saves 20.29% final queries but loses quality; equal-query fresh control still required.
- Next discriminating controls: independent fresh indirect seeds for D/I covariance; equal-query fresh room baseline.
  No reservoir retuning or promotion. Host ASan/UBSan exit zero; both five-CTest sets pass, no host diagnostics.
- Evidence audit passes 33 matching semantic rows, 66 logged hashes/build, eight exact original/PNG captures,
  feature isolation and exact nearest/shadow work sums.

### Fresh D/I correlation / Whole6 controls — verified, no promotion

- [Fixed controls and evidence](../Exhibits/LightCompute/ReplayCorrelation.md): five arms, 32 paired batches,
  separate fresh-I PRNG stream, eight warm steps, independent CPU reference; no parameter rescue or production port.
- Independent fresh D/I streams raise plane RMSE 0.63%; the room error change is uncertain. Correlation loss alone
  does not explain the much larger temporal room penalty. Allocation/proposal/history variance remains confounded.
- Whole6 room RMSE 0.157961268 versus temporal 0.179752331, with 584,171 versus 619,922 final queries:
  12.12% lower RMSE and 5.77% fewer queries. This is measured query/error dominance, not game-frame timing.
- Release and host sanitizer each pass five CTests and all ten mean gates, matched-seed/nine-slab and one-bounce
  checks, covariance decomposition and exact 6:8 calls. Parity: 37 identical rows, 70 hashes/build, ten exact
  captures/PNG pixels and source rederivation. Actual readbacks/frontier retained; no physical-GPU timing claim.
- No promotion. Keep production output 348.046875 MiB/1080p and defaults/layouts unchanged. A future proposal or
  allocation redesign needs new holdouts and explicit densities, not post-hoc acceptance of this configuration.
- Next direction: isolated bounded secondary-vertex reconnection with explicit densities/Jacobian/visibility;
  new representation must beat both fresh controls on fixed holdouts before any production port. Not implemented.

### Secondary-vertex reconnection — native correctness gate verified, GPU implementation pending

- [Fixed protocol and measured evidence](../Exhibits/LightCompute/SecondaryReconnection.md): static diffuse
  secondary square, shifted receiver and two strip-occlusion fixtures. Reuses production Reservoir unchanged.
- Angular and area encodings agree within 2.23e-16. All 12 corrected/fresh reference gates and six required
  negative controls pass. Source visibility holes require proposal and selection support, not just a Jacobian.
- Release and ASan+UBSan exit zero; 37 identical rows and four verified source hashes/build. Null candidates count
  in M; independent clipped-domain integrals predict the incorrect support-limited estimates.
- This is analytic native geometry/illumination, not a Vulkan renderer or cost comparison. No general transport,
  temporal/material, Windows/editor or physical-GPU acceptance. Production and 348 MiB output unchanged.
- Next: isolated area-coordinate Vulkan secondary-surface exhibit with visibility/null/support controls; then
  real geometry/suffix and both Whole6/Whole8 holdouts. No production port or promotion at this gate.

### Secondary-vertex analytic Vulkan gate — verified, not real-scene GI

- [Fixed protocol and actual readbacks](../Exhibits/LightCompute/SecondaryReconnectionGpu.md): area-coordinate
  Slang estimator, angular reference and four support/measure controls; 64×4096 reservoirs/method/fixture.
- Both builds exit0, all five CTests each, 12 reference means and six required negative controls. Area/angular
  maximum difference1.49e-7. All28 native/GPU mean checks pass; source support failures reproduce on Vulkan.
- Cross-build parity:38 identical rows,31 checked hashes/build,four exact raw captures and28 PNG arrays.
- SwiftShader CPU Vulkan type4, analytic plane/strip geometry and deterministic secondary illumination only.
  Source payload remains in one invocation; second dispatch inert. No persistence, ray-cost or frame-time claim.
- Next: actual triangle secondary hits/visibility and independently validated outgoing suffix, then persistent
  reuse/history invalidation and Whole6/Whole8 holdouts. Production defaults/output348.046875MiB unchanged.

### Triangle-BVH secondary transport — correctness verified, literal implementation not accepted

- [Triangle transport and outgoing-lighting gate](../Exhibits/LightCompute/TriangleReconnection.md): actual
  production NearestFacet queries for cosine hits, reconnection and directional-source visibility; nonemissive
  diffuse secondary patch and opaque strip. Area mixture/floor support and null M accounting retained.
- Release and ASan+UBSan host pass all five CTests each, 12 reference means and six required transport negatives.
  Area/angular difference <=1.79e-7. Suffix:1,048,576 evaluations of16,384 unique fixture/point pairs;114,688 blocked
  probes disagree with the deliberately unshadowed suffix as required.
- Cross-build audit:42 identical rows,33 checked hashes/build,four exact raw captures,32 PNG arrays; derived shader
  matches both builds. Actual readbacks inspected. No shader/driver sanitizer or physical-GPU timing claim.
- More BVH calls in all fixtures. Occluded RMSE worsens28.18%/19.01%, queries+36.34%/+10.53% versus this restricted
  Fresh8 component control. This is not the full four-bounce Whole8; game-quality/efficiency acceptance absent.
- Next: persistent source vertex/outgoing/PDF payload with light/geometry invalidation and fresh receiver
  visibility; retain variance/support holdouts and later full Whole6/Whole8 comparisons. Not implemented yet.
- Production source/default/layout and output348.046875MiB unchanged; no port or promotion.

### Persistent vertex/outgoing cache — verified query optimization, not production acceptance

- [Payload lifecycle and actual readbacks](../Exhibits/LightCompute/VertexCache.md): GPU payloads persist across
  real submissions; receiver visibility is fresh. Eight-step cadence includes light/material/geometry revisions.
- Release and ASan+UBSan host pass five CTests each;64 reference means,12 stale-cache negatives and32 pair gates.
  Cached vs same-seed inline discrepancy <=5.96e-8; warm source/suffix queries zero, <=1 receiver query/reservoir.
- Eight-step total BVH query reduction20.67–27.31%; warm reductions94.95–96.24%. Not physical-GPU execution time.
  Frozen source samples add no independent quality; the prior occlusion variance penalty remains unchanged.
- Parity194 identical rows,63 checked hashes/build,32 exact raw captures and32 representative PNG arrays.
- Implementation backed up to GitHub0441442 after prior-phase checkpoint282bd5a; final evidence retained here.
- Next: bounded refresh, receiver-motion holdouts and fresh-control temporal quality/cost, not a direct port.
  No RGB/general surfaces/multi-donor/history reprojection or editor/Windows/physical-GPU acceptance.
  Production348.046875MiB unchanged;32 B/test-receiver payload +16 B diagnostic telemetry are exhibit-only.

### Bounded cache refresh — verified; frozen-winner temporal quality not accepted

- [Fixed motion/refresh holdouts](../Exhibits/LightCompute/CacheRefresh.md): refresh1/4/8, fresh RIS8 and cosine6/8
  restricted-component controls;16 poses,32 independent cohorts,1024 trajectories,4 triangle-strip fixtures.
- Release + ASan/UBSan host exit0/five CTests each. All384 pose-mean gates pass;period1/inline max2.39e-7.
  Parity494 identical rows,97 checked hashes/build,64 exact raw captures and24 retained PNG arrays.
- Period4/8 reduce queries41.89–67.56% versus component fresh8, but temporal RMSE rises28.85–274.80%.
  All16 temporal MSE contrasts versus fresh6/8 positive, minimum17.38 nominal SE. No guaranteed-CI claim.
- New .3 holdout:period4/8 temporal RMSE+100.05%/+140.20%;lag1 .4206/.5841 versus fresh8 .0005.
  Lower flicker can hide correlated error. Prior donor-proposal variance penalty remains separately visible.
- No retrospective period/floor tuning or promotion. Next: retain density-bearing candidates before donor winner
  selection, current-receiver selection and explicit sample refresh, with fixed storage/query budgets and holdouts.
- Software Vulkan only;not TAA/full GI or physical-GPU timing. Production348.046875MiB/defaults/layout unchanged.

### Candidate retention — verified restricted-component gains; production promotion deferred

- [Candidate bank](../Exhibits/LightCompute/CandidateBank.md): eight raw area proposals before donor selection;
  inline area8/full-bank8, rotate4/2, fresh cosine6/8;same32-cohort/16-pose/4-fixture protocol.
- Rotate4 versus component fresh6:queries−8.45% to−35.80%,instant RMSE−25.17% to−47.19%,temporal RMSE−3.04%
  to−28.85%;all four temporal MSE contrasts favorable,minimum4.34 nominal SE,not guaranteed confidence bounds.
- Rotate4 still loses temporal quality versus fresh8 on .3 holdout(+12.70%,17.99 nominal SE);.6 observed+1.47%
  is only1.69 SE. Rotate2 temporal error worse versus fresh8 everywhere. Retention is not decorrelation.
- Nonpersistent area8 has lower instantaneous/temporal error in4/4 fixtures and fewer queries in3/4 vs fresh8;
  full-bank8 matches it pointwise and in queries. This known-area sampler is not full general-scene GI.
- Release/host ASan+UBSan exit0,five CTests each;494 rows identical,163 hashes/build,384 mean gates,128 exact
  raw captures,24 PNG arrays. Native17,039,360 slot-preservation checks;independent1,572,864-candidate audit,
  max q/reconstruction errors1.36e-7/5.34e-8. Nonnull-divisor negative disagrees at121,428 reconstructed estimates.
- Payload32→144B(+112B/reservoir,221.484375MiB at1080p);telemetry16B unchanged. Not a fit/promotion claim.
  Production348.046875MiB/layout/defaults/Engine unchanged. No physical-GPU/Windows/TAA acceptance.
- Next:receiver-anchored proposals with stored historical densities, same memory/rotation/holdouts, not packing
  or cadence tuning. Inline area8 remains the useful lower-storage comparison.

### Receiver-anchored proposals — quality holdouts pass; optimized query baseline still required

- [Historical-density bank](../Exhibits/LightCompute/ReceiverBank.md): same8 slots,replace4/2,32 cold cohorts,
  16 poses and four existing strip fixtures;new draws anchored at the current receiver,old q retained.
- Rotate4 versus restricted component fresh8:instant RMSE−31.78% to−37.90%,temporal RMSE−14.07% to−16.65%
  across all fixtures,including .3. Four paired temporal contrasts favor rotate4,minimum24.41 nominal cohort SE.
- Wrong current-q substitution CPU counterfactual fails30/64 pose means,10/10/8/2 by fixture. GPU correct means384
  pass;full-refresh inline/bank estimates and total query counts identical. Rotate2 still loses fresh8 temporal quality.
- Release/host ASan+UBSan exit0,five CTests each;558 semantic rows identical,165 checked hashes/build,128 exact
  captures,24 PNG arrays. Native17,039,360 retained-slot checks;independent1,572,864-candidate audit verifies birth q
  and current estimate,max errors1.36e-7/5.23e-8. All136 fresh-control rows unchanged.
- Query reductions37.72–49.46% are against the inherited fresh8 implementation,NOT optimized-speed acceptance.
  Source inspection finds fresh cosine first-hit followed by redundant visibility recheck;the bank repeats checks
  too. This caveat applies to earlier inherited-control query comparisons. Keep measured counts,not inflated claims.
- Payload144B+16B telemetry unchanged;not free production storage. Known square/scalar directional component only,
  no physical-GPU/Windows/TAA/full GI acceptance. Production348.046875MiB/Engine/layout/defaults unchanged.
- Next:exact-sample ray-budget audit/elision for controls and bank,including miss/occlusion/numerical-edge identity;
  only then revisit practical work efficiency and lower-storage weighted-reservoir injection. No packing or promotion.

### Ray-budget elision — rejected at numerical boundaries; main comparison blocked

- [Exact-sample safety audit](../Exhibits/LightCompute/RayBudget.md):frozen gate rejects any first-hit/reconstructed
  shadow-query disagreement. Both builds reproduce three entries on .3 strip,receiver x=1,target x=−.399999887,
  y=−.5/0/.5;probe indices918/938/958. They are one x configuration,not independent failure-rate samples.
- Four retained1024-probe fixtures:valid hits561/335/215/77,disagreements0/0/3/0. Actual Vulkan arrays retained;
  fixed prepared extent repeats input sets,with only the first1024 cases/fixture retained for analysis.
- Builds and five CTests/build pass,focused runtime exits1 in BOTH release and ASan/UBSan at unchanged gate.
  Evidence audit reproduces rejection:eight identical diagnostic rows,39 hashes/build,four byte-identical raw arrays,
  four PNG arrays,exact shader regeneration. No host sanitizer report;driver/shader code not instrumented.
- Twelve-arm32-cohort trajectory means/noise/candidate/work checks NOT RUN;blocked before them. No saving or
  optimized-speed claim accepted. Existing inherited-control query totals remain historical as-implemented counts.
- Setup attempt initially violated fixed submission extent,corrected without changing probe cases or safety gate;
  original setup and first failure logs retained. Full final collection logs all fixtures before rejecting.
- Next:record both ray directions,hit/reconstructed positions,facet IDs,distances and float bits;define/test consistent
  visibility semantics and neighboring representable rays before reuse. No post-hoc epsilon/threshold relaxation.
- No production Engine/layout/default change or promotion.348.046875MiB output stays. Windows/editor/physical-GPU
  time/TAA/general GI not accepted;bank memory burden unresolved. No PR or Actions run.

### Confirmed visibility reuse — complete scoped correction and fair baseline comparison

- [Visibility contract/results](../Exhibits/LightCompute/VisibilityReuse.md):actual reconstructed-point query remains
  authoritative. Same-origin/same-range diagnostics isolate one ULP in direction.x on918/938/958;original short
  ray misses,renormalized ray hits blocker2. No epsilon/tolerance or numerical-edge relaxation.
- Confirmed query reused for new candidate rejection,density and newborn receiver contribution;historical slots
  always retraced. Fresh cosine skips only null/out-of-domain rechecks;exact savings audited against misses.
- Release/host ASan+UBSan exit0,five CTests each;929 identical semantic rows,177 hashes/build,768 mean gates,
  12,582,912 exact paired estimates,34,078,720 retention checks,136 byte-identical raw captures,24 exact PNGs.
  All408 legacy mean/result rows unchanged;independent q/estimate errors1.36e-7/5.23e-8 over1,572,864 candidates.
- All4096 edge cases preserve recheck;23 primary/recheck differences in .3 including original three remain.
  Unsafe old gate rerun under new host:expected exit1,eight archived rows unchanged. Original rejection retained.
- **Both controls and bank optimized:**rotate4 vs fresh8 queries−16.18–21.80%,instant RMSE−31.78–37.90%,temporal
  RMSE−14.07–16.65%;minimum24.41 nominal cohort SE for favorable temporal-MSE contrasts. Not physical timing.
- Corrected fresh6 costs less:rotate4 spends4.28–11.76% extra queries for25.75–27.79% less temporal RMSE.
  Nonpersistent area8 costs12.20–23.85% extra queries vs corrected fresh8 for34.82–37.60% less temporal RMSE.
  Rotate2 saves queries but remains worse temporally than fresh8. No longer claim inherited-control work wins.
- Focused phase100%,remaining0%;overall renderer approximately35% **planning estimate**,not measured readiness.
  Done:diagnosis,correction,all baseline/alternative comparisons,both builds,regressions,visual proof and backup.
- Remaining separate gates:lower-storage weighted-reservoir/fresh injection,full geometry/RGB/GI,production port,
  physical-GPU/Windows/editor/TAA/timing. Experimental144B+16B bank not production-fit;packing deferred.
  Production348.046875MiB/Engine/layout/defaults unchanged. No PR or Actions;session branch backed up to GitHub.

### Two weighted reservoirs — correct historical weights;quality-neutral replacement rejected

- [Complete fixed-protocol evaluation](../Exhibits/LightCompute/WeightedPair.md):two selected float4s plus revision
  descriptor48B logical payload versus144B raw bank. Each selects from4 proposals with positive geometry h,
  w=h/q,Z=Σw/(4hselected). Four includes nulls;historical Z preserved;visibility/outgoing excluded from target.
- Full2 refresh8 draws;rotate1 alternates4-draw injection and one held reservoir. Cold/light epochs0/9/13 reset
  both. Confirmed visibility reused only for newborns;historical selected points retraced. Same seeds/events/means.
- Both release/host ASan+UBSan exit0,five CTests/build;1,069 identical rows,243 hashes/build,896 reference means,
  200 byte-identical captures,24 exact PNG arrays.816 previous control rows/136 control+diagnostic hashes unchanged.
  Native1,703,936 selected-history checks;262,144 exact independent float32 selection replays,6,144 finite cases.
- Wrong null divisor,current target denominator,and zero birth-visibility target detected mathematically. Positive
  target preserves support but selection adds conditional variance. Empty-strip full2/full8 discrepancy5.97e-8 max.
- Rotate1 vs corrected fresh8:queries−19.52–26.86%,instant RMSE−19.29–37.67%;temporal changes−16.03%,+2.05%,
  +1.19%,−2.59%. Frozen all-fixture acceptance fails .12/.3;adverse MSE contrasts3.74/2.03 nominal cohort SE.
- Versus corrected Rotate4:queries−3.99–6.46%,temporal RMSE+0.17–19.43%;retain as budget tradeoff only,not a
  quality-neutral replacement. No target/cadence tuning after measurement. Lower-storage quality goal stays open.
- Focused evaluation100%,remaining0%;overall renderer stays~35% **planning estimate**,not measured readiness.
  Logical96B difference is not measured allocation/bandwidth reduction. Common debug buffers retain audit vectors.
- Next distinct candidate:intermediate reservoir count/support-safe score plus new frozen holdout. General RGB/GI,
  production C++ integration,physical-GPU/Windows/editor/TAA/time/memory acceptance remain undone. No packing.
  Production348.046875MiB/Engine/layout/defaults unchanged. Source and final evidence backed up;no PR or Actions.

### Four weighted reservoirs — restricted logical-memory/variance compromise accepted

- [Complete fixed-protocol comparison](../Exhibits/LightCompute/WeightedQuartet.md):four selected reservoirs,two
  proposals each,replace2/update.8 cold/4 warm proposals;positive geometry h,birth Z=Σ(h/q)/(2hselected),average4.
  Full0/9/13 rebuilds;confirmed newborn visibility reused,historical visibility retraced. No numerical weakening.
- Same14 controls+full4w/rotate2w;32 cohorts×1024 trajectories×16 poses. Four previous strips plus predeclared
  new .45 holdout. Frozen corrected-Fresh8 instant/temporal/query acceptance PASSES5/5;no target/cadence tuning.
- Queries−17.51–24.00%,instant RMSE−27.62–37.82%,temporal RMSE−8.60–16.12% vs corrected Fresh8. All five paired
  temporal-MSE contrasts favorable,minimum14.85 nominal cohort SE,not guaranteed confidence intervals.
- Versus raw Rotate4:queries−1.58–2.81%,temporal RMSE+0.06–6.93%;logical history144→80B(−44.44%). Quartet
  is not quality-neutral to the raw bank. Quartet uses1.35–9.99% MORE queries than corrected Fresh6,for
  20.61–27.59% lower temporal RMSE.
- Both release/host ASan+UBSan exit0,five CTests/build;1,527 identical rows,375 hashes/build,1,280 mean gates,
  330 byte-identical captures,24 exact PNG arrays.952 old result rows/200 capture hashes unchanged. Original
  three boundary disagreements preserved;all5,120 confirmed edge cases pass.15,728,640 matched estimates exact0.
- Nativequartet4,259,840/pair2,129,920 history checks;independent655,360/327,680 float32 selection replays.
  Max q/weight/Z/estimate errors1.37e-7/6.54e-7/4.71e-7/7.78e-8;384+6,144 finite checks with negative controls.
- Focused phase100%,remaining0%;overall renderer~40% **planning estimate**,not measured readiness. Done:design,
  implementation,all baseline/alternative comparisons,new holdout,both builds,independent evidence and backup.
- Logical80B is scalar/planar only,not allocated savings or RGB/facet descriptor cost.16B telemetry unchanged;
  common debug allocation still includes all audit fields. No production Engine/layout/defaults change or packing.
- Next distinct gate:general-scene RGB/facet descriptors and support-positive targets at the same fresh-query
  budget,then production integration and real device/editor/TAA/time/memory acceptance.348.046875MiB remains.
  No PR or Actions;source and final evidence committed/pushed to the session branch.
