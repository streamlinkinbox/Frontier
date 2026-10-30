# Opt-in C++ facet-area reservoir selector port

## Frozen protocol — before measurements

- Port only the fresh two-candidate h/q selector and W/(2*hSelected) normalizer into a reusable engine API.
  Geometry sampling,RNG,ray tracing,temporal/spatial reuse and renderer wiring are not part of this primitive.
- Preserve the shader convention:choose first iff uniform<firstWeight/sumWeight. Do not silently substitute
  the existing streaming reservoir's second-candidate random convention.
- M is always2,including zero-target/null proposals. Both zero weights produce a canonical empty result.
- Reject nonfinite/negative targets or densities,positive target with zero density,invalid uniform variates,
  and unrepresentable weights/sums/normalizers. Rejection is explicit,not silently treated as a null sample.
- Release and host ASan/UBSan C++17,strict warnings;focused unit tests plus exact algebraic expectation cases.
- Replay all RIS pairs from geometric,perspective and deformed retained GPU captures:294,912 pairs total.
  Verify original manifests before use. Compare independent Python algebra and native results with GPU
  selected ordinals,weight sums and normalizers. Fixed normalized2e-5;selector margin2e-6,max16 across corpus.
- Reconstruct CPU images only when the port selects the GPU-traced path. A differing near-boundary choice is
  counted and not fabricated from an untraced path. Full image equivalence requires zero such differences.
- Reweight the retained selected-path RGB integrand with the native normalizer;compare18 CPU RGB images
  against retained GPU RGB and finite reference banks. This is capture-driven replay,not fresh CPU/GPU tracing.
- Native image MSE/cross-reference MSE must not increase by more than1e-10 relative to the corresponding GPU
  estimator;image max absolute channel error≤2e-5. Retain same-query raw4 and higher-query raw8 controls.
- Wrong normalizer omits density;must fail at least one RGB mean gate in both RIS types on deformed fixtures.
- Engine library is excluded from the default build unless explicitly linked. Renderer defaults,32B descriptors
  and production348.046875MiB remain unchanged. No packing,PR or Actions. No physical speedup claim.

Any failed equivalence gate is retained,not repaired by changing thresholds or the sampler after measurement.
This accepts a bounded C++ operator port only,not the complete area sampler or a production renderer.

## Result — accepted bounded operator,2026-10-30

|Gate|Measured result|Decision|
|---|---|---|
|Strict C++17 Release and host ASan/UBSan|14,831 focused checks and one CTest per build|Pass|
|All three original capture manifests|Every listed hash matches before extraction|Pass|
|Geometric/perspective/deformed corpus|294,912 pairs;zero differing GPU/native selected ordinals|Pass|
|Independent Python double weights/normalizers|Maximum normalized error0 for both|Pass|
|GPU weight sum/normalizer parity|Maximum normalized errors7.34015e-8/1.51959e-7|Pass|
|Release/sanitized native replay|Output bytes identical|Pass|
|18 RGB image equivalence gates|Maximum absolute channel error6.44805e-10|Pass|
|Ordinary and cross-reference MSE preservation|18/18 within frozen1e-10 allowance|Pass|
|Reference RGB means|54/54 pass unchanged6*SE+2e-5 gates|Pass|
|Wrong density-omission normalizer|Fails3/3 deformed fixtures for each RIS type|Sensitive|

Added explicit normalizer-overflow and underflowed-probability/zero-uniform unit cases before final rerun;
no implementation changes or acceptance-threshold changes were needed after corpus measurement.

The target `FrontierFacetAreaReservoir` is `STATIC EXCLUDE_FROM_ALL`,not linked by the renderer. Consumers
explicitly link it and include `Engine/Rendering/FacetAreaReservoir.h`. It preserves the first-ordinal draw
orientation rather than changing the existing streaming reservoir. The exponent-based normalizer avoids
premature overflow of twice the target and premature underflow of half the sum. Invalid/unrepresentable
results remain explicit rejections,distinct from valid all-zero pairs. A positive first weight retains its
zero-uniform endpoint even if the floating-point probability ratio underflows to zero.

