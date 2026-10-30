# Opt-in C++ facet-area proposal/RNG port

## Frozen protocol — before measurements

- Port shader hash-to-24-bit-uniform mapping,draw scheduling,uniform-facet and area-proportional selection,
  square-root barycentrics,area PDF and world-point construction into an excluded-by-default engine library.
- Use explicit float32 arithmetic in shader order;no fast math or contraction. Prepare validates finite,
  positive representable areas/PDFs and strictly increasing area CDF. Reject invalid input explicitly.
  Degenerate triangles are rejected,not silently dropped or renumbered. No target scoring or tracing port.
- Reuse original fixture constructors and production BVH ordering through source-digested extraction of exact
  function bodies. No rewritten fixture geometry,no GPU-record-derived input areas. Verify original geometric,
  perspective and deformed manifests before comparison;preserve all historical captures and reports.
- Generate589,824 distinct native proposals:three corpora×three fixtures×two measures×4096 seeds×eight draws.
  Compare all1,474,560 retained GPU proposal observations,including raw4/raw8 controls. Compare24-bit RNG exactly,
  barycentrics within2e-6,normalized PDF/point error2e-5. Facet mismatch permitted only within2e-6 of the area
  CDF boundary,max16 observations across corpus. Count and disclose;do not fabricate alternative traced paths.
- Independently recompute Python float32 triangle areas,CDFs,draws,points and PDFs. Draw ordinals must agree;
  floating-point fields use the same fixed tolerances. Proposal mass counts and barycentric first/second/cross
  moments must pass6-standard-error checks plus2e-5;counts checked per fixture/measure,not as independent repeats.
- Negative controls:uniform-facet choice substituted for area choice,omitted square root and wrong measure PDF
  must each fail comparison on every deformed fixture. Keep the previous selector/normalizer unit regression.
- Strict Release and host ASan/UBSan;native capture bytes identical. Display actual newly generated native point
  samples and original triangles,not invented images or fresh transport. No image-equivalence claim for tracing.
- No renderer/default/descriptor integration,memory packing,PR,Actions or physical timing claim.
  Production348.046875MiB remains unchanged. Windows/physical GPU checks are separate unavailable gates.

Thresholds above are frozen before the first corpus measurement;failed gates will be reported,not retuned.

## Result — accepted bounded proposal port,2026-10-30

|Gate|Measured result|Decision|
|---|---|---|
|Strict Release/host ASan+UBSan|Two CTests and15,024 focused checks/build|Pass|
|New proposal units|193:hash goldens,wrap,invalid geometry,lost CDF support|Pass|
|Previous selector/normalizer regression|14,831 checks/build|Pass|
|Fresh native proposal generation|589,824 distinct proposals from independently constructed triangle geometry|Pass|
|Retained GPU observations,including raw controls|1,474,560;zero differing facet ordinals|Pass|
|24-bit draw variates and selection variates|Native/Python exact;retained GPU RIS selection variates exact|Pass|
|Native/Python barycentric coordinates/PDF|Maximum normalized error0|Pass|
|Native/GPU barycentric coordinates/PDF|Maximum error0 on this corpus|Pass|
|Native/GPU reconstructed world point|Maximum normalized error0 on this corpus|Pass|
|Native moment/count laws|90 moment checks and288 facet-count bins pass|Pass|
|Three wrong-algorithm controls|Each detected on all3 deformed fixtures|Sensitive|
|Release/sanitized output parity|Proposal,fixture and exact-constructor bytes identical|Pass|

No sampler or threshold change was needed after corpus measurement. Before the first audit,the capture driver
was corrected to use each corpus's own existing seed constant and the preparation guard was strengthened to
reject CDF saturation before the final triangle. Final verification additionally records all three raw uniform
variates per proposal and checks wrapping-ordinal hash goldens;the core sampling arithmetic remained unchanged.

### Engine contract

- New target `FrontierFacetAreaProposals` is `STATIC EXCLUDE_FROM_ALL`,C++17,with floating-point contraction
  disabled and no fast math. Explicitly link it and include `Engine/Rendering/FacetAreaProposals.h`.
- `Prepare` caches shader-order float32 area CDFs and both PDFs from caller-owned ordered triangle corners.
  It allocates only on the cold path;`Draw` allocates nothing. Draws preserve triangle ordinals and use the
  shader's final-triangle CDF fallback. No area/density is inferred from retained GPU records.
- Degenerate/nonfinite triangles,overflow/underflow,nonrepresentable PDFs and lost float CDF support are rejected.
  Failed preparation invalidates the previous distribution. A rejected proposal is not a valid zero-energy path.
- RNG uses explicit uint32 wrapping arithmetic and high24 bits. The three experimental seed domains are
  geometric0x739ab531,perspective0x4a615e23,deformed0x5e92b341,with unchanged fixture mixing and lane scheduling.
