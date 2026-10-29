# Non-overflow spot and IES light cells

This isolates the **analytic-light** uniform cell lists in the LightCompute validation host. A 128×128
receiver grid, eight remote point lights, an inactive overlapping entry and one active test source exercise
three cases: spot without IES, point with a synthetic asymmetric IES profile, and spot with that profile.
The overlapping inactive entry deliberately triggers the capacity-one fallback; it is still assigned to cells.
The tests use SwiftShader CPU Vulkan, not a hardware GPU or the editor.

| Check                                     | Result                                                                |
| ----------------------------------------- | --------------------------------------------------------------------- |
| Normal capacity `16`, each source type    | `14,336` vs `163,840` visits; **zero** fallbacks; identical float4s   |
| Moved to a cell boundary (`x=4`)          | Same exhaustive/cluster equality and zero fallbacks                   |
| Moved near either domain edge (`x=±15.5`) | `11,264` visits vs `163,840`; zero fallbacks; identical readbacks     |
| Capacity `1`                              | `4,096` overflow queries fell back to exhaustive; identical readbacks |
| Two receivers outside the grid            | Exactly two fallback queries; identical readbacks                     |
| Panel open/blocked, source on/off         | Controls changed all cases; 60 main-source samples shadowed           |
| Rotate synthetic IES basis                | Point and spot changed; both still match native lighting              |
| GPU vs native reference                   | Worst absolute RGB error over all tested cases `1.23e-5`              |

The independent geometric shadow oracle intersects an analytic rectangle; it does not reuse the shader's triangle
or BVH intersection. Source arrival, spot attenuation, GGX BRDF and IES interpolation are compared with separate
native double-precision implementations and then packed into float expected channels. This checks agreement with
an implementation of the same model, not a measured luminaire. The IES profile is authored for this fixture; it
is not evidence of real-world candela calibration or exhaustive LM-63 format support. Candidate visits are
algorithmic counters, **not timings or a performance claim**.

Release and ASan/UBSan host runs pass. The external SwiftShader driver is not sanitizer-instrumented. These tests
exercise deterministic direct summation, not temporally reused direct reservoirs or general moving-scene/editor
integration. The domain, receiver arrangement, range, blocker and light counts are deliberately bounded.

## Emissive geometry is separate

The tested cell lists hold analytic directional, point, spot and IES lights, **not emissive triangles**. The
existing [emissive-area comparison](README.md#emissive-triangle-importance-sampling--executed) uses a separate
global cumulative triangle distribution and compared selected probes with a native reference. The newer bounded
replay can sample emissive facets separately. Neither constitutes clustered emissive-light selection; a scalable
emitter structure, PDF/support validation, dynamic updates, double-counting policy and hardware/editor tests remain
open.

```bash
bash Exhibits/LightCompute/RunClusterSpotIes.sh
SANITIZE=1 bash Exhibits/LightCompute/RunClusterSpotIes.sh
```

[Release results](Results/ClusterSpotIes-release.txt) · [Sanitized results](Results/ClusterSpotIes-sanitized.txt) ·
[Solar regressions](Results/ClusterSpotIes-regression.txt) · [Existing emissive evidence](Results/EmissiveArea.txt) ·
[Renderer evidence](RendererProgress.md).
