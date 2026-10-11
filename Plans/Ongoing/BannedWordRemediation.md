# Banned-Word & Naming Remediation

Bring authored code into line with `AgenticInstuctions/SKILL-Naming.md` and `SKILL-Formatting.md`. Tiered so we fix
what we own without permanently diverging the copied Frontier engine code from upstream.

## Scope tiers

| Tier | What | Action |
|------|------|--------|
| 1 | Code we authored — the vehicle dynamics layer (`Engine/PhysicalDynamics/Vehicle/*`, GRIT-derived) and the Project-Drive / Project-Tractrix seams | Fix fully (naming + formatting) |
| 2 | Upstream-copied host (`DriveExecution.cpp` derived from `GameExecution.cpp`, `RigidBodySolver.cpp`) | Fix only identifiers **we** added; leave inherited names to avoid diverging from Frontier |
| 3 | Vendor (Jolt, ImGui, Vulkan) | Exempt — vendor spellings are verbatim |

## Clarifications

- 🔴 **`Ordinal` IS banned** (project directive, 2026-09-30 — reverses the earlier reading), alongside `Ordinates`.
  Replace positional-sequence words with the mechanism: `Slot` / `Index` for a countable position, `Number` for a
  monotonic counter, `Sequence` for an ordered workflow. Recorded in `SKILL-Naming.md` (banned addendum + rationale;
  the `SlotOrdinal`/`RecordingOrdinal` examples became `SlotIndex`/`RecordingNumber`).
  - Authored occurrences fixed: `ImageOrdinal` → `ImageIndex` (param, `SwapchainExchange.h`); swatch `Ordinal` →
    `SwatchIndex` (local, `MaterialLevelViewport.cpp`). `Ordinates`: **zero** in the tree.
  - `QueryHousingOrdinal()` (`GameExecution.cpp`, `DriveExecution.cpp`) is a **base-engine API call** — the method is
    defined on `Frontier::ProjectZero::InterfaceTrialSequence`, whose definition is **not vendored in this repo**.
    Renaming the call here would break it, so it is left as-is (Tier-2/external) and flagged for base-engine remediation.
- `Controller`, `Bridge`, `Manager`, `Model` (as a container), `flag`, `state`, `value`, `Config` (shorthand) are the
  high-frequency banned tokens in the authored layer. `Update`, `Get`, `Set` are banned verbs.
- Booleans drop `is` / `has` / `can` / `have` and state the property as a noun phrase (`ThrottleKeyDown`, not
  `bHasThrottle`). `IsKeyPressed` / `IsMouseButtonPressed` are the **engine `InputExchange` API** (Tier 3-adjacent),
  not ours to rename.

## Standing rename decisions

| Retired | Replacement | Reason |
|---------|-------------|--------|
| `VehicleInputController` | `DriverInputIntegrator` | `Controller` banned; it accumulates/ramps input over Δτ (Integrator) |
| `VehicleInputBridge` | `DriverInputExchange` | `Bridge` banned; it crosses the device→engine input edge (Exchange) |
| `InputConfig` | `InputConfiguration` | Zero shorthand |
| `InputDevice` (enum) | `InputDeviceCategory` | Matches engine `…Category` enum convention |
| `Get*` | `Query*` / `Read*` / `Access*` | CRUD verb banned |
| `Set*` (config) | `Assign*` / `Apply*` | CRUD verb banned |
| `Set*Key` / `Set*Axis` | `Forward*Key` / `Forward*Axis` | States the mechanism (forwards a device reading) |
| `Update(dt)` | `Advance(Δτ)` | `Update` banned; delta-time uses the real glyph `Δτ` |
| `k…` / `a…` member prefixes | full PascalCase noun phrases | Zero shorthand, zero `k` prefixes |
| `DrivingModel` / field `Model` | `DrivingScheme` / `ActiveScheme` | `Model` banned; it selects the tyre-force computation scheme |
| `…Tests` / `RunTests` | `…Checks` / `RunChecks` | `Test` banned; a verification suite asserting invariants |
| `TestTrack` | `ProvingGround` | `Test` banned; the domain noun for the circuit a car is exercised on |
| `TestDrive` | `TrialDrive` | `Test` banned; `Trial` is the domain noun (cf. `InterfaceTrial`) |

## Decisions (2026-09-30 session)

