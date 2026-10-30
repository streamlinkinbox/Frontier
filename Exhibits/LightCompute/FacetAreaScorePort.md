# Opt-in C++ facet-area target and compensated geometry port

## Frozen protocol — before measurements

- Port the existing shader's compensated area displacement/receiver projection,geometry factor,Lambertian
  unoccluded proxy RGB and positive target floor. Input source shading normal/albedo is caller-resolved;
  general material/texture evaluation,primary intersection,visibility and history are not part of this API.
- Preserve float32 operation order,no fast math or contraction. Explicitly reject nonfinite/negative radiometry,
  invalid barycentrics/nonunit normals,zero/unrepresentable distance and nonrepresentable arithmetic.
  Valid backfacing/zero-energy proxies retain the positive target floor,not invalid rejection.
- Generate the existing589,824 native proposals again;require bytes identical to the accepted proposal port.
  Reuse source-extracted fixture constructors/BVH order. Export source triangles/normals/albedos independently;
  use recorded GPU primary receivers,not a newly claimed primary-intersection port. Original manifests verified.
- Compare589,824 fresh native targets against independent double analytic geometry/score and1,474,560 retained
  GPU target observations,including raw controls. Fixed absolute target error2e-5;geometry/proxy normalized
  error2e-5 against double reference. Synthetic long-double algebra/cancellation checks are also required.
- Compose new native targets/densities with the existing native selector for294,912 pairs. Fixed normalized
  weight/normalizer error2e-5;facet/selected-ordinal differences only within2e-6 of the threshold,max16 total.
  Full18-image replay equivalence requires zero actually differing selected ordinals;never fabricate untraced paths.
- Replay selected retained RGB integrands under native normalizers;54 channel means use unchanged6*SE+2e-5.
  Max CPU/GPU channel error2e-5;ordinary/cross-reference MSE increase≤1e-10 versus corresponding GPU estimator.
  Retain raw4/raw8 comparisons. This is capture-driven arithmetic replay,not fresh transport or a quality win.
- Wrong missing target floor,missing receiver cosine and missing Lambertian pi must fail score parity on each
  deformed fixture. Arbitrary positive scores need not bias the estimator;do not misuse mean gates as score tests.
  A naive large-coordinate point subtraction must fail a synthetic geometry check that compensation passes.
- Strict Release and host ASan/UBSan;previous193 proposal and14,831 selector unit checks remain regressions.
  Native captures must agree bytewise across builds. Show native proxy/target or replay imagery from actual data.
- Excluded-by-default engine library;no renderer/default/32B layout/production348.046875MiB allocation changes.
  No PR,Actions,memory packing or physical speedup claim. Windows/physical GPU execution is not tested here.

Thresholds above are frozen before the first target corpus measurement;failed gates are retained,not retuned.

## Result — accepted bounded score/geometry port,2026-10-30

|Gate|Measured result|Decision|
|---|---|---|
|Release/host ASan+UBSan|Three CTests and16,084 focused checks per build|Pass|
|New score units|1,060,including1,024 long-double geometric comparisons|Pass|
|Previous proposal/selector units|193+14,831 per build|Pass|
|Native proposal regeneration|589,824;all bytes identical to accepted proposal port|Pass|
|Fresh native targets versus retained GPU|1,474,560 observations;maximum absolute target error0|Pass|
|Independent double geometry/proxy/target|Max errors2.04348e-7/9.58309e-9/9.90397e-9|Pass|
|Composed native pair selection|294,912 pairs;zero ordinal differences|Pass|
|GPU weight sum/normalizer parity|Max normalized errors7.34015e-8/1.51959e-7|Pass|
|RGB replay equivalence|18 images;maximum channel delta6.44805e-10|Pass|
|Reference channel means and MSE preservation|54/54 means;18/18 ordinary+cross-MSE gates|Pass|
|Three wrong-score controls|Each detected on all3 deformed fixtures|Sensitive|
|Release/sanitized native captures|Targets,selection outputs and fixture/proposal bytes identical|Pass|

The geometry/proxy errors use `abs(native-reference)/max(1,abs(reference))`;target errors are absolute.
GPU sums are inferred from recorded normalizers and selected targets,not separately captured sum buffers.
GPU references come from the same renderer,not an independent external renderer. Thresholds were not tuned.

### Why compensation matters

|Synthetic case|Naive float32 result|Compensated native result|Independent check|
|---|---:|---:|---|
|Triangle at z2²⁴;true separation1|Rounded separation2|Separation1|Long-double geometry agrees|
|Grazing receiver projection|Rounded projection0|5.9604644775390625e-8|Geometry relative error6.02744e-8|

The API retains subtraction low words and split-product remainders instead of relying on hardware FMA.
Before the first corpus run,the tests were extended with explicit positive-output underflow rejection and
1,024 long-double geometry cases. Later verification added diagnostic numbers,raw-control primary equality
checks,all54 residual/SE rows and PNG byte checks;no sampling arithmetic or frozen acceptance threshold changed.

The missing-floor control fails the exact positive-support requirement on actual zero-proxy samples in each
fixture. Its1e-6 discrepancy alone would fit inside the2e-5 numeric tolerance,so it is not mislabeled a numeric
parity failure. The missing receiver cosine and omitted Lambertian pi controls exceed numeric parity tolerance.
A different strictly positive target can remain unbiased;these controls check faithful scoring,not a claim
that every different score biases the estimator or must fail an RGB mean test.

### Engine contract and limits

- Explicit target `FrontierFacetAreaScore` is `STATIC EXCLUDE_FROM_ALL`,C++17,strict warnings,no fast math or
  floating-point contraction. Include `Engine/Rendering/FacetAreaScore.h`;no renderer links it by default.
