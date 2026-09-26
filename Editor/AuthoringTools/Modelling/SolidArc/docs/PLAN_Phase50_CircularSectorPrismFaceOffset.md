# Plan — Phase 50 bounded circular-sector-prism face offset

## Capability slice

Cross the face-offset family into a new curved single-loop profile domain: a straight prism whose
planar cap is an exact quarter circular sector bounded by two radial lines and one rational circular
arc. A positive upper-cap offset extends the sector prism while preserving the analytic arc and both
radial seams.

This is distinct from the polygonal/concave prism routes and from the hole-free ellipse and circular
cylinder routes. It does not approximate the arc with a polyline, use Boolean healing, or widen
face offset to arbitrary curved profiles.

The focused fixture uses a radius-5 quarter sector centered at `(0,0)`, from angle `0` to `π/2`,
height `6`, and positive upper-cap offset `1.5`. Its exact source/result topology is
`V6/E9/C18/L5/F5`, genus zero.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetExtrudedCircularSectorPrism`. It accepts only a closed one-hull genus-zero
straight prism with two planar revolution caps, two radial line walls, one circular-arc extrusion
wall, an exact rational degree-2 quarter arc, and its upper planar sector cap selected.

The route rebuilds the exact sector caps with `NurbsSurface::Revolution`, the two radial walls and
arc wall with analytic extrusions, and extends only the Z height. It validates the exact topology,
closed manifold state, arc/radial support identities, and sector-area volume identity.

## Refusal boundary

Refuse lower/side faces, boxes, polygonal profiles, full cylinders, ellipse profiles, non-quarter or
non-circular arcs, holes, tilted/oblique/freeform/mixed supports, invalid offsets, malformed topology,
and healing-dependent fallback. The public `OffsetFace` dispatcher reaches this route only after the
existing bounded planar and polygonal routes decline.

## Verification and proof

`CircularSectorPrismFaceOffsetVerification` independently constructs the sector prism and checks
source/result `V6/E9/C18/L5/F5`, two analytic revolution caps, two radial extrusion walls, one arc
extrusion wall, exact radius and quarter sweep, the quarter-circle sector volume identity, source
immutability, public dispatch, and all refusal boundaries. The durable proof will be
`Proofs/Phase50_CircularSectorPrismFaceOffset.png`.
