# VehicleHMI Port Recovery — Verified Audit, Merge, and Continuation Plan

## TL;DR

- ✔️ Found everything asked for, with exact revisions. Nothing below is assumed from a branch name.
- 🔴 The earlier recovery report was right about 649/ec6/215 pins and wrong/incomplete on XPBD + FEM:
  native CPU XPBD is recoverable at `38d28c3`; spring+XPBD+shell+solid FEM together are JS-only on
  the unmerged e9bd/86ba demo branches; no native C++ FEM tyre exists anywhere inspected.
- 🔴 "Rendering pipeline finished" is not supported for Vulkan: ReSTIR/SDF/card/SVGF mathematics run
  as CPU portable cores with frame declarations; GPU recording, Slang bodies, hardware ray tracing,
  and demo golden parity are all still SDK-phase work (the ec6 exhibit READMEs state this themselves).
- Merge outstanding is small and exact: ec6 Phase 7 (7 commits) onto 649. Everything earlier in ec6 is
  already merged into 649. Owner order retained: merge + prove baseline → ReSTIR/ray-tracing SDK
  phase → vehicle/tyre completion. Cabin HMI display stays deferred.

## How this was verified (no assumptions)

| Check | Method | Result |
| --- | --- | --- |
| Branch heads | `gh api repos/<repo>/branches/<branch>` for every named repo/branch | All pins in the ledger confirmed |
| XPBD/FEM existence | Blob-less partial clones of 7 repos; every branch tip tree + full filename history | Native XPBD found; native FEM absent |
| "Latest" XPBD | Blob hashes per commit, not tip dates | Final blobs at `38d28c3` (see §3) |
| ec6 vs 215 vehicle copy | Blob-hash compare per file | Verbatim except 2 adapted + 5 new (see §4) |
| Demo tyre selector | Read `VehicleSolver.js` on ec6, e9bd, 86ba, 21530081 | Spring-only vs 4-model selector (see §2) |
| Renderer status | Read native headers + exhibit READMEs on ec6 | CPU cores yes; Vulkan bodies no (see §5) |
| XPBD behaviour | Built + ran recovered validation, g++ 12, Linux sandbox | Final 10/14, older 9/14 (see §3) |
| Not run here | ec6 7-program CTest suite; any Windows/MSVC or Vulkan run | 🏴 Blocked: no CMake/Vulkan/Windows in sandbox |

## 1. Source lineage (what copied what)

```text
SultanAladin/GRIT (Chaos build)                 SultanAladin/Frontier- arena/215a6f39 (f2cfc57)
  ComputeSuspensionDisplacements  ───────────►   Project-Drive native C++: Jolt world (RigidBodySolver,
  (quoted in TraceContactTyre.h)                 908 lines), VehicleSolver, Drivetrain, Pacejka MF,
                                                 suspension, ABS, aero, ControlVehicle sockets/meshes
                                                            │  copied, contact simplified
                                                            ▼
                                    HTML VehicleHMI demo (WebGPU/WGSL ReSTIR + Jolt WASM)
                                      e9bd 6afb6f0 = 86ba 819bd55 (identical tyre blobs)
                                      TyreModel = spring | xpbd | shell | solid
                                                            │  port target (improve in C++/Vulkan)
                                                            ▼
                       SultanAladin/Frontier arena/649fea90 (4ef9f21)  ◄── merge ──  c7 ec6 (d99f5e5)
                         main: pattern/texture/GI-math/editor             Phase 7: Jolt + vehicle (7 commits)
```

- `VehicleSolver.js` header (all demo copies): chassis/sockets/tyre sheet are "the Project-Drive
  ControlVehicle's own (`VehicleGeometry.h`, `SuspensionSpecification.h`, `PacejkaMagicFormula.h`,
  `Drivetrain.cpp`, `VehicleSolver.h`)"; only the contact differs (native XPBD lattice → JS point contact).
- Jolt is one pin: `jrouwe/JoltPhysics@0373ec0dd762`, sha256 `834644d7…` — identical in the 215 lock
  file (`ExternalPackages/Dependencies.lock.json`) and ec6 `Automation/JoltDependencies.json` (5.3.0).
  The "Jolt 5.6.1" text in the 215 header is a stale comment; the runtime string derives from headers.

## 2. Tyre physics — exactly where each mechanism lives

