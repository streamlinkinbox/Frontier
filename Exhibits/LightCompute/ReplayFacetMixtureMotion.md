# Moving textured facet-mixture validation

This exhibit checks the opt-in uniform/power facet mixture, GGX direction proposal and balanced MIS
against separately seeded, four-hit CPU paths through moving rough-textured geometry. It adds no
production/editor integration and does not change the Engine shader. Vulkan runs on **SwiftShader CPU**,
not a physical GPU.

## Fixture

A `64 × 64` room contains two unequal-power radiant triangles, a rough-textured object and floor. The
camera moves `0.07` units and the rigid object rotates `0.12` radians. Texture palette, roughness and
metallicity change; the two radiant triangles translate `0.30` units in total and exchange their
`1 : 0.08` powers. A blocker is inserted, then removed as the sources move back and their powers are
restored. The triangle BVH is rebuilt after translation without replacing texture-coordinate `w`
components. The 30 object texture-coordinate vertices agree after inverse rigid transformation within
`1.20e-7`.

For each state, independent double-precision primary rays and atlas calculations check all 4,096 pixel
guides. Each state has 3,840 hits; maximum position/normal/albedo errors were respectively
`3.82e-6`, `5.97e-8` and `7.16e-7`. This is a primary-guide check, **not** independent GI integration
for every pixel. The numerical GI probes are object pixel `(28,38)` and floor pixel `(46,50)`.

For each probe, three separately seeded CPU references trace 240,000 paths each in the moved, blocked
and reopened scenes. The reference uses `mt19937_64`, cosine directions, brute-force triangle
intersections and independently evaluated rough BRDF/texture calculations rather than GPU BVH
traversal, light sampling or MIS. It starts from the independently calculated primary receiver and
uses the same finite float scene inputs. GPU batches use paired frame seeds for fresh uniform and
mixture estimates. Both use spatial reuse, GGX directions and balanced MIS; only their facet proposal
differs. Three uniform/cosine history submissions then precede the first transition into the
mixture/GGX proposal. Reuse retraces the sequences after each change.

## Release results

The 96-batch release run passed all 30 RGB comparisons (two probes × five methods/states × three
channels). `±` below denotes sample standard error, not a confidence interval. For each comparison,
the assertion allows `6 × hypot(CPU SE, batch SE) + 1% × |CPU mean| + 0.0001`. The largest raw
CPU/GPU difference was approximately `1.78` nominal combined SE; the standard-error calculation
does not prove the pseudorandom batches are independent.

| Green channel | CPU reference | Vulkan estimate |
|---|---:|---:|
| Object, moved; fresh uniform | `0.0137334 ± 0.0000926` | `0.0131631 ± 0.0006070` |
| Object, moved; fresh mixture | `0.0137334 ± 0.0000926` | `0.0143804 ± 0.0009197` |
| Object, moved; reused mixture | `0.0137334 ± 0.0000926` | `0.0143766 ± 0.0009631` |
| Object, blocked; reused mixture | `0.00921086 ± 0.00002590` | `0.00856304 ± 0.00046254` |
| Object, reopened; reused mixture | `0.0215638 ± 0.0002213` | `0.0219816 ± 0.0007029` |
| Floor, moved; fresh uniform | `0.142289 ± 0.003057` | `0.152391 ± 0.007395` |
| Floor, moved; fresh mixture | `0.142289 ± 0.003057` | `0.146020 ± 0.005910` |
| Floor, moved; reused mixture | `0.142289 ± 0.003057` | `0.136282 ± 0.005540` |
| Floor, blocked; reused mixture | `0.0147496 ± 0.0008445` | `0.0134296 ± 0.0021920` |
| Floor, reopened; reused mixture | `0.143791 ± 0.003638` | `0.134163 ± 0.005335` |

At least one channel shows each of the CPU-reference occlusion and relighting effects by more than
six nominal combined CPU SE. Transition temporal accepts total `351,552 / 348,768 / 344,736`;
spatial links total `4,352,256`. Both selected probes retain a represented count of two in all
`192 / 192 / 192` transition observations. Each transition retains history with cap one and no
reset; this is not a general reprojection test.

