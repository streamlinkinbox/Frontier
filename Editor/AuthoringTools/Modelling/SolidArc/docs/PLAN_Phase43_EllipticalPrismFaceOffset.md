# Plan — Phase 43 bounded elliptical-prism face offset

## Capability slice

Add the next non-cone curved-profile modelling domain: a straight prism extruded from one exact
rational ellipse. A positive offset of its selected upper planar cap extends the prism while
preserving the analytic ellipse and its orientation. This is intentionally different from the
hole topology of Phase 42 and from the native circular-cylinder tweak routes: the cross-section is
non-circular and the operation is a face-edit offset, not an edge tweak or a blend.

The focused fixture uses an ellipse centred at the origin with major radius 6, minor radius 3,
major direction +X, height 5, and offset distance 2. The route must recognize the exact analytic
ellipse, vertical generators, and upper planar cap before reconstructing the unchanged ellipse at
the extended height.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetExtrudedEllipticalPrism`. It accepts only one closed rational ellipse
profile extruded along +Z, one upper planar cap, and one finite positive distance. It validates the
native analytic support, two Z levels, the exact profile topology, and a positive-volume solid. It
rebuilds the profile at the source lower Z and extrudes it by source height plus the offset.

The result must preserve the source's exact topology and ellipse radii/orientation, with volume
following the analytic identity `pi * 6 * 3 * (5 + 2)` within the kernel's NURBS volume tolerance.

## Refusal boundary

The route refuses circular cylinders, polygonal prisms, holed/multi-loop profiles, lower or side
faces, non-elliptic freeform curves, tilted/non-prismatic supports, zero/negative/non-finite
distances, malformed topology, and healing-dependent fallback. The public `OffsetFace` dispatcher
may delegate this exact ellipse case only after the box, pentagonal, and holed-prism routes decline.

## Verification and proof

`EllipticalPrismFaceOffsetVerification` independently constructs the ellipse prism and checks exact
analytic classification, source/result topology, major/minor radii, orientation, volume, source
immutability, public dispatch, deterministic refusal boundaries, and malformed transactional
behaviour. The durable proof will be `Proofs/Phase43_EllipticalPrismFaceOffset.png`.
