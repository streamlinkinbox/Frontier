# 🧩 VehiclePhysics — Phase 7: three tyre models, vehicle dynamics, Jolt collisions

> **Historical-source correction:** much of this page describes an earlier, incomplete demo checkout. The
> retained full VehicleHMI demo does contain a 160-particle XPBD-labelled carcass, Hex8 solid and continuum
> shell; the separate older 84-particle Engine lattice is not the same implementation. Project Drive is a
> different reference. The later [projection](TyreProjectionDevice.md), [Hex8 solid](TyreSolidDevice.md) and
> [curved-shell](TyreMembraneDevice.md) GPU exhibits execute on SwiftShader CPU Vulkan, not a physical GPU or
> the editor. None proves calibrated real-world tyre physics; the curved-shell audit finds mechanics limitations. Further vehicle-dynamics/Jolt porting remains deferred. Statements below are
> historical exhibit scope, not evidence that all models are integrated in the current application.

See [verified continuation](Continuation.md) for recovered sources, five original demo tests, twelve XPBD
cases, the separate Project Drive audit and historical test-renderer captures.

These historical checks exercised `Frontier/Engine/Physics/` against the reference available at the time and
used it to drive a vehicle in a test host. Run everything (Jolt 5.2.0 is fetched/built once into `_ClaudeScratch/`, node runs the demo's
JS oracles):

```
PYTHON=<pillow python> bash Exhibits/VehiclePhysics/RunVehiclePhysics.sh
```

## ⭐ Historical port inventory and later source corrections

| Model | Demo source | Verdict | Production file |
|---|---|---|---|
| Demo tyre force | `TyreSlipSolver.js` | Reduced demo force model with source comparisons; not the complete Project Drive MF6.1/aligning-moment model | `TyreSlipSolver.{h,cpp}` |
| Carcass spring K = 25 kN/m + 0.9·p + vehicle | `VehicleSolver.js` | Bounded source comparisons; the later hard-drop convergence test fails | `VehicleSolver.{h,cpp}`, `VehicleSpecification.h` |
| Tyre heat / gas law / grip / wear | `TyreThermalSolver.js` | Historical demo-source comparison; smoke emission excluded | `TyreThermalSolver.{h,cpp}` |
| Analytical surface deformation | `VehicleVisual.wgsl ResolveWheelVertex` | Visual vertex projection, not a 3D FEM dynamics solver; later GPU functions are tested separately | `TyreDeformation.h`, `TyreSurfaceDevice.slang` |
| XPBD soft-body lattice | The earlier checkout lacked the source; the restored full VehicleHMI demo has a 160-particle XPBD-labelled projection | The historical 84-particle lattice is a distinct model; the later 160-particle port is [tested separately](TyreProjectionDevice.md) | `TyreCarcassLattice.{h,cpp}`, `TyreContinuum.{h,cpp}` |
| Collisions | `JoltWorld.js` (Jolt WASM loose bodies + chassis proxy) | same architecture on Jolt C++ 5.2.0 | exhibit `Code/JoltScene.cpp` |