| Mechanism | HTML demo | Native C++ | Status |
| --- | --- | --- | --- |
| Spring (`K = K₀ + κ·p`, ~223 kN/m) | All demo copies (`VehicleSolver.js`) | `TraceContactTyre.h` fan + 250 kN/m residual (215 head, ec6) | ✔️ In production path |
| Reduced radial XPBD (32 particles, response tables) | e9bd/86ba `TyreCarcassProjection.js` only | — | JS reference only |
| CPU XPBD lattice (3×48, gas surface, rim bottoming) | — | `XPBDSoftTyre.*` + `XPBDTyreSolver.*` at `38d28c3` | 🚩 Recovered; recalibration needed (§3) |
| GPU XPBD (Vulkan/Slang, 6 compute units) | WGSL copy on `unassignedinbox/Frontier` aef49291 | `SultanAladin/Slate` ca517016 `TyreSolver` (`8e79925`) | 🚩 Separate stack; not Frontier-integrated |
| Shell FEM (orthotropic membrane + bending) | e9bd/86ba `TyreContinuumElasticity.js` `ShellContinuumWheel` | — | 🔴 Native = new work |
| Solid FEM (Hex8 compressible Neo-Hookean) | e9bd/86ba `SolidContinuumWheel` (160 nodes, 128 Hex8) | — | 🔴 Native = new work |
| Tetrahedral FEM (StVK, explicit) | `c7` 3488daf0 demo; `streamlinkinbox` 7c483f80 shell oracle | — | Secondary JS references only |

- 🔴 Correction of the ec6 assumption: the demo copies *inside* ec6/649 (`Experimental/SlateReference`
  A `VehicleProvingGrounds`, B `IlluminationHost`) have **no** continuum files and no `TyreModel`
  selector — measured `VehicleSolver.js` is spring + point contact, visual record 208 bytes.
  The 4-model demo is pinned by ec6 `Frontier/Projects/Project-VehicleHmiDemo/Upstream.json` to e9bd
  (`6afb6f0`), where the record is 7,888 bytes and `AssignTyreModel(spring|xpbd|shell|solid)` exists.
  Neither e9bd nor 86ba is an ancestor of 649 — that demo revision was never merged in.
- Native modular seam already existed and was deleted with XPBD: at `5f264e0`/`38d28c3`,
  `VehicleSolverConfiguration::ContactModel` = `RayTrace | XPBDCarcass`, one `TyreReaction` output,
  `TyreSubsteps = 16`. Reviving that selector (extended, not reinvented) is the correct port shape.
- Geometry agrees across stacks: tyre R 0.50855 m, rim R 0.3446 m, width 0.3958 m (`VehicleGeometry.h`,
  measured from `ControlVehicle.blend`) = JS FEM profile constants. XPBD header defaults (0.34/0.20 m)
  are generic and must be stamped from `VehicleGeometry` per wheel, as `ApplyGeometry` already does.

## 3. Recovered native XPBD — measured, not assumed

| Item | Evidence |
| --- | --- |
| Final source | `Frontier-@38d28c3`: `XPBDSoftTyre.cpp` `ee2cfa3`, `.h` `5d59439`, `XPBDTyreSolver.*` `a912e50/daee028` |
| Removal | `9ceb33c` (2026-10-13): "VehicleDrivingChecks: 12/12 pass (XPBD: 8/12), 1.3 s (XPBD 3×48: 20 s)" — speed/robustness, not loss |
| Build here | `g++ -std=c++17 -O2 XPBDSoftTyre.cpp PacejkaMagicFormula.cpp XPBDTyreValidation.cpp` → clean, 3 s |
| Run here (final `38d28c3`) | **10 passed, 4 failed** in 10 s — `Exhibits/VehiclePhysicsRecovery/Results/XpbdFinal-38d28c3.log` |
| Run here (Slate `ca73246`) | **9 passed, 5 failed** — `Results/XpbdSlate-ca73246.log`; final revision is strictly better |
| Failing checks (final) | Fx peak 7,754 vs Pacejka 5,865 N (1.32×, limit 1.3×); slip stiffness 4.2×; cornering stiffness 2.6×; Mz sign +214 vs −719 N·m |

- 📝 Read: the solver is stable, load-bearing, correct-sign in Fx/Fy, and load-sensitive. The
  validation file never changed (`cccf09a` everywhere) while the tyre gained gas-surface inflation,
  tension-only sidewalls, shear spokes, rim bottoming, and the active window — calibration drifted
  from the stale oracle bands. Port task = recalibrate compliances against Pacejka + fix Mz lever arm
  (patch fore-aft resolution), then re-run. Do not port it as-is and call it calibrated.
- Configurable surface exists: `SoftTyreParameters` (rings/segments, pressure, gas law, 8 compliances,
  damping ratios ζ, active window, rim-bottoming). Productise via TOML (`toml++` per `CLAUDE.md`),
  stamp geometry from `VehicleGeometry`, keep Pacejka as tangential source for spring/trace modes.
