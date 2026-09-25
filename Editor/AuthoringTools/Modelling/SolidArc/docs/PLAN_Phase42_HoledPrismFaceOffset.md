# Plan — Phase 42 bounded holed-prism face offset

## Capability slice

Add the next face-edit route in a genuinely different topology domain: a genus-one straight prism
with a through-hole. The source is an axis-aligned rectangular prism whose upper planar cap contains
one exact circular inner loop. A positive offset of that selected annular cap extends the prism in
+Z while preserving the through-hole, both cap loops, and the genus-one topology.

This is deliberately not another cone/bicone blend, not a shell/thicken operation, and not a
Boolean healing path. It exercises multi-loop face selection and closed genus-one output, which the
previous pentagonal-prism phase did not cover.

## Source and reconstruction contract

Add a separate `FaceEditSolver::OffsetExtrudedHoledPrism` route. It accepts only a valid closed
single-hull genus-one extrusion with a four-edge rectangular outer cap loop, one exact rational
circular inner cap loop, vertical axis-aligned generators, and the selected upper annular cap. A
finite positive distance rebuilds the same outer rectangle plus the same circular through-hole at
the
extended height.

The focused fixture is an outer rectangle `12 x 9`, height `6`, with a centered radius-1.5 hole and
an offset distance of `1.5`. The verifier records the exact source/result topology from the kernel,
requires genus one and one annular cap, and checks the volume identity
`(12 * 9 - pi * 1.5^2) * (6 + 1.5)`.

## Refusal boundary

The route refuses hole-free boxes, pentagonal-prism and hex-prism routes, multiple holes, non-circular
inner loops, non-rectangular outer loops, lower-cap selections, side faces, zero/negative/non-finite
offsets, tilted/non-prismatic/curved/freeform supports, malformed topology, and healing-dependent
fallback. The public `OffsetFace` dispatcher may delegate only the exact holed-prism case after the
legacy box and Phase 41 pentagonal routes decline.

## Verification and proof

`HoledPrismFaceOffsetVerification` independently constructs the genus-one source and checks source
and result topology, annular cap identity, rational circular hole preservation, exact volume,
source immutability, public dispatch, deterministic refusal boundaries, and malformed transactional
behavior. The durable proof will be `Proofs/Phase42_HoledPrismFaceOffset.png`.
