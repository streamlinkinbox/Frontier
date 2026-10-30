# Fresh native facet-area transport integration

## Frozen protocol — before measurements

- Integrate existing native proposal,compensated target and pair selection with fresh camera rays,primary hits,
  suffix and receiver-connection visibility. No recorded GPU receiver,ray,visibility or RGB enters native execution.
- Reuse production packed triangle BVH and port its shader-order float32 ray-aligned facet strike/box queries.
  Independent existing double BVH plus linear double triangle rays audit newly generated rays. No visibility
  exemptions or tolerance-based boolean waivers. Invalid scene/ray inputs remain explicit rejections.
- Scope:the existing nine16-facet geometric/perspective/deformed diffuse fixtures,two RIS and four raw controls.
  Source normals interpolate and orient as in the shader;material textures/general BSDFs/history are not ported.
- Preserve original camera constructors,seeds,proposal counts,floors,ray offsets and bounds. Native executable
  takes only output directory,not GPU captures. Original manifests verified before post-run comparisons.
- Strict Release/host ASan+UBSan and prior1060+193+14831 component unit regressions. Native captures must match
  across builds. Native primary hits221184,candidates1474560 and selected/raw paths1179648 across nine captures.
- Compare primary positions/normals/barycentrics within2e-5;different primary facet only on a shared boundary
  within2e-6 with equal material,maximum16. All proposal identities and traced visibility bits must match GPU.
  Selection differences only within2e-6 of the threshold,max16;full image equivalence requires zero differences.
- Native emitted-ray double visibility agreement must be exact for2359296 suffix/connection checks. Compare
  native/GPU deformed emitted-ray coordinates within2e-5. No use of GPU rays to repair native construction.
- RGB max channel error2e-5;54 native/reference channel means use6*SE+2e-5. Native RIS ordinary/cross-reference
  MSE increase≤1e-10 versus matching GPU estimator. Retain raw4/raw8 images and metrics;no new quality-win claim.
- Negative controls:omitted receiver visibility and omitted suffix visibility must alter native transport in each
  deformed fixture. An empty/invalid BVH must not silently manufacture unoccluded transport. Synthetic primary,
  shared-edge,miss,finite-segment and invalid-intake checks required before full captures.
- Present actual freshly traced native RGB images. Software CPU arithmetic counts are not measured GPU speed.
  No production activation/default/32B descriptor/348.046875MiB allocation change,packing,PR or Actions.
  Windows and physical GPU execution remain untested. Freeze gates before the first corpus run.

## Result — numerical parity passes;frozen acceptance held,2026-10-30

**Not promoted.** Fresh native transport agrees with the retained GPU and both double ray checks,but three
of the six frozen per-fixture omission-sensitivity checks fail. This result does not weaken or replace the gate.

|Gate|Measured result|Decision|
|---|---|---|
|Strict Release/host ASan+UBSan|Four CTests and17,142 focused checks/build|Pass|
|Fresh native primary hits|221,184;all hit/position/normal/material bytes match GPU|Pass|
|Fresh proposals/targets|1,474,560 candidates;identities/PDFs/barycentrics match,target error0|Pass|
|Fresh selected/raw paths|1,179,648;all selected ordinals and visibility bits match GPU|Pass|
|Independent emitted-ray visibility|2,359,296 checks agree with both double BVH and linear double rays|Pass|
|Primary double audit|No differing primary identities/ties;distance tolerance passes|Pass|
|Deformed emitted ray coordinates/ranges|Maximum native/GPU difference0|Pass|
|Native/GPU lane/packet RGB|Maximum absolute errors1.49012e-8/3.72530e-9|Pass|
|Native/GPU image RGB|Maximum absolute error9.89531e-10 across54 images|Pass|
|RIS reference means/MSE preservation|54/54 means and18/18 ordinary+cross-reference gates|Pass|
|Native normalizer parity|Maximum normalized error1.54812e-7|Pass|
|Build parity|All9 native buffers and10 semantic rows byte/text-identical|Pass|
|Omitted connection sensitivity|Changes RGB in1/3 required deformed fixtures|Fail|
|Omitted suffix sensitivity|Changes RGB in2/3 required deformed fixtures|Fail|