- The earlier independent-double deformed audit allowed2 near-boundary ambiguities;this shader-order float32
  port has0. Original captures,seeds,geometry and acceptance thresholds were not changed to obtain that result.

### Evidence and reproduction

- [Actual native point samples](Images/FacetAreaProposalPortPoints.png):820 newly generated C++ points per
  deformed fixture/measure,projected XZ over original triangle outlines. Visually inspected:uniform-facet
  sampling gives small facets more points per unit area;area-proportional sampling concentrates on large
  receiver/wall facets. These are point plots,not shaded transport frames or a new quality measurement.
- [All nine corpus/fixture comparisons](Results/FacetAreaProposalPort-comparison.md) and
  [18 distributions/90 barycentric moments](Results/FacetAreaProposalPort-moments.md).
- [Parity summary](Results/FacetAreaProposalPort-parity.txt),[Release log](Results/FacetAreaProposalPort-release.txt),
  [sanitized log](Results/FacetAreaProposalPort-sanitized.txt),
  [manifest](Results/FacetAreaProposalPort-evidence-hashes.txt).
- `Results/FacetAreaProposalPort-fixtures.bin.gz`:nine fixtures×16 triangles×nine little-endian float32 corners;
  order corpus,fixture,triangle,corner,axis. Corpus0=geometric,1=perspective,2=deformed.
- `Results/FacetAreaProposalPort-native.bin.gz`:48 bytes per proposal,`<2I10f>` containing
  `(index,accepted,u,v,pdf,pointX,pointY,pointZ,selectionUniform,choice,first,second)`.
  Order corpus,fixture,measure,packet,draw;measure0=uniform-facet,1=area-proportional;4096 packets,eight draws.
- Seven exact fixture/vector function bodies are extracted from existing headers to a scratch-only header by
  `Code/FacetAreaProposalScenes.py`;the real production BVH ordering is reused. No handwritten fixture clone,
  geometry stub,Vulkan submission or shader compilation is involved. Source and generated-header hashes are logged.
- All original geometric/perspective/deformed working-tree manifest hashes pass. The earlier arithmetic-port
  manifest is verified against parent9dcfb7b,and every working file still matches except the intentionally
  extended engine CMake. The original report/evidence is not overwritten to conceal this source evolution.

Prerequisites:C++17 library/C++20 fixture exhibit,CMake/CTest≥3.20,Python3,Pillow. Tested on Linux with
GCC12.2.0,CMake3.31.6,Python3.11 and Pillow13.0.0;no Linux executable is shipped. Windows/MSVC and physical
GPU execution remain untested. The runner accepts `CMAKE`/`CTEST` overrides;default sandbox tool paths use
`_ClaudeScratch/venv/bin`. The standalone project is `Exhibits/LightCompute/FacetAreaProposalPort`.

```sh
bash Exhibits/LightCompute/RunFacetAreaProposalPort.sh
SANITIZE=1 bash Exhibits/LightCompute/RunFacetAreaProposalPort.sh
_ClaudeScratch/venv/bin/python Exhibits/LightCompute/Code/FacetAreaProposalEvidence.py
```

### Choice and remaining scope

|Choice|Rating:more stars=better fixture quality|Why|
|---|---|---|
|Uniform-facet area proposal|🏆 ⭐⭐⭐|Prior same-query RGB winner;new C++ proposals preserve captured GPU inputs|
|Area-proportional proposal|⭐⭐|Also exact on this corpus;prior held-out RGB error is higher than uniform|
|Higher-query uniform raw8|⭐⭐⭐|Prior best absolute RGB accuracy;not a same-query/time comparison|
|Production enablement|Deferred|Target/proxy score,transport integration and physical timing remain unaccepted|

This phase validates proposal generation,not a fresh rendering-quality improvement. GPU world points are
reconstructed from retained barycentrics and source triangles,not separately captured GPU position buffers.
Moment gates use32768 samples per fixture/measure;correlated methods and measures are not pooled as independent
replicates. The six-standard-error checks are diagnostics of these seed domains,not a universal RNG theorem.
Raw4/raw8 proposal observations are checked,not newly rendered or claimed faster. Scores,BRDF evaluation,
compensated geometry factors,visibility,temporal/spatial history and renderer wiring are not ported here.

**Progress:** bounded proposal/RNG phase100%/0% remaining;overall rendering~86%/~14% remaining,planning estimates.
Next complete problem:port and independently validate the unoccluded target/proxy score and compensated geometry
factor before fresh tracing and renderer integration. Physical timing,broader scenes and reuse remain separate.
Production348.046875MiB/defaults/32B descriptors unchanged;packing/editor/vehicle/Jolt deferred. No PR or Actions.
