# Plan — Phase 45 bounded triangular-prism side draft

## Capability slice

Add the next direct-modelling operation beyond face offset: a bounded draft on one side of a
straight triangular prism. The source is an axis-aligned triangular prism with three vertical
extrusion faces. Selecting one wall and supplying a finite angle moves the corresponding upper
profile edge outward/inward by `tan(angle) * height`, creating a ruled drafted wall while retaining
the other two profile edges and the exact triangular-prism topology.

This is a non-cone draft domain, not another apex fillet/chamfer, radius, sweep, or offset variant.
It advances the broader face-edit operation family beyond the existing canonical-box `Draft` route.
The focused fixture uses profile points `(-4,-2)`, `(5,-2)`, `(0,4)`, height `6`, and draft angle
`12 degrees` on the outward-facing edge from `(-4,-2)` to `(5,-2)`.

## Source and reconstruction contract

Add `FaceEditSolver::DraftExtrudedTriangularPrism`. It accepts only a closed genus-zero
`V6/E9/C18/L5/F5` axis-aligned triangular prism, one selected vertical extrusion face, and a finite
non-zero angle strictly below 90 degrees. It extracts the selected profile edge and outward planar
normal, moves only that edge at the high-Z section, rebuilds three ruled walls and planar caps, and
validates the same closed topology.

The verifier checks the drafted wall's top-edge displacement, unchanged opposite vertices, planar/
ruled support classes, closed topology, volume identity `height * (source triangle area + drafted
top triangle area) / 2`, source immutability, and deterministic public/route behavior.

## Refusal boundary

The route refuses boxes, pentagonal/hexagonal/oblique prisms, cylinders, ellipse or multi-loop
profiles, non-vertical or non-planar supports, cap selections, zero/invalid/near-90-degree angles,
collapsing drafts, malformed topology, and healing-dependent fallback. The existing canonical-box
`Draft` route remains separate; the public `Draft` dispatcher delegates only the exact triangular
case after the box route declines.

## Verification and proof

`TriangularPrismDraftVerification` independently constructs the source and checks source/result
`V6/E9/C18/L5/F5` topology, selected-wall identity, exact displacement and volume, support
classifications, source immutability, public dispatch, refusal boundaries, and malformed
transactional behavior. The durable proof will be `Proofs/Phase45_TriangularPrismDraft.png`.
