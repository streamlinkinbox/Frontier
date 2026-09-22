# SolidArc Phase 36q — bounded complete cone–cone root fillet

## Scope

Add one narrow constant-radius fillet route for a complete coaxial cone–cone root:

- one closed circular root shared by two native coaxial conical frusta;
- positive radii at both far caps and distinct support slopes;
- one analytic circular meridian tangent to the lower and upper cone generators;
- retained lower cone, toroidal roll, retained upper cone, and native caps;
- exact full-turn topology and analytic meridian volume acceptance.

The slice accepts only the canonical complete source, not the already-supported partial cone–cone sector topology.

## Acceptance checks

- Structural classifier measures both cone endpoint rows, common root frame, axes, slopes, and caps.
- Narrowing lower-to-root and root-to-upper support configuration only; zero-angle, same-slope, apex, and unsupported
  orientation cases refuse explicitly.
- Reconstruct exact retained cones and a circular roll from the two cone half-angles.
- Enforce genus-zero `V5/E9/C18/L6/F6` with no open, non-manifold, or misoriented edges.
- Check analytic first-moment removal, source immutability, console dispatch, and visible exterior proof.

## Explicit non-goals

Do not support partial sectors, non-coaxial/oblique cones, apex/zero-radius networks, flaring or reflex configurations,
branched/arbitrary/incomplete roots, variable-radius/nonlinear/G2 blends, corner patches, or general intersection/trim/sew healing.

## Verification and documentation

Add `ConeConeFilletVerification.cpp`, CMake/build-gate registration, README/roadmap/limits entries, and
`Proofs/Phase36q_ConeConeFillet.png`. Run the dedicated and complete SolidArc gates and inspect the PNG before committing.