### Retained failed controls

|Deformed fixture|Connection omitted:changed packets|Suffix omitted:changed packets|Frozen requirement|
|---:|---:|---:|---|
|0|0|0|Both must change at least one packet:failed|
|1|0|7,247|Connection sensitivity failed|
|2|4,158|3,841|Both sensitive|

Each fixture has24,576 estimator packets across six methods. The image/RGB omission check uses absolute
channel differences greater than1e-12,not a subjective visual judgment. Zero changes indicate a blind spot
in these fixtures' RGB controls;they do not prove that visibility is unnecessary or permit removing the queries.
All actual visibility booleans still match independent double rays exactly,including blocked paths.

The [first audit failure](Results/FacetAreaTransport-first-audit.txt) is retained. Its collector stopped at the
first insensitive case. The final collector completes all comparisons and writes the failure table before
returning **exit1** on the unchanged frozen acceptance requirement. No sampler,fixture,offset,seed or gate
was tuned after that failure. Shared-diagonal/finite-endpoint synthetic units were added to complete the
predeclared unit coverage;their inclusion did not change any native capture bytes.

### What now runs natively

```text
retained source constructors + camera definitions (not GPU buffers)
    → packed production BVH + native float32 primary ray strike
    → native surface interpolation/orientation
    → native proposal → compensated score → pair selector
    → fresh native suffix and receiver-connection rays
    → freshly traced RGB estimators + raw4/raw8 controls
```

- New `FrontierFacetAreaTransport` target is `STATIC EXCLUDE_FROM_ALL`,C++17,strict warnings,without fast math
  or contraction. It reuses production packed BVH layout and the existing native score/proposal libraries.
- `Prepare` validates finite geometry/shading,packed-corner order,tree reachability/cycles,nested-bound containment,
  leaf coverage and area/PDF support. Failure invalidates the previous scene. Empty/invalid BVHs reject queries;
  they do not silently return valid unoccluded transport. `Trace` distinguishes valid misses from rejected rays.
- Ray-aligned facet strikes and packed-box queries preserve the shader operation order. The bounded64-slot
  query stack has the shader's exhaustive fallback rather than silently dropping triangles.
- Primary surface interpolation,normal orientation and source diffuse shading execute in C++. Suffix/connection
  rays use the same fixed light,source/receiver offsets and finite bounds as the existing corpus. This is a
  deliberately bounded diffuse transport API,not general material/texture/BSDF or environment transport.
- The native executable accepts only `--unit` or an output directory. It reads **no GPU capture,receiver,ray,
  visibility,selected integrand or RGB input**. GPU files and reference banks are opened only afterward by Python.
- Every fresh visibility ray is audited against the existing double production BVH and a separate linear
  double Möller–Trumbore loop. Those extra audit queries are not counted as estimator queries or timing gains.

### Evidence and reproduction

- [Actual freshly traced native RGB comparison](Images/FacetAreaTransportComparison.png),visually inspected:
  native/GPU RIS speckle and geometry coincide;native raw4 is noisier in the first two shown fixtures;raw8
  remains slightly more accurate in all3 shown. The third fixture does not establish a robust uniform-RIS gain.
- [All54 native/GPU RMSE comparisons](Results/FacetAreaTransport-comparison.md),including both RIS and all
  four freshly traced raw controls. [54 RIS reference means](Results/FacetAreaTransport-means.md),
  [failed sensitivity table](Results/FacetAreaTransport-negative.md),
  [held summary](Results/FacetAreaTransport-parity.txt).
