# 🧩 Phase R5 — Hardware AS path (rayQuery Tier B) + Tier B slab evaluation (plan, for approval)

Plan v2.2 phase R5. R4b delivered the full OpenPBR lobe set on the Tier A software path; R5 adds the
Vulkan hardware path and switches Tier B to multi-slab evaluation. Per the standing rule: plan first,
code only after you approve.

## 0. Scope line

**In:** BLAS/TLAS build at scene load; `rayQueryEXT` in the same compute shaders behind the existing
`TraceScene` / `TraceShadow` names (specialisation constant selects the implementation); per-instance
identity from the TLAS (kills the R4b linear instance scan); Tier B `slab_limit` default 4 with the
multi-slab permutation; Software-tier regression (1060 output unchanged).
**Out (explicit):** ray pipeline / SBT (Tier C placeholder stays), TLAS refit for motion (R7 H-PLOC),
ReSTIR temporal/spatial reuse (R6), denoiser (R6), texture compression/streaming.

## 1. Design

* **AS layout (recommended: per-instance BLAS + one TLAS).** One BLAS per Slate instance over that
  instance's triangle range (object-space positions + shared index range — the same vertex data the R2
  raster consumes), one TLAS with `customIndex = Slate instance id`. Sponza = ~105 one-time BLAS builds
  at load (ms, logged). Alternative: single BLAS over the flat world-space soup + identity TLAS —
  simpler, but keeps the instance-lookup problem and teaches R7 nothing. Per-instance also makes future
  rigid-motion TLAS updates trivial.
* **Shader path.** `#extension GL_EXT_ray_query`, TLAS on a new binding (CWBVH blobs on 8/9 stay for
  the Software tier — both resident, shader picks by spec constant, estimator untouched). `TraceScene`:
  `rayQueryEXT` closest-hit → `committedInstanceCustomIndexEXT()` = Slate instance (no scan),
  `committedPrimitiveIndexEXT()` + `committedIntersectionBarycentricsEXT()` → vertex fetch via the
  instance's `VertexOffset`/`FirstIndex` (same `ResolveMaterial`, barycentrics now free). `TraceShadow`:
  candidate loop, `AlphaMaskCutOut()` rejects cut-outs and continues (replaces the 4-layer closest-hit
  walk; `AlphaMaskedMaterialCount == 0` keeps the committed-only fast path).
* **CPU side.** New `Engine/GeometricRaster/AccelerationStructureIndex.{h,cpp}` (Index = direct spatial
  lookup — the TLAS literally is that): owns BLAS/TLAS handles + scratch, builds from
  `SceneStructure` + instance ranges, one-triangle BLAS/TLAS smoke test at start-up (v2.2 §3.2 promised
  it for R4 — never landed, so R5 row 1). `UploadScene` builds/updates the AS only when the resolved
  tier ≥ RayQuery; `ray_tracing_tier = Software` forces CWBVH even on RTX (the identity proof).
* **Tier B slabs.** `slab_limit` default becomes per-tier (A: 1, B/C: 4, ceiling 8 — config key already
  exists and clamps). `ResolveMaterial` gains the `slab_index` loop with OpenPBR §3.10 albedo scaling
  between verticals (the `ResolveLayers` bookkeeping already exists per slab); permutation chosen per
  material by resolved slab count, so walls stay on the 1-slab path. Tier A behaviour unchanged.

## 2. Files

| File | Change |
|---|---|
| `Engine/GeometricRaster/AccelerationStructureIndex.{h,cpp}` (new) | BLAS-per-instance + TLAS build, customIndex table, smoke test, build metrics |
| `Engine/Shaders/ReSTIRViewport.slang`, `TraversalCWBVH.slang` | rayQuery path behind `TraceScene`/`TraceShadow`, spec-constant switch, TLAS binding |
| `Engine/DeviceExchange/SwapchainExchange.{h,cpp}` | AS storage + build submission in `UploadScene`, both blob sets resident |
| `Projects/Project-Zero/Source/GameExecution.cpp` | tier log line (`using = …`), AS build ms, forced-downgrade toast path (exists — wire the AS case) |
| `Engine/DisplayPresentation/DiagnosticInspector.{h,cpp}` | F3 popup: tier + AS stats (BLAS count, bytes) + per-tier kernel ms (carried from R4b if wanted — §4.3) |
| Build lists | new TU in `CMakeLists.txt` + `ToolchainSequence.ps1` (PS 5.1-safe) |

No config-key changes (`ray_tracing_tier`, `slab_limit` already exist). No data-format changes.

## 3. Proofs

1. **Toggle identity (hardware):** `ray_tracing_tier = Software` vs `RayQuery` on RTX — converged images
   identical on Cornell / Sponza / shaderball (per-frame noise may differ by RNG draw order, as in R4b).
2. **CPU mapping harness (sandbox):** instance→BLAS-range table vs `SceneStructure` (every flat triangle
   covered exactly once, customIndex round-trips to the Slate instance id); single-slab Tier B output =
   Tier A output by construction (same `EvaluateBsdf`).
3. **Smoke test:** one-triangle BLAS/TLAS + one ray at start-up, logged.
4. **Regression:** all R4b harnesses re-run (furnace 67/67, resolve 48/48, exports) — Tier A untouched.

## 4. Hardware acceptance

1. RTX run: toggle identity (§3.1) + `kernel` ms per tier at 1080p on Sponza (baseline table for R6;
   R4b §3.5 numbers land here). AS build ms in the log.
2. **GTX 1060 run: output identical to R4b, tier line reads `Software BVH`, no AS built** — proves the
   never-faked path and that the primary path is untouched. (Pascal has no `rayQuery`; §5.)
3. F3 popup shows tier + AS stats; forced `Software` on RTX toasts the downgrade.
4. Multi-slab: car-paint-heavy shaderball pixels differ Tier B vs A as designed (coat/flake separate);
   plain-wall pixels identical.

## 5. ⚠️ The hardware problem (flagged, not hidden)

Neither machine in this loop can execute R5's new code: the sandbox has no Vulkan at all, and **Pascal
never got `VK_KHR_ray_query`** (v2.2 risk table) — your 1060 will always resolve `Software BVH`. What the
1060 *can* prove is §4.2 (primary path untouched) plus review-level confidence (spec/extension-correct
code, mapping harness, toggle design). The §3.1/§4.1 proofs need an RTX run — borrowed machine, CI with
a GPU runner, or deferred until you have one.

## 6. Order of work (one commit per row)

1. `AccelerationStructureIndex` + smoke test + mapping harness (CPU-provable here).
2. Shader rayQuery path (spec switch, instance id, barycentrics, alpha confirm) — compiles here via
   slangc-less review only; executes on RTX.
3. Wiring: `UploadScene` AS build, toggle, telemetry, F3 popup, build lists.
4. Tier B multi-slab permutation + re-run of all harnesses; phase note.

## 7. Open decisions for you

1. **Hardware gap:** implement R5 now with RTX proofs pending (§5), or skip to **R6 ReSTIR DI proper**
   first (runs on the 1060, big visual win: temporal/spatial reuse, M-clamp, neighbour rejection) and do
   R5 when an RTX run exists? My recommendation: **R6 first** — R5's code cannot be proven here and an
   unproven AS path sitting under the kernel helps nothing; R6 pays off on the card you own.
2. **BLAS layout:** per-instance + TLAS (recommended, §1) or single world-space BLAS?
3. **R4b carry-over:** fold the `DiagnosticInspector` complexity-histogram + LOD-stats popup into R5
   row 3, or defer to R6 with the other diagnostics (M/W/age views)?
