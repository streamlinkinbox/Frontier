# Plan — Phase 52 bounded analytic torus face offset

## Capability slice

Advance face editing into the closed toroidal surface domain with one exact analytic ring torus.
The selected face is the torus's complete periodic face; a positive normal offset is represented
exactly by increasing its minor radius while preserving its major radius, centre, and axis.

This is distinct from the Phase 51 revolved annular-prism cap route: the torus has no planar caps,
and one closed analytic face with only the kernel's periodic seam topology, giving genus-one
`V1/E2/C4/L1/F1` topology. It is not a cone/bicone apex operation, a generic freeform offset, or a
healing/mesh fallback.

The focused fixture uses major radius `8`, minor radius `2`, centre `(0,0,0)`, Z axis, and positive
face offset `0.75`. The result has major radius `8`, minor radius `2.75`, and the same topology.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetTorusFace`. It accepts only one closed, one-hull, genus-one body with
periodic seam topology `V1/E2/C4/L1/F1` and exactly one natural `SurfaceClassification::Torus` face.
The source must be a ring torus coaxial with the origin and Z axis. The result is rebuilt with
`BrepBody::Torus`, preserving analytic toroidal support and topology.

The route accepts only finite positive offsets that keep `minor radius + distance < major radius`.
The torus volume identity is `2π² · major radius · minor radius²`.

## Refusal boundary

Refuse partial or trimmed toroidal faces, spindle/self-intersecting offsets, zero/negative/non-finite
offsets, spheres, cylinders, cones, annular prisms, translated or tilted toruses, mixed/freeform
supports, malformed topology, and healing-dependent fallback. The operation is copy-in/copy-out.

## Verification and proof

`TorusFaceOffsetVerification` constructs the exact ring torus and checks source/result
`V1/E2/C4/L1/F1`, genus one, closed natural torus support, major/minor-radius identity, volume
identity, source immutability, public dispatch, refusal boundaries, and a distinct durable proof at
`Proofs/Phase52_TorusFaceOffset.png`.
