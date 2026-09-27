# Plan — Phase 54 bounded elliptical-annular-prism face offset

## Capability slice

Advance exact face editing into a genus-one elliptical profile: a straight prism with one exact
rational elliptical through-hole. A positive offset of its upper planar annular cap extends the height
while preserving both the outer and inner ellipse rims and the analytic extrusion walls.

This is distinct from Phase 43's single-loop elliptical prism and Phase 42's circular-holed prism:
it combines a new multi-loop topology with an exact elliptical inner support. It is not a polygonal
approximation, a generic hole fallback, a Boolean/healing path, or a cone-family operation.

The focused fixture uses an outer ellipse with semi-axes `6` and `3`, an inner ellipse with semi-axes
`2.0` and `1.0`, source height `5`, and positive upper-cap offset `1.5`. Source and result retain the
closed genus-one topology produced by two exact elliptical loops.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism`. It accepts only a straight Z extrusion with
one outer and one inner exact rational elliptical loop on each planar cap, the selected upper annular
cap, two elliptical extrusion walls, and no additional loops or supports.

The result is rebuilt by extruding the same two ellipse loops from the source lower Z level through the
extended height. Both ellipse identities, analytic planar/extrusion supports, topology, and volume
identity `π(outerA·outerB − innerA·innerB)h` are preserved.

## Refusal boundary

Refuse single-loop elliptical prisms through this API, circular holes, rectangular/polygonal holes,
multiple holes, lower/side faces, cylinders, toruses, spheres, tilted/oblique/freeform/mixed supports,
invalid offsets, malformed topology, and healing-dependent fallback. The operation is copy-in/copy-out.

## Verification and proof

`EllipticalAnnularPrismFaceOffsetVerification` constructs the exact two-loop prism and checks source/result
topology, both outer and inner ellipse rims, analytic planar/extrusion supports, volume identity, source
immutability, public dispatch, refusal boundaries, and the durable proof
`Proofs/Phase54_EllipticalAnnularPrismFaceOffset.png`.
