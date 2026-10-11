# Plan — Phase 53 bounded analytic sphere face offset

## Capability slice

Advance exact face editing into the closed spherical support domain. The selected face is the complete
natural analytic sphere; a positive normal offset is represented exactly by increasing its radius while
preserving the centre and the kernel's canonical axis metadata.

This is distinct from Phase 52's genus-one torus route: the sphere is a genus-zero periodic surface
with pole and seam topology `V2/E1/C2/L1/F1`. It is not a cone/bicone operation, a trimmed-face
approximation, or a tessellation/healing fallback.

The focused fixture uses radius `5`, centre `(0,0,0)`, canonical Z axis, and positive face offset
`1.25`. The result has radius `6.25` and the same topology.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetSphereFace`. It accepts only one closed, one-hull, genus-zero body with
sphere topology `V2/E1/C2/L1/F1` and exactly one natural `SurfaceClassification::Sphere` face. The
source must be an origin-centred sphere with the canonical Z-axis metadata. The result is rebuilt with
`BrepBody::Sphere`, preserving exact analytic spherical support and topology.

The route accepts only finite positive offsets. The volume identity is `4πr³ / 3`.

## Refusal boundary

Refuse toruses, cylinders, cones, annular prisms, translated or non-canonical-axis spheres, zero/
negative/non-finite offsets, malformed topology, mixed/freeform supports, and healing-dependent
fallback. The operation is copy-in/copy-out.

## Verification and proof

`SphereFaceOffsetVerification` constructs the exact sphere and checks source/result
`V2/E1/C2/L1/F1`, genus zero, natural analytic sphere support, radius/centre identity, spherical
volume identity, source immutability, public dispatch, refusal boundaries, and the durable proof
`Proofs/Phase53_SphereFaceOffset.png`.