- FEM native design (new work, from JS mathematics only): one wheel-local node/element store with
  Hex8 Neo-Hookean solid + shell membrane/bending sharing the 32×5 ControlVehicle profile, pressure
  volume term, bead clamp, penalty/Coulomb contact through the same ground query, output restricted
  to `TyreReaction` + node displacements for the existing GPU deformation record. Explicit steps need
  the JS demo's own CFL lesson (tet demo: ρ 1000 kg/m³ mass-scaled, Δt 1e-4 s) — budget substeps and
  prove energy behaviour; never copy WGSL/JS structure beyond equations.

## 4. Previous agent (ec6 Phase 7) — correct vs fix

### Done correctly ✔️

| Claim | Verification |
| --- | --- |
| Jolt 5.3.0 pinned + checksum restore | `JoltDependencies.json` + `PrepareJolt.py`; revision = 215 lock revision |
| CMake target `FrontierJoltPhysicsExtension` | Root `CMakeLists.txt:49`, static, links `Jolt`; `FrontierVehiclePhysics` links it |
| Vehicle files from latest Project-Drive | 17 files byte-identical to 215 (drivetrain, Pacejka, aero, slip, geometry, trace, suspension…) |
| No Jolt wheeled-vehicle constraint | Custom forces at patches; Jolt integrates chassis only — matches plan decision D6 = B |
| Contact material split from PBR | `VehicleContactMaterial.h` (geometric-mean μ); `QuerySurface` returns hit/normal/body μ |
| 7 validation programs defined | `Exhibits/VehiclePhysics/CMakeLists.txt` — 7 `add_test` rows present as claimed |

### Fix / complete 🔴

Effort legend: ✔️ low · 🚩 medium · 🔴 high (more = more work).

| # | Gap (evidence) | Fix | Effort |
| --- | --- | --- | --- |
| F1 | 7/7 CTest not re-run independently; Windows/MSVC never proven (own README) | Re-run on merge baseline + Windows build before any "done" claim | 🚩 |
| F2 | `RigidBodySolver` reduced in port: 609 vs 908 lines (215) — no Heightfield, `StepOnce`, sphere/cylinder casts, distance spring | Restore 215 surface into the extension; keep ec6 `QuerySurface` addition | 🚩 |
| F3 | No Project-Drive in world: no `DriveCourse`, scene author, ControlVehicle meshes, wheel/rim/tyre visual materials (own README "Remaining boundary") | Port `Projects/Project-Drive` scene + content; assign `performance-rubber.material` + body/rim PBR; mechanical μ stays separate | 🔴 |
| F4 | Chassis shape generic: half extents (2.3, 1.15, 0.20) vs authored ~5.97 m length (sockets ±2.99 m), CoM/inertia from `VehicleGeometry` unused by the Jolt body | Build chassis from `VehicleGeometry` (mass 1300 kg, inertia radii, socket extents) | ✔️ |
| F5 | Dropped 215 pieces: `VehiclePhysicsThread`, `VehicleInstanceSequence`, `TyreDeformationPublisher`, in-engine driving/input checks | Restore with F3; thread owns fixed-step + tyre substeps | 🚩 |
| F6 | XPBD/FEM absent; report said XPBD "not recoverable" (history not searched) | §2/§3 of this plan; revive `ContactModel` selector + new native FEM | 🔴 |
| F7 | Names breach `SKILL-Naming.md`: `binding` (banned 2026-06-20), `Component` as payload noun, `BrakingSystem` (`system`), verbs `Bring/Assign/IsReady` | Rename at the extension seam only, with owner ruling per name; do not churn 215-verbatim files silently | 🚩 |
| F8 | `PacejkaSlipSolver`/`WheelSpinSolver` are JS-derived provisional exhibit code beside native `PacejkaMagicFormula`/`TyreSlipDynamics` | Keep exhibit-only labelling; product library uses native files only (already true — keep it true) | ✔️ |
| F9 | Prior "ledger commit `3cf18d9b`" exists on no remote (checked 5 repos) — unpushed work was lost | This ledger commit replaces it; push only with owner approval (Actions gate) | ✔️ |

- Extension naming (owner question): keeping `FrontierJoltPhysicsExtension` is right — `Subsystem`
  maps to `Extensions/<Name>/` in the naming skill, and `RigidBodySolver` is a mechanism name, not a
  vehicle name. The renames owed are F7's banned words inside it, not the target name.

## 5. Renderer (ReSTIR / SDF GI / ray tracing) — actual status