The fresh mixture/uniform **batch standard-deviation** ratios for RGB were `0.959 / 1.515 / 1.000`
at the object and `0.671 / 0.799 / 0.793` at the floor. In particular, the object green channel
became noisier. Paired seeds do not establish statistical independence, and these ratios do not
imply a universal variance, convergence or equal-time improvement. The floor blue mixture running
means at 32/64/96 batches were `0.039259 / 0.039141 / 0.039004`, with sample SE
`0.002941 / 0.002087 / 0.001869`; the independent reference was `0.035758 ± 0.001140`.
A handful of running means is not a monotonic-convergence proof. A later
[disjoint-seed repeat](ReplayFacetMixtureMotionRepeat.md) compares fresh and reused samples and exposes
a rigid-red uniform/reference discrepancy that needs further investigation.

The shorter 8-batch smoke test also passed in release, but is not a convergence measurement. The
full release and ASan/UBSan host runs both exited successfully and their printed numerical diagnostics
match. External Vulkan and Slang shaders are not sanitizer-instrumented. Full numerical details, RGB
channels, source hashes and primary-guide statistics are in the
[release log](Results/ReplayFacetMixtureMotion-release.txt) and
[sanitized-host log](Results/ReplayFacetMixtureMotion-sanitized.txt); the
[smoke log](Results/ReplayFacetMixtureMotionSmoke-release.txt) is retained separately.

After extending the rough-texture submission helper, its disabled-mixture path was also rerun against
the prior shader checkpoint `0d5af89`: 20 images across uniform/power facets and cosine/GGX proposals
had all `655,360` float components bit-identical, with eight matching history records. Both release
and sanitized hosts passed. This compares the current helper with the two shader versions; it is not
the stopped full-gallery recapture. [Release parity](Results/ReplayFacetMixtureMotionPriorParity-release.txt) ·
[sanitized parity](Results/ReplayFacetMixtureMotionPriorParity-sanitized.txt).

## Limits and reproduction

This bounded opaque room and two radiance probes do not establish general unbiasedness, independent
shader random streams, complete occlusion coverage, deforming-mesh reprojection, editor integration,
physical-GPU performance or completed ReSTIR GI. The [moving-light gallery](ReplayFacetMixtureMotionCaptures.md)
shows the same scene changes at another resolution and with different seeds; the numerical probes do
not validate every gallery pixel. The [older mixture gallery](ReplayFacetMixtureCaptures.md) uses
larger camera/object motion but did not translate the radiant triangles.

```sh
# Structural smoke test only
CHECK=--replay-facet-mixture-motion-smoke Exhibits/LightCompute/RunReplayFacetMixtureMotion.sh

# Full validation (240,000 paths for each of six references, 96 GPU batches)
Exhibits/LightCompute/RunReplayFacetMixtureMotion.sh
SANITIZE=1 Exhibits/LightCompute/RunReplayFacetMixtureMotion.sh

# Previous-shader default-off regression (requires git checkpoint 0d5af89)
CHECK=--replay-facet-mixture-parity RESULTS=Exhibits/LightCompute/Results/ReplayFacetMixtureMotionPriorParity-release.txt Exhibits/LightCompute/RunReplayFacetMixture.sh
CHECK=--replay-facet-mixture-parity SANITIZE=1 BUILD_JOBS=1 RESULTS=Exhibits/LightCompute/Results/ReplayFacetMixtureMotionPriorParity-sanitized.txt Exhibits/LightCompute/RunReplayFacetMixture.sh
```

Prepare the pinned tools using `python3 Exhibits/LightCompute/Code/Prepare.py`. The result files
contain source hashes. `VULKAN_TOOLS`, `SLANGC`, `VULKAN_LIBRARY`, `CMAKE`, `BUILD_JOBS` and
`RESULTS` may select alternate prepared tools or output paths.