- 🔴 **`Test` is banned** (project directive). Replacements as above; recorded in `SKILL-Naming.md`.
- 🔴 **Banner emoji is `📦`; the former puzzle-piece marker is retired** (project directive — "we already used it a
  lot"). It was replaced by `📦` **repo-wide** (14 files incl. FlattenedEngine, shaders, StarFix, governance docs) and
  removed from the `SKILL-Formatting.md` whitelist. `📦` is the one module marker for both the file-header banner and
  the first `///` line; the retired glyph must not be reintroduced anywhere (source, docs, or chat).
- **`DrivingScheme`** replaces the interim `DrivingLayer` (better name for the retired `Model`).

## Phases

| Phase | Files | Status |
|-------|-------|--------|
| 1 — driver input layer | `DriverInputIntegrator.h` (was `VehicleInputController.h`), `DriverInputExchange.h` (was `VehicleInputBridge.h`) + callers (`DriveExecution.cpp`, `VehicleInstanceSequence.h`, `TractrixVehicleScene.h`, `VehicleInputTests.cpp`) | 🟢 done — g++ syntax-clean |
| 2 — vehicle dynamics core (class API) | `VehicleController.{h,cpp}` → `VehicleSolver.{h,cpp}`; `VehicleControllerConfig`→`VehicleSolverConfiguration`; `SetInput`→`AssignInput`; `IsBuilt`→`Constructed`; `DrivingModel`→`DrivingScheme` (field `Model`→`ActiveScheme` — was briefly `DrivingLayer`/`ActiveLayer`, renamed on user feedback); `SlipSolverKind`→`SlipSolverSelection`. 17 referencing files + BOTH build lists (`ProjectDrive.cmake`, `ToolchainSequence.ps1`) updated | 🟢 done — g++ syntax-clean |
| 3 — powertrain subsystems | `PacejkaTyreModel.{h,cpp}`→`PacejkaMagicFormula.{h,cpp}` (`Model` banned); `PacejkaCache`→`PacejkaCoefficients` (`Cache` banned); `Drivetrain` `Set*`→`Assign*` / `Get*`→`Query*` (Engine/Turbo/Transmission/Clutch/Differential/Induction/Supercharger/RaceTune/Parameters); `TyreSlipDynamics::SetSolver`→`AssignSolver`, `GetSolver`→`QuerySolver`, `SetForceLag`→`AssignForceLag`, ctor param `Model`→`Formula`; `GetCache`→`QueryCoefficients`; `UseBrakeThermalModel`→`BrakeThermalEnabled`; mock helper `SetBoxInertia`→`AssignBoxInertia`. All callers + BOTH build lists updated. GRIT/UE reference names in comments (`GetSpringForce_N`, `GetMappedRangeValueClamped`) left verbatim | 🟢 done — g++ syntax-clean |
| 4 — `Test` ban (project directive) | Files renamed: `*Tests.cpp`→`*Checks.cpp` (Abs/Aero/VehicleDriving/VehicleInput), `TractrixTestTrack.h`→`TractrixProvingGround.h`, `FieldTestDrive.cpp`→`FieldTrialDrive.cpp`; tokens + prose + `TractrixVehicleScene.h` include/type/member updated; SKILL-Naming banned addendum + rationale updated. Not in any build list ⇒ no toolchain edit. External `RunFluidGpuTest`/`IsDepthTested`/CLI `--fluid-*-test` left (base-engine) | 🟢 done — g++ syntax-clean |
| 5 — formatting: members | trailing-underscore snake_case private members→PascalCase across the engine layer: `XPBDSoftTyre` (`params_`→`Parameters`, `nodes_`→`NodeRecords`, `edges_`→`ConstraintEdges`, `reaction_`→`ContactReaction`) + `BrakingSystem` (`params_`→`Parameters`, `abs_`→`AntilockParameters`, `wheels_`→`WheelRecords`). Other engine vehicle files were already PascalCase. Unit-suffixed fields (`Temperature_K`, `Torque_Nm`, `Pressure_Pa`, `_W`) kept — SI domain notation | 🟢 done — g++ syntax-clean |
| 6 — instance / scene seams | `DriveSceneStructure.{h,cpp}`→`DriveSceneAuthor.{h,cpp}` (`Structure` banned) + `DriveSceneIsCurrent`→`DriveSceneMatchesRevision` (`Is` prefix); `Config`/`InitialConfig`/`NewConfig` shorthand→`ActiveConfiguration`/`InitialConfiguration`/`NewConfiguration` in `VehicleInstanceSequence` + `ChaseCameraSolver`. **Also fixed a latent Phase-2 build break:** `cfg.Model`/`Config.Model`→`.ActiveScheme` in `DriveTelemetry.cpp` + `VehicleInstanceSequence.cpp` (field was renamed to `ActiveScheme` in Phase 2, but these Source files can't compile in-sandbox so it was missed). All refs + BOTH build lists + docs updated. Engine `SceneStructure.h` include and base-engine `ShowcaseStructure` reference left verbatim | 🟢 done |
| 7 — our identifiers in the copied host | `DriveExecution.cpp`: `IsDrive`→`DriveSelected` (our local). Inherited Project-Zero scene flags (`IsShowcase`/`IsLegacyRoom`/`IsOutdoor`/`IsMaterials`/`IsShaderBall`/`IsShowroom`) and engine APIs (`IsKeyPressed`, `IsMouseButtonPressed`, `IsDepthTested`, `Camera.IsSteeringActive()`) left verbatim — Tier-2/3, not ours | 🟢 done |
| 8 — formatting: rulers + 📦 markers | Header rulers normalized to **142** and section banners to **122** across the whole authored tree (`VehicleGeometry.h` 142→122 banners; `VehicleDrivingChecks.cpp` bare 122 `//===` → titled 122 `//---` banner). `📦` module marker added to every authored source that lacked one: 7 engine validation/checks `.cpp`, 3 Project-Drive host/seam `.cpp` (centered-title→`// 📦 …` line), `FieldTrialDrive.cpp`, `TractrixDriveScene.cpp`, and the `PngWriteCounterpart.h` forwarder shim. Tree-wide audit: **every** authored source carries `📦`, all `//===`=142, all `//---`=122, zero puzzle-piece markers, no tabs/trailing-whitespace. g++ syntax-clean across the engine layer | 🟢 done |
| 9 — formatting: `///` blocks + inline column alignment | Convert prose headers to machine-read `///` annotation blocks (prose/in/out/err/use/cost/pre/post/note/tag) and vertically align inline `// [unit] - desc` comment columns across the layer. Pure cosmetic, cannot affect the build, high mechanical churn / low correctness value; most files are the uncompilable Vulkan host/seam TUs. Deferred | 🟡 residual — cosmetic, low priority |

## Build sync

Pure identifier renames do not move files, so the `Build/ProjectDrive.cmake` and `Build/ToolchainSequence.ps1` source
lists are unaffected. When a **file** is renamed (Phase 1 renamed two header files), update every place that names it —
including the MSVC `ToolchainSequence.ps1` and any `Module.toml`, not only CMake/g++. Header-only files are not in the
source lists, so Phase 1 needed only a comment fix in `ProjectDrive.cmake`.

## Phase 2 decision (resolved)

`VehicleController` → **`VehicleSolver`** (solves the coupled longitudinal/lateral/yaw dynamics). `VehicleIntegrator`
was considered and rejected. Applied across all 17 referencing files and both build lists. `Step`/`Build`/`SlipSolver`
were kept — they are not on the banned list (`SlipSolver` uses the approved `Solver` suffix).

## `VehicleSolver.{h,cpp}` — fully compliant (formatting completed early)

Beyond the Phase-2 banned-word renames, the two `VehicleSolver` files were finished to full SKILL compliance:
banner emoji is `📦`; every private member moved off trailing-underscore snake_case to PascalCase
(`config_`→`ActiveConfiguration`, `input_`→`DriverCommand`, `telemetry_`→`CurrentTelemetry`, `tyres_`→`SoftTyres`,
`built_`→`ConstructionComplete`, `slip_`→`SlipDynamics`, `slipState_`→`SlipDeflections` (drops banned `State`),
`drivetrain_`→`Powertrain`, `braking_`→`BrakingHydraulics`, `wheelOmega_`→`WheelSpin`, `pacejka_`→`PacejkaTyre`,
`hooks_`→`ChassisHooks`, `steerAngle_`→`SteerAngle`, `gearIndex_`→`GearIndex`, `shiftTimer_`→`ShiftTimer`);
single-letter params fixed (`Clamp(v,lo,hi)`→`(Amount,Lower,Upper)`, `Sign(v)`→`Sign(Amount)`,
`PlanarNormalized(v)`→`(Vector)`, `Step(dt)`→`Step(Δτ)`). All members are private, so no external caller changed.
Local **physics notation** (`Fx`,`Fy`,`Fz`,`Vx`,`Vsy`,`Iw`,`Reff`,`kappa`,`alpha`,`omega0/1`) was deliberately kept — it
mirrors the equations documented in the comments and is the correct domain vocabulary, not banned shorthand.

## Deferred to the formatting pass (Phase 5)

Items found during Phase 2 but out of the banned-**word** scope: internal trailing-underscore snake_case members and
single-letter params across the rest of the layer. (The banner emoji is settled — `📦` everywhere; the old marker retired.)
Batched together so the diff is one mechanical style sweep rather than scattered noise across feature phases.
