# SolidArc Phase 36p — bounded complete cylinder–cone root fillet

## Scope

Add one conservative constant-radius fillet route for the canonical complete coaxial cylinder–cone boss root:

- one closed circular root shared by a cylindrical boss below and a native conical frustum above;
- narrowing cones only, with a positive cone top radius and a measured nonzero half-angle;
- analytic circular meridian tangent to the cylinder and cone;
- retain the outer cylinder, planar shoulder, shortened boss cylinder, retained cone, and both caps;
- transactional refusal for unsupported flaring, apex, zero-angle, non-coaxial, arbitrary, and malformed roots.

## Acceptance checks

- Reuse structural `CylinderConeBossRoot` classification and preserve source immutability.
- Solve the fillet center and contacts from the cone half-angle rather than approximating the blend.
- Preserve native cylinder/cone/toroidal support metadata and exact genus-zero topology `V6/E11/C22/L7/F7`.
- Check analytic meridian volume, manifold/genus/orientation, console dispatch, and visible proof output.
- Verify zero, negative, consuming, flaring, apex, arbitrary-torus, and unsupported selections refuse transactionally.

## Explicit non-goals

Do not support flaring or zero-angle cylinder–cone pairs in this slice, partial roots, non-coaxial/oblique supports,
cone–cone or apex networks, arbitrary trimmed/branched/incomplete chains, variable-radius or nonlinear/G2 fillets,
curved corner patches, or general intersection/trim/sew healing.

## Verification and documentation

Add `CylinderConeFilletVerification.cpp`, CMake/build-gate registration, README/roadmap/limits entries, and
`Proofs/Phase36p_CylinderConeFillet.png`. Run the dedicated and complete SolidArc gates and inspect the generated PNG before committing.