- [Release build/runtime log](Results/FacetAreaTransport-release.txt),
  [host sanitizer log](Results/FacetAreaTransport-sanitized.txt),
  [SHA256 manifest](Results/FacetAreaTransport-evidence-hashes.txt). Prior GPU/component reports remain unchanged;
  historical engine CMake hashes are checked against their original snapshots,not silently rewritten.
- `Results/FacetAreaTransport-00.f32.xz` through `-22.f32.xz`:lossless fresh native captures. First digit
  corpus0=geometric,1=perspective,2=deformed;second digit fixture0..2. Decode XZ to41 slabs×24,576 float4 values,
  little-endian float32;each packet belongs to4096-sample method blocks0..5 as in the original GPU captures.
- Slab0 is newly traced RGB;slab1 is unused zero;slabs2..17 contain proposals,lane contributions,targets and
  selections in the original layout;18=query counts;19..22=primary identity/strike,position,normal,albedo.
  Slabs23..38 contain eight native connection origins/ranges and directions;unused slots are canonical zero.
  Slab39 omits connection visibility;40 omits suffix visibility. The archives preserve all diagnostic rays,
  not just final images. XZ compression changes no payload bytes or production-memory layout.
- Actual54 native32x32 image arrays plus the deformed comparison crops are retained as PNG. Display uses
  identical linear exposure64 and nearest enlargement;metrics use unclipped float RGB,not PNG pixels.
- Exact source extraction reuses seven fixture/vector functions and two camera functions;the production BVH
  packing is unchanged. Reference images remain finite banks from the same renderer,not an external reference.

Prerequisites:C++17 library/C++20 fixture exhibit,CMake/CTest≥3.20,Python3 with Pillow. Python's standard-library
LZMA support decodes retained XZ buffers. Tested on Linux,GCC12.2.0,CMake3.31.6,Python3.11,Pillow13.0.0.
No Linux executable is shipped;Windows/MSVC and physical GPU execution remain untested. GPU comparisons use
retained software-Vulkan captures,not a new GPU run. No Vulkan dependency is needed for this native run.

```sh
bash Exhibits/LightCompute/RunFacetAreaTransport.sh
SANITIZE=1 bash Exhibits/LightCompute/RunFacetAreaTransport.sh
# Expected exit1:the frozen visibility-control sensitivity gate remains held.
_ClaudeScratch/venv/bin/python Exhibits/LightCompute/Code/FacetAreaTransportEvidence.py
```

The standalone project is `Exhibits/LightCompute/FacetAreaTransport`. The runner supports `CMAKE`/`CTEST`
overrides;default sandbox tool paths use `_ClaudeScratch/venv/bin`. Native generation and the four CTests
return0;the separate final evidence collector returns1 because overall frozen acceptance is not met.

### Choice and progress

|Choice|Rating:more stars=better fixture quality|Why|
|---|---|---|
|Uniform-area RIS|🏆 ⭐⭐⭐|Best same-query RGB accuracy in shown fixtures;fresh native result preserves GPU|
|Uniform-area raw8|⭐⭐⭐|Best absolute accuracy shown;68 versus36 counted queries/pixel,not equal time|
|Area-proportional RIS|⭐⭐|Numerically matches GPU;higher absolute error on these fixtures|
|Production promotion|Held|Three frozen per-fixture control checks fail;no waiver or default activation|

**Phase evaluation100%/0% remaining;phase acceptance is held,not completed.** Overall rendering~89%/~11%
remaining,planning estimates. Next complete problem:predeclare dedicated occlusion-sensitivity fixtures that
exercise nonzero affected transport,then verify both visibility operations independently before any promotion.
Do not rewrite this failed historical gate. Renderer wiring,general material coverage,broader scenes/reuse,
physical GPU timing and Windows remain open. Counted queries are not a measured speedup or real-time result.
Production348.046875MiB/defaults/32B descriptors unchanged;packing/editor/vehicle/Jolt deferred. No PR or Actions.