| Layer | Demo (WGSL) | Native today (649 = ec6, identical 112 render paths) | Verdict |
| --- | --- | --- | --- |
| Frame schedule | JS host order | `SubmissionOrder` + `TransientTargetAtlas` + `BarrierDerivation`; complete frame = 38 units / 33 targets / 77 barriers | ✔️ Done as data |
| ReSTIR estimator | `ReservoirTemporal/Spatial.wgsl` | `ReservoirSampling.*` CPU double-precision RIS + reuse; suite reports RMSE 0.385 vs 3.86 naive | ✔️ Mathematics only |
| SVGF | `Variance*.wgsl` | `VarianceFiltering.*` CPU; reported RMSE 0.277 → 0.023 | ✔️ Mathematics only |
| SDF indirect | `Distance*.wgsl` | `DistanceField.*` analytic primitives (sphere/box/half-space) + baked grid + sphere trace | 🚩 Toy scene class only |
| Card radiosity | `Card*.wgsl` | `SurfaceCards.*` CPU per-texel radiosity with SDF visibility | ✔️ Mathematics only |
| End-to-end frame | WebGPU real-time | `ReferenceRenderer` CPU, brute-force shadow rays, 320×200 capture, "intentionally high" runtime | 🚩 Oracle, not product |
| Vulkan + Slang bodies | 80 WGSL shaders | 2 Slang files (46-line fixed-light raster + present), "not compiled in the portable sandbox" | 🔴 Not ported |
| Hardware ray tracing | Software BVH (own README: "not Vulkan hardware ray tracing") | No acceleration-structure / ray-query code anywhere in native tree | 🔴 Not started |
| Golden-image parity | Demo captures exist upstream | "Demo JS/WGSL were never copied or line-read" (`Docs/PortingFindings.md` A1); oracles are analytic | 🔴 Open by design |

- 📝 Translation: the agent finished the *portable-core phase* honestly and documented its boundary;
  anyone reporting "the rendering pipeline (ReSTIR SDF GI) is finished" dropped the words "CPU
  reference mathematics". The remaining renderer work is the SDK phase: record the declared frame on
  Vulkan, write Slang bodies unit-by-unit against the CPU oracles, add hardware ray query for
  visibility/reflection where it beats sphere tracing (measure, per unit), then golden captures.
  C++/Vulkan wins available: fp32 GPU reservoirs at full res, async compute overlap of the indirect
  prelude, AS-based visibility instead of per-texel sphere traces, bindless card atlas.

## 6. Merge (649 ← ec6) — exact surface

```text
38e6215 (shared base, 2026-10-22) ──┬─ 649: 15 commits (merges of Oct-22 ec6 + pattern/texture work)
                                    └─ ec6:  7 commits (Phase 7 only: 60b64db … d99f5e5)
Phase 7 diff: 44 added files + modified CMakeLists.txt + Docs/PortingFindings.md + Docs/VehicleHmiEnginePlan.md
649 touched only one of those three after the base (e5f1081) → expect at most one small CMake conflict.
```

- 🔴 Actions gate (`CLAUDE.md`): pushing a merged tree to an `arena/**` branch touches
  `Frontier/Projects/Project-Networking/**` paths already in 649, and `networking-windows.yml`
  triggers on `arena/**` pushes for those paths. Owner approval is required before that push.
- This session branch (`streamlinkinbox/Frontier arena/10a5e6ab-frontier`) cannot push into
  `SultanAladin/Frontier`. The merge must either land here as the new combined tree, or be executed
  by the owner/other agent in Sultan's repo from this ledger. Decision table in the delivery message.

## 7. Continuation order (owner priorities applied)

| # | Work | Exit evidence |
| --- | --- | --- |
| 1 | Merge Phase 7 onto 649; restore F2/F4; build root + exhibits (GCC here, MSVC on Windows) | CTest output per suite; no tree regressions |
| 2 | Renderer SDK phase: Vulkan recording of the declared frame; Slang bodies vs CPU oracles; hardware ray query where measured faster | GPU captures + per-unit numeric parity vs `ReferenceRenderer` |
| 3 | Project-Drive world: course/heightfield, ControlVehicle scene + materials, physics thread, spawn/camera/telemetry | Drivable scene run + captures; driving checks green |
| 4 | Tyre completion: revive `ContactModel` (trace default); recalibrated XPBD; new native shell + solid FEM behind the same `TyreReaction` | §3 validation 14/14; pressure/load/kerb sweeps per model; cost table |
| 5 | Perf pass (C++/Vulkan > demo): substep budgets, active-window XPBD, GPU deformation record reuse | Measured ms/wheel table vs demo budgets, same scene |
| — | Cabin HMI display | Deferred by owner — do not start |
