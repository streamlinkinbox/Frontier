# Plan — Phase 46 bounded genus-two twin-holed-prism face offset

## Capability slice

Cross the face-offset family into a distinct higher-genus topology: a rectangular prism with two
separate circular through-holes. A positive offset of the selected upper cap extends the source
while preserving both holes, three cap loops, and genus two.

This is not another cone/bicone blend, radius/sweep/set-back variant, or renamed Phase 42 fixture.
It proves a new multi-loop topology (`genus 2`) and the corresponding deterministic loop pairing
and reconstruction. The bounded route remains a face-edit offset; it does not attempt general
multi-loop healing or arbitrary hole counts.

The focused fixture uses an outer rectangle `16 x 10`, height `6`, two radius-1.25 holes centred at
`(-4,0)` and `(4,0)`, and offset distance `1.5`. Its exact source/result topology is
`V12/E18/C36/L12/F8`.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetExtrudedTwinHoledPrism`. It accepts only a closed single-hull genus-two
straight rectangular prism, one selected upper planar cap with one four-edge outer loop and exactly
two one-edge rational circular inner loops, axis-aligned line generators, and two Z levels. It
rebuilds the same outer rectangle and two circles at the source lower level with the extended
height, and validates the exact genus-two result.

The verifier checks the rectangle-minus-two-hole volume identity, all three upper/lower cap loops,
four exact circular rims, source immutability, and deterministic loop-independent reconstruction.

## Refusal boundary

The route refuses hole-free/one-hole prisms, three or more holes, overlapping or wall-consuming
holes, non-circular inner loops, non-rectangular outer loops, lower/side faces, boxes through the
new API, cylinders, tilted/non-prismatic/freeform supports, invalid offsets, malformed topology,
and healing-dependent fallback. The public `OffsetFace` dispatcher delegates this exact genus-two
case only after the existing bounded routes decline.

## Verification and proof

`TwinHoledPrismFaceOffsetVerification` independently constructs the genus-two source and checks
exact topology, two-hole loop classification, circular rim preservation, volume, source immutability,
public dispatch, loop-order determinism, refusal boundaries, and malformed transactional behavior.
The durable proof will be `Proofs/Phase46_TwinHoledPrismFaceOffset.png`.
