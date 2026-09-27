# Plan — Phase 51 bounded revolved-annular-prism face offset

## Capability slice

Advance face editing into a revolved genus-one solid: a full-turn annular prism, or washer, made by
revolving one radial rectangular profile around the Z axis. A positive offset of its upper annular
cap extends the axial height while preserving the inner and outer circular rims.

This is a new revolved topology/support domain after the quarter-sector prism. It is not a native
single-cylinder cap tweak, a cone/bicone apex route, a polygonal prism, or a Boolean/healing path.

The focused fixture uses inner radius `4`, outer radius `6`, height `4`, and positive upper-cap offset
`1.5`. Its exact source/result topology is `V4/E8/C16/L4/F4`, genus one.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetRevolvedAnnularPrism`. It accepts only a closed one-hull genus-one full-
turn revolved annular solid with two planar annular revolution caps, two cylindrical revolution walls,
exactly two circular rims per cap, and its upper annular cap selected.

The route rebuilds the radial rectangular profile at the source lower level and revolves it through
exactly `2π` about the source Z axis at the extended height. It validates the exact topology, all
revolution supports, inner/outer radius preservation, and the annular-area volume identity.

## Refusal boundary

Refuse lower/side faces, boxes, solid cylinders, cones, partial-turn revolutions, non-annular profiles,
polygonal/ellipse/sector prisms, holes through the new API, tilted/oblique/freeform/mixed supports,
invalid offsets, malformed topology, and healing-dependent fallback.

## Verification and proof

`RevolvedAnnularPrismFaceOffsetVerification` constructs the annular prism and checks source/result
`V4/E8/C16/L4/F4`, genus one, two annular caps, two inner/outer circular rims, four revolution
supports, the annular-area volume identity, source immutability, public dispatch, and all refusal
boundaries. The durable proof will be `Proofs/Phase51_RevolvedAnnularPrismFaceOffset.png`.
