# Standalone GPU Hex8 tyre solid

## What runs

`Frontier/Engine/Physics/Shaders/TyreSolidDevice.slang` ports the current VehicleHMI demo's 32-sector,
5-node-per-sector solid. Each independent tyre uses **128 Hex8 cells** whose four inner corners are fixed;
the four outer corners share the 160-particle surface atlas. One invocation sequentially accumulates cell
forces for one tyre, then applies the demo's damping and authored contact target for each substep. This is a
fidelity-first parallelization across tyres, not a performance claim or a fully unconstrained solid.

The reference geometry's corners, material-space gradients and volumes are exposed by the existing CPU
solver and packed into checked Engine footprints in `TyreSolidDevice.{h,cpp}`. The Slang entries and the
existing `TyreSurfaceDevice.slang` use the same 80-byte particle layout as the standalone XPBD-labelled
projection. The existing Vulkan command recorder can submit either pair of reset/advance pipelines, provided
its descriptor layout matches the chosen model. The exhibit runs a solid advance followed by surface
projection in **one Vulkan command buffer**, with a compute barrier and no intervening particle readback.

**This is not integrated into the application or editor.** The Exhibit is the only Vulkan consumer; there
is no actual graphics mesh/presentation test, physical GPU run, tyre/road force solver or vehicle/Jolt
integration. Vehicle dynamics and Jolt remain explicitly deferred.

## Device contract

- Bind set 0 storage buffers: 80-byte commands (0), 80-byte particles (1), 32-byte solid results (2), and
  128 shared 80-byte reference-cell footprints (3). A cell contains four particle indices and four material
  gradients; its first gradient's fourth component holds volume, while the remaining fourth components are
  zero. One atlas describes the fixed demo geometry, not arbitrary tyre sizes or independently varying tyres.
- Use `CompileTyreProjectionCommand` for command guards and `CompileTyreSolidCells` to copy the CPU solver's
  reference geometry. Host/device cell checks reject out-of-range corners, nonfinite or excessively large
  gradients, invalid volumes and nonzero reserved components. They are **bounds checks**, not proof that an
  arbitrary finite cell atlas is geometrically valid or stable. Reset before the first advance.
- `TyreSolidDeviceResult` contains prescribed force, success, mean clamped Jacobian and mean diagonal-stress
  norm, plus the contact-node count. Invalid commands or invalid full-size cell buffers set success to zero and
  leave particles unchanged. Insufficient buffer ranges skip the invocation entirely. The reported vertical
  force is `deflection * (25000 + 0.90 * pressure)` N: a **prescribed demo spring force**, not a reaction
  integrated from the solid stresses.
- `RecordTyreProjection` accepts the solid pipelines/descriptor set as caller-owned Vulkan objects. Pipeline
  layout needs the four-byte compute push constant at offset zero for the exact tyre count. The caller owns
  pipeline creation, GPU memory, valid descriptors, command-write availability, reset/advance/surface barriers,
  submission and object retirement. The validation does not supply an Engine simulation scheduler.

## Verification

Release and ASan/UBSan-host executions both passed on **SwiftShader CPU Vulkan** (device type 4); the driver
and GPU shader are not sanitizer-instrumented. Direct Slang-generated SPIR-V is passed to Vulkan without
`wgpu`/Naga translation. The unchanged demo source is the trajectory reference; the separate CPU implementation
reproduces that source, so agreement with both is **not two independent physical validations**.

| Check | Result |
|---|---:|
| Reference trajectories | 12 demo cases × 8 steps, repeated across 33 tyres (two workgroups) |
| Particle snapshots compared with JavaScript and Engine CPU | 42,240 |
| Worst local / world position vs JavaScript | `0.000101864338 m` / `0.000100295991 m` |
| Worst local / world position vs CPU | `0.000101864338 m` / `0.000100314617 m` |
| Worst prescribed-force error vs JavaScript / CPU | `0.001953125 N` / `0.00089931488 N` |
| Worst mean Jacobian / mean stress vs JavaScript | `4.62233e-6` / `0.687864` |
| Contact counts; 50,688 pinned coordinates; duplicate tyres | Exact for all recorded cases |
| Surface projection from the resident solid particles | 4,096 probes; max position `5.96e-8 m`, normal-component `2.35e-5` vs CPU projection on the same GPU field |
| Invalid commands, cells and short buffer; partial dispatch | Rejected or skipped without unintended particle/result mutation |

The roughly `0.1 mm` maximum trajectory error has not been attributed to a particular precision or
transcendental operation. Thresholds are `0.3 mm` for coordinates, `0.02 N` for prescribed force, `0.001`
for mean Jacobian and `1.0` for mean stress in the demo's model units. These are comparison tolerances, not
engineering accuracy claims. The visible vertex functions are still tested through a compute probe; the
separate vertex entry was compile-only in the [projection exhibit](TyreProjectionDevice.md).

### Independent constitutive check

The very same Engine Slang stress function used during force accumulation was also evaluated for 64 positive,
unclamped-J deformation gradients: identity, rigid rotation, dilation, uniaxial compression, shear and
bounded general deformations. An independent host oracle used central finite differences (`1e-5`) of

`W(F) = μ/2 (||F||² − 3) − μ log(det F) + λ/2 (det F − 1)²`

and compared its numerical derivative against the nine GPU stress components. Maximum absolute discrepancy
was `0.008467` in model stress units; max determinant discrepancy `1.53e-7`; identity and tested rotation
returned zero stress on this execution. This tests the constitutive relationship **away from the shader's
Jacobian clamps**, not its clamped/inverted-cell behaviour, contact assembly, dynamics or measured rubber.
No measured tyre dataset or independent physical full-wheel result was used.

The release and sanitized trajectory, 4,096 surface results and 64 stress samples have identical SHA-256
`6df6bb954411ef4a1cb4a85fb1d47cc143581e46f76b49972bcd501700e7696c`.

## Reproduce and remaining work

```bash
bash Exhibits/VehiclePhysics/RunTyreSolidDevice.sh
SANITIZE=1 bash Exhibits/VehiclePhysics/RunTyreSolidDevice.sh
```

[Release log](Results/TyreSolidDevice-release.txt) · [Sanitized log](Results/TyreSolidDevice-sanitized.txt) ·
[XPBD regression, release](Results/TyreSolidProjectionRegression-release.txt) ·
[XPBD regression, sanitized](Results/TyreSolidProjectionRegression-sanitized.txt). Both projection regressions
exited successfully and retained their previous readback digest.

A separate [curved-shell GPU port](TyreMembraneDevice.md) now covers the demo's other continuum model, with
important independent mechanics counterexamples. Standalone spring-contact dynamics, long-duration/convergence
analysis, measured-tyre calibration, physical GPU/Windows testing, a real graphics consumer and editor/scene
scheduling remain open.
The prior hard-drop convergence failure remains unresolved; no real-time or physical accuracy claim is made.
