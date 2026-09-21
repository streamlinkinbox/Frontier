# SolidArc Phase 36o — bounded partial plane–cone root fillet

## Scope

Add one narrow constant-radius fillet route for the same canonical coaxial plane–cone sector accepted by Phase 36j:

- one open non-reflex sector or physical half-turn;
- one two-member tangent chain shared by a planar shoulder and a native coaxial conical frustum;
- positive conical top radius, so the apex route remains separate and refused;
- exact revolution of the analytic circular meridian roll;
- radial endpoint-cap healing only for the general sector;
- transactional construction, exact topology, manifold/orientation, and source immutability checks.

The route is a constant-radius analytic sector fillet, not a general partial-edge or variable-radius solver.

## Acceptance checks

- Reuse structural `PlaneConePartialBossRoot` classification; do not trust face order or tags alone.
- Preserve the source and reject zero, consuming, over-large, reflex, full-turn, apex, torus, non-coaxial, branched, and malformed inputs.
- Verify the shoulder contact, conical contact, spine radius, and meridian arc from the cone half-angle.
- Retain the outer cylinder, trimmed shoulder, exact toroidal roll, retained cone, and bottom/top sector caps.
- Heal only the two radial caps for a general open sector.
- Enforce `V12/E18/C36/L8/F8` for the general sector and `V12/E17/C34/L7/F7` for the half-turn.
- Add console dispatch and a visible exterior-readable top-view proof.

## Explicit non-goals

Do not support a true apex, a cone–cone or cylinder–cone apex network, non-coaxial/oblique axes, reflex or arbitrary trims, branched/incomplete chains, curved corner networks, nonlinear or variable-radius laws, G2 construction, or general intersection/trim/sew healing.

## Verification and documentation

Add `PartialPlaneConeFilletVerification.cpp`, CMake/build-gate registration, README/roadmap/limits entries, and
`Proofs/Phase36o_PartialPlaneConeFillet.png`. Run the dedicated and complete SolidArc gates and inspect the generated PNG before committing.