Stated deviations of the vehicle port: the driving surface is a caller height function (flat here) instead of the demo's
sculpted course; wheels do not ride loose debris (the demo's `RigidBodyWorld` support query); the 700-point hull is not
assigned, so the demo's sphere hull stands in; scrape/spark bookkeeping (visual only) is omitted. The car is *not*
Jolt-driven — exactly as in the demo, its own 480 Hz solver drives, and Jolt owns loose bodies plus a zero-gravity
chassis proxy that exchanges only collision velocity deltas.

## ✔️ Checks (29/29, see Results.txt)

- **A1–A4** MF6.1 Fx/Fy/combined/relaxation vs `Code/TyreOracle.mjs` running the demo's unmodified JS under node:
  max relative error **1.8e-15** (bit-exact port). **A5** PB-FEM vertices vs a WGSL transliteration: 8.7e-08 m.
  **A6** thermal trajectory (25 kW slide, 8 s): error **0.0** — the conduction/gas-law/grip port is exact.
- **B** Static settle vs the demo's `VolumetricTyrePhysics.test.mjs`: deflection 14.0–14.5 mm (band 10–25), contact
  angle −π/2, 220 kPa, corner loads sum to weight within **0.00 %**.
- **C** XPBD lattice rig vs the spring curve: within **0.75–1.30×** at 220 kPa for 10–40 mm deflection, force ordered
  by pressure, enclosed volume conserved to +0.31 % under 20 mm load. ⚠️ Disclosed residual: at 0 kPa gauge the
  lattice runs ~2× stiff past 20 mm (belt geometric floor; the spring model's flat-tyre regime is softer) — visible
  in `CarcassCurves.png`, not hidden by the tolerance choice.
- **D** The whole car on the lattice carcass: settles at 13.6 mm mean deflection (spring: 14 mm) and drives
  (20.4 m, 12.5 m/s in 3 s WOT).
- **E** Scripted drive on the 1:1 spring model: launch to **36.8 m/s in 7 s** with AMT upshifts and 1.17 bar boost;
  governed slalom (4 yaw sign changes, 0.064 rad peak slip angles); full brake stop; **line-lock burnout** — rear slip
  pegged at the 1.5 clamp while the car stays under 0.12 m/s and rear tread climbs **+78 K** through the thermal
  model; release launches the car.
- **F** Jolt scene: **10/10 cones** displaced/tipped with 12.24 m/s of velocity exchanged back into the car; wall
  impact at 12.3 m/s stopped to 0.24 m/s by the proxy exchange, nose penetration 0.023 m.

## Evidence files

`Results.txt`, `PacejkaCurves.png`, `CarcassCurves.png`, `TyreDeformation.png` (FEM outlines + lattice particles),
`DriveTrace.png`, `JoltScene.png`, and the CSVs they render (`pacejka.csv`, `lattice.csv`, `lattice_particles.csv`,
`fem_outline.csv`, `drive.csv`, `jolt_trace.csv`). Code lives in `Code/` per exhibit convention and links only
production physics + Jolt — no exhibit-local physics.

## 🎞️ Visuals — the actual ControlVehicle, rendered (`Visuals/`, `RenderVehiclePhysics.sh`)

`Code/SceneRender.cpp` is a small software rasterizer (no GPU in the sandbox) that loads the real production
geometry — `ControlVehicleBody/Tyre/Rim.facet` from the VehicleProvingGrounds archive — assembles the wheels
exactly as the demo's `ProjectWheelFacets` does (Y-mirrored right wheels with swapped winding, rim scaled to the
tyre rim radius, rim family 9 → RimBase), shades with the demo's authored `CourseMaterial` linear albedos
(BodyCoat chartreuse, near-black VehiclePlastic flanks, blue-metal Hub accent), and drives the mesh from the live
production physics: body pose and wheel poses from `VehicleSolver`, tyre vertices deformed per frame by
`TyreDeformation.h` (PB-FEM) from each corner's true deflection/pressure/camber, tread tint from
`TyreThermalSolver` surface temperatures, cones/wall from the Jolt scene. Flat lambert+hemisphere+Blinn shading,
planar projected shadow, checker proving ground — deliberately a fast flat render, not the path tracer.

- `Visuals/hero.png`, `hero-rear.png` — settled car on deformed tyres (14 mm @ 220 kPa).
- `Visuals/tyre-nominal.png` / `tyre-flat.png` — same corner at 220 kPa (14.0 mm) and 30 kPa (60.2 mm): the mesh
  follows the carcass spring.
- `Visuals/tyre-lattice.png` — the XPBD lattice's 84 particles (blue crown, amber sidewall) carrying the car at
  13.8 mm, rubber hidden.
- `Visuals/SlalomDrive.gif` — launch → governed 15 m/s slalom → brake, chase camera, live tyre deformation.
- `Visuals/Burnout.gif` — line-lock burnout; the rear tread mesh glows as the thermal solver heats it.
- `Visuals/ConeHits.gif` — Jolt cone row + wall, proxy velocity-delta exchange stopping the car.

Reproduce: `bash Exhibits/VehiclePhysics/RenderVehiclePhysics.sh` (builds Jolt 5.2.0 if absent, renders all
modes, assembles via `Code/AssembleVisuals.py`).

Renderer caveats, stated plainly: smooth tyre shading uses per-vertex averaged normals (demo shades per-corner);
the ground is a camera-snapped tile grid because the rasterizer drops triangles with a vertex behind the camera
(no near-plane clipping); shadows are a single planar projection of the car onto z=0; headlight/taillight
emission is scaled down from the authored HDR values so the flat renderer does not blow out.
