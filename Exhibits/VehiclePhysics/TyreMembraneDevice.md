# Standalone GPU curved-shell tyre

## Scope

`Frontier/Engine/Physics/Shaders/TyreMembraneDevice.slang` ports the retained VehicleHMI demo's
`ShellContinuumWheel`: a 32-sector × 5-ring, 160-particle orthotropic membrane with 128 quadrilateral
patches, a meridional bending hinge and the demo's authored contact target. One compute invocation advances
one tyre sequentially; different tyres can run concurrently. The shader computes patch reference metrics
from its reset/rest particles rather than uploading the demo's precomputed float32 metric array. It uses the
existing checked command/particle/result ABI and `TyreProjectionVulkan` recorder; its result force is the
**prescribed** `deflection * (25000 + 0.88 * pressure)` N, not an integrated tyre-road reaction. The source
class does not calculate a useful mean stress; this port does not invent one.

Release and ASan/UBSan-host checks compile Slang directly to SPIR-V and submit it to **SwiftShader CPU
Vulkan**. The driver and shader are not sanitizer-instrumented. The Exhibit is the only Vulkan consumer:
there is no physical GPU, actual graphics mesh, production simulation-loop or editor integration. There is
no new vehicle-dynamics or Jolt work. This is a source-faithful approximation, not a validated physical
shell solver.

## Tested behavior

| Check | Result |
|---|---:|
| Unchanged JavaScript reference | 12 pressure/deflection/orientation cases × 8 steps, duplicated across 33 tyres |
| Particle snapshots | 42,240; max local/world errors `0.000101984 m` / `0.000100445 m` |
| Prescribed force | Max JavaScript error `0.001953125 N`; analytic formula max `0.000921011 N` |
| Pinned coordinates / duplicate simulation slots | 50,688 / exact |
| Zero-load drift / largest local deformation | `0 m` / `0.0816884 m` in these cases |
| Existing GPU surface functions | 24,576 probes, including the membrane particle buffer in the same command buffer; continuum max position/normal errors versus CPU projection `5.96e-8 m` / `9.33e-5` |
| Count, invalid-command and short-buffer guards | Passed; partial dispatch leaves unrequested tyres byte-identical |

The trajectory comparison is against the unchanged demo, **not** an independent CPU membrane solver or
measured tyre. The approximately `0.1 mm` maximum discrepancy has not been attributed to a specific cause;
the finite coordinate test tolerance is `0.3 mm`. Surface checks compare a CPU projection with the same
GPU-generated particle field, not an actual rendered wheel. A separate vertex entry compiled in the earlier
[projection investigation](TyreProjectionDevice.md) but was not run as graphics.

## Independent mechanics counterexamples

The source model's force rules are not accepted here as a conservative or fully objective physical shell.
[`TyreMembraneMechanicsAudit.mjs`](Code/TyreMembraneMechanicsAudit.mjs) independently evaluates two bounded
counterexamples using the retained demo's reference geometry and the same stated equations:

1. For sector 0's first patch at the model's `220 kPa` reference pressure, hold the patch origin fixed and
   vary independently the lengths of its circumferential and meridional edges. At 10% and 5% stretch,
   finite-difference cross derivatives of the two generalized force terms are `3.59204` and `10.52897`
   in model units, a `65.88%` discrepancy relative to the larger value. A smooth force derived from one
   scalar strain energy would have equal mixed derivatives in these coordinates. This is a specific source
   counterexample, not a measured rubber result or a proof about every configuration.
2. Rigidly rotate the reference ring by `0.3 rad` around its local Z axis, moving even the pinned points for
   this isolated symmetry test. The source's local-Y hinge reports a curvature change of `-0.0130324 m`.
   A rigid rotation alone should not generate bending strain in an objective shell formulation. Real pinned
   boundary conditions may prohibit this motion; the test isolates the hinge rule, not a driving scenario.

These findings are properties of the modeled equations; they are **not** failures of the release/sanitizer
runs. They mean source parity must not be presented as physical validation. The port deliberately preserves
these behaviors instead of silently changing the reference. General constitutive reciprocity, objective
bending, numerical stability, contact mechanics and comparison with measured tyres remain open.

## Reproduce

```bash
bash Exhibits/VehiclePhysics/RunTyreMembraneDevice.sh
SANITIZE=1 bash Exhibits/VehiclePhysics/RunTyreMembraneDevice.sh
```

[Release log](Results/TyreMembraneDevice-release.txt) · [Sanitized log](Results/TyreMembraneDevice-sanitized.txt) ·
[XPBD regression, release](Results/TyreMembraneProjectionRegression-release.txt) ·
[XPBD regression, sanitized](Results/TyreMembraneProjectionRegression-sanitized.txt).
Release and sanitized membrane trajectory readbacks have SHA-256
`b46c6a836a04c17f6e413d35e2fd1b4e17683da71dba5fdffbcb8fafee8fe12b`; both XPBD regressions also passed with
their earlier readback digest. The host oracle's counterexamples do not execute GPU gradients or establish
real-world accuracy.
