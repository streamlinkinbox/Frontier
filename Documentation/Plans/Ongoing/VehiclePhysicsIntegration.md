# Vehicle Physics Integration — Jolt adapter, three carcass solvers, supercharger, WorldRegister, inspector

> Working plan for `arena/cde77209-frontier`. Base: `arena/88b72faa-frontier` (editor tree) merged with the verified
> tyre-physics line `arena/5ec53579-frontier` (`Exhibits/VehiclePhysics`, `Frontier/Engine/Physics`, 29/29 checks).
> References: VehicleHMI demo (JS sources under `Experimental/VehicleHmiReference/` and
> `Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/`), Project Drive C++ core
> (`Experimental/ProjectDriveReference/`, production port `Frontier/Engine/Physics/ProjectDrive/`), GRIT
> `VehicleFramework` specifications (`SuperchargerSpecifications.h` et al.), the VehicleHMI inspector prototype
> `Experimental/VehicleInspector/` (`arena/ca09b37f-frontier`, SultanAladin/Frontier).

## Scope

| # | System | Content | Verification |
|---|---|---|---|
| 1 | Physics port | `Frontier/Engine/Physics/**` + exhibit suite merged from the tyre line | 29/29 Phase-7 checks rerun here |
| 2 | `PhysicsAdapter` / `JoltAdapter` | Backend-neutral rigid-world interface; Jolt 5.2.0 backend; `NullPhysicsAdapter` headless fallback. Fractures stay **static bodies with simple bound colliders** (box/sphere/hull) — no fracture dynamics yet | `PhysicsAdapterValidation` |
| 3 | `PhysicsThread` | Dedicated physics thread, fixed-Δτ substep accumulator (vehicles substep internally at 480 Hz, world slices at a configurable cadence), snapshot publication via `SnapshotExchange` | determinism + step-count + lifetime checks |
| 4 | Supercharger | `InductionCharger` unified forced induction: NA / first-order turbo (demo parity) / Roots / TwinScrew / Centrifugal; boost curve, bypass valve, parasitic crank drag; opt-in on the demo solver, stock path stays bit-exact | `InductionChargerValidation` |
| 5 | Tyre pressure + temperature | Per-corner readings surfaced into `PhysicsWheelSnapshot` (pressure Pa, tread/carcass/gas K, tread remaining, grip) and the inspector; gas law, pressure grip sensitivity and deflection-vs-pressure checked | `TyrePressureTemperatureValidation` |
| 6 | Three carcass solvers on the vehicle | Spring (load carrier), XPBD (84-node lattice + 160-node projection), 3D FEM (Hex8 128-cell corotational); **per-wheel** selection (`AssignWheelCarcassModel`) | settle / drive checks + `TyreCategoryValidation` |
| 7 | `WorldRegister` | Tracks every world object (scene records + physics bodies + vehicle proxies); markdown snapshot export — stored as `a.md` for now | `WorldRegisterValidation` |
| 8 | Vehicle inspector | `VehicleConfiguration` + `VehicleRecord` + `VehicleInspector` (C++ `RecordInspector`, 3 tabs), sections per ca09b37f incl. supercharger, per-corner carcass/pressure/wear, telemetry strip | `VehicleConfigurationValidation` round-trip |
| 9 | Exhibit | `Exhibits/VehiclePhysicsStudio/` — all validations + drive animation: launch → slalom → brake over cones/wall, tyres deforming in all three categories side-by-side, GIF evidence | `RunVehicleStudio.sh` → `Results/` + `Visuals/*.gif` |

## Design decisions

- Demo `VehicleSolver` stays bit-exact for the stock induction path; supercharger is opt-in (`AssignInduction`).
- Existing `CarcassModel` API (pre-dates the naming ruling) is preserved; editor-facing selection uses
  `TyreCarcassCategory { Spring, XpbdLattice, XpbdProjection, FemSolid }`.
- Jolt fetched once into ignored `_ClaudeScratch/jolt/` (pinned tarball, never committed); Jolt-free builds use
  `NullPhysicsAdapter` and report the Jolt checks as skipped, never as passed.
- `PhysicsThread` owns the adapter world and the vehicle list; consumers read `PhysicsFrameSnapshot` triple-buffer
  copies. No code outside the adapters touches Jolt.