GPU weight sum is inferred from the recorded normalizer and selected target,not a separately captured sum.
The native sum also matches independent Python h/q algebra exactly on this corpus;the two comparisons are
not independent new GPU observations.

### Evidence and reproduction

- [Actual comparison sheet](Images/FacetAreaPortComparison.png),visually inspected at native output scale.
- [18-case RMSE/control comparison](Results/FacetAreaPort-comparison.md):corpus0=geometric,1=perspective,
  2=deformed;arm0=uniform-area RIS,1=area-proportional RIS. All18 images cover both proposal arms and all
  three corpora;the sheet shows only uniform/deformed. PNGs are display assets,not the metric inputs.
- [54 RGB residual/SE/gate rows](Results/FacetAreaPort-means.md),[parity summary](Results/FacetAreaPort-parity.txt),
  [Release build/test log](Results/FacetAreaPort-release.txt),[sanitized log](Results/FacetAreaPort-sanitized.txt),
  [SHA256 manifest](Results/FacetAreaPort-evidence-hashes.txt).
- Retained `Results/FacetAreaPort-pairs.bin.gz`:five little-endian doubles per pair `(h0,q0,h1,q1,u)`.
  `Results/FacetAreaPort-native.bin.gz`:four little-endian uint32 plus two doubles per pair
  `(selected,accepted,M,reserved,weightSum,normalizer)`. Order:corpus,fixture,arm,packet,lane.
- Reproduce with C++17 compiler,CMake/CTest≥3.20,Python3 and Pillow. Local sandbox uses CMake3.31.6,
  GCC12.2.0,Pillow13.0.0;no Vulkan installation is needed for this arithmetic replay.

```sh
bash Exhibits/LightCompute/RunFacetAreaPort.sh
SANITIZE=1 bash Exhibits/LightCompute/RunFacetAreaPort.sh
_ClaudeScratch/venv/bin/python Exhibits/LightCompute/Code/FacetAreaPortEvidence.py
```

The runner accepts `CMAKE`/`CTEST` overrides;its default sandbox paths are under `_ClaudeScratch/venv/bin`.
For Windows,use the standalone CMake project `Exhibits/LightCompute/FacetAreaPort`,then build and run CTest;
Windows/MSVC and physical GPU execution were not tested in this Linux sandbox. No Linux executable is shipped.

### Visual comparison and choice

|Choice|Rating|Why|
|---|---|---|
|Uniform-area RIS|🏆 ⭐⭐⭐|Best same-query absolute RMSE on all three shown fixtures;C++ preserves GPU result|
|Uniform-area raw8|⭐⭐⭐|Lowest absolute RMSE in shown fixtures;68 versus36 counted queries including primary|
|Area-proportional RIS|⭐⭐|Also passes port parity;higher absolute RMSE than uniform on these fixtures|
|Production enablement|Deferred|Full geometry/RNG/tracing port and integration/timing acceptance remain open|

Visual inspection:the C++ and retained GPU columns have the same speckle and geometry in all three rows.
Raw4 is visibly noisier in the first two;the complex third fixture does not support a strong visual uniform-RIS
quality-win claim. Raw8 retains slightly lower measured error than uniform RIS in all three shown fixtures.
The sheet uses identical linear exposure64,32x32 pixels enlarged without filtering;finite reference images
are retained GPU reference banks,not an independent external renderer.

**Scope limit:** selected-path RGB integrands are recovered from the retained GPU contribution divided by its
normalizer,then reweighted by C++. This tests the ported arithmetic,not geometry generation,visibility,BRDFs,
RNG or independent fresh CPU rendering. No timing/speedup,temporal reuse or arbitrary-scene acceptance claim.
No production defaults,32B descriptor layouts or348.046875MiB allocations changed;packing remains deferred.

**Progress:** bounded operator phase100%,0% remaining;overall rendering~84%/~16% remaining,planning estimates.
Next complete problem:port the area proposal/RNG generation and validate it independently before renderer wiring.
Then fresh tracing/integration,physical GPU timing and broader-scene/temporal acceptance remain separate gates.