- `EvaluateFacetAreaScore` takes source corners,barycentrics,caller-resolved source normal/albedo and receiver
  position/normal/albedo. Light direction/radiance and positive target floor are explicit fields with the
  existing fixture defaults. It returns validity,geometry RGB,unoccluded proxy RGB,target and diagnostic projections.
- Valid backfacing/black surfaces retain the floor. Invalid/nonfinite inputs,nonunit normals,unrepresentable
  distance/geometry/proxy arithmetic and positive-output underflow are explicit rejections,not empty transport.
- Scoring performs no allocation,RNG,material lookup,ray tracing,selection or history access. Source shading
  attributes must already be resolved. The validated fixtures use flat diffuse attributes with no material override;
  arbitrary texture/normal-map/material evaluation is not ported by this API.
- Proposal generation,geometry/score and selection now run as native arithmetic in the validation chain.
  Primary receivers remain recorded GPU metadata. Replayed RGB integrands remain the previously selected and
  traced GPU paths. This is not fresh native primary/visibility transport or a production renderer integration.

### Evidence and reproduction

- [Actual native/GPU target heatmaps](Images/FacetAreaScorePortTargets.png):uniform-area candidate targets,
  averaged over32 proposals per pixel,then mapped through the identical log10 range1e-6..1e-2. Visually inspected:
  all three native/GPU fixture maps coincide,including the wall-region structure in fixture2.
- [Actual RGB replay/control sheet](Images/FacetAreaScorePortComparison.png):finite GPU reference,native
  arithmetic replay,retained GPU RIS,raw4 and raw8. Same32x32 nearest-neighbor enlargement and exposure64.
  Visually inspected:the native/GPU speckle is identical;raw4 is noisier in the first two fixtures. Fixture2
  does not establish a strong uniform-RIS gain. Raw8 retains slightly lower measured absolute RMSE on all3 shown.
- [All18 RMSE/control comparisons](Results/FacetAreaScorePort-comparison.md),
  [54 residual/SE/gate rows](Results/FacetAreaScorePort-means.md),[summary](Results/FacetAreaScorePort-parity.txt),
  [Release log](Results/FacetAreaScorePort-release.txt),[sanitized log](Results/FacetAreaScorePort-sanitized.txt),
  [SHA256 manifest](Results/FacetAreaScorePort-evidence-hashes.txt).
- `Results/FacetAreaScorePort-source-fixtures.bin.gz`:nine fixtures×16 facets×21 little-endian float32 entries,
  ordered A,B,C,NormalA,NormalB,NormalC,AlbedoRGB. Corpus0=geometric,1=perspective,2=deformed;fixture then facet.
- `Results/FacetAreaScorePort-native.bin.gz`:48 bytes per proposal,`<2I10f>` containing
  `(accepted,reserved,geometryRGB,proxyRGB,target,receiverProjection,sourceProjection,squaredDistance)`.
  Order corpus,fixture,measure,packet,draw;two measures,4096 packets,eight draws,589,824 proposals total.
- `Results/FacetAreaScorePort-selections.bin.gz`:32 bytes per pair,`<4I2d>` containing
  `(selected,accepted,M,reserved,weightSum,normalizer)`;same ordering with four consecutive draw pairs per packet.
- Regenerable scratch score intake is26 float32 entries per proposal:source A/B/C,resolved normal,source albedo,
  receiver position,receiver normal,receiver albedo,U,V. Its SHA256 is retained with native output digests.
- Native fixture constructors are reused through the previous exact seven-function source extraction and
  production BVH ordering. All original GPU working-tree manifests pass. Both earlier C++ port manifests are
  verified against their original snapshots;all their working files match except the intentionally extended
  engine CMake. Historical evidence is not rewritten to conceal that evolution.

Prerequisites:C++17 library/C++20 fixture exhibit,CMake/CTest≥3.20,Python3,Pillow. Tested on Linux with
GCC12.2.0,CMake3.31.6,Python3.11,Pillow13.0.0. No Linux executable is shipped. Windows/MSVC and physical GPU
execution remain untested. No Vulkan setup or shader rebuild is required for this retained-capture audit.
The standalone project is `Exhibits/LightCompute/FacetAreaScorePort`;runner `CMAKE`/`CTEST` overrides are supported.
Default sandbox tool paths are `_ClaudeScratch/venv/bin`.

```sh
bash Exhibits/LightCompute/RunFacetAreaScorePort.sh
SANITIZE=1 bash Exhibits/LightCompute/RunFacetAreaScorePort.sh
_ClaudeScratch/venv/bin/python Exhibits/LightCompute/Code/FacetAreaScoreEvidence.py
```

### Choice and next gate

|Choice|Rating:more stars=better fixture quality|Why|
|---|---|---|
|Uniform-area RIS|🏆 ⭐⭐⭐|Prior best same-query RGB accuracy;native arithmetic preserves captured result|
|Area-proportional RIS|⭐⭐|Also passes all score/selection gates;higher prior absolute fixture error|
|Uniform-area raw8|⭐⭐⭐|Best absolute accuracy in shown fixtures;higher query budget,not equal time|
|Production activation|Deferred|Fresh transport,renderer wiring,broader scenes and physical GPU acceptance remain|

**Progress:** bounded target/geometry phase100%/0% remaining;overall rendering~88%/~12% remaining,planning estimates.
Next complete problem:integrate the native arithmetic chain with fresh primary/selected-path visibility transport
and compare actual traced results before renderer wiring. General material coverage,physical GPU timing and
broader-scene/temporal-spatial reuse remain separate acceptance gates. No fresh rendering-speedup claim.
Production348.046875MiB/defaults/32B descriptors unchanged;packing/editor/vehicle/Jolt deferred. No PR or Actions.
