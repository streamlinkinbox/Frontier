# Plan — Phase 49 bounded orthogonal concave-prism side draft

## Capability slice

Advance the draft family into the non-convex planar-profile domain: an axis-aligned straight prism
whose profile is the six-edge orthogonal L-shaped concave loop from Phases 47–48. A selected vertical
side wall is drafted about the source Z direction by moving only its upper profile edge along the
wall's outward normal.

This is a distinct operation from Phase 47's upper-cap offset and Phase 48's shell/thicken route,
and extends the Phase 45 triangular side-draft idea into a reflex single-loop topology without
widening draft to arbitrary polygons or freeform B-reps.

The focused fixture uses the counter-clockwise L profile
`(-6,-4) -> (6,-4) -> (6,-1) -> (-1,-1) -> (-1,4) -> (-6,4)`,
height `6`, the wall along `y = -4`, and a positive draft angle of `12 degrees`. The exact source
and result topology is `V12/E18/C36/L8/F8`, genus zero.

## Source and reconstruction contract

Add `FaceEditSolver::DraftExtrudedConcavePrism`. It accepts only a closed one-hull genus-zero
axis-aligned orthogonal six-edge concave prism, one selected vertical extrusion wall, two Z levels,
and exactly one reflex upper/lower profile. It moves the selected high-Z profile edge by
`tan(angle) * height` along the selected wall's horizontal outward normal, then rebuilds six ruled
walls and two planar caps.

The route accepts bounded outward and non-collapsing inward angles, validates the exact topology and
concavity after drafting, and never mutates the source.

## Refusal boundary

Refuse boxes through the new API, convex or other-sided profiles, non-orthogonal concave profiles,
upper/lower cap selections, non-vertical/curved/oblique/freeform/mixed supports, zero/invalid/near-90
draft angles, collapsing or inverted profiles, malformed topology, and healing-dependent fallback.
The public `Draft` dispatcher reaches this route only after the existing box and triangular-prism
routes decline.

## Verification and proof

`ConcavePrismDraftVerification` checks exact source/result topology, selected-wall displacement,
unchanged opposite profile vertices, the mean-section L-profile volume identity, analytic
planar/ruled supports, source immutability, public draft dispatch, bounded inward acceptance, and
all refusal boundaries. The durable proof will be
`Proofs/Phase49_ConcavePrismDraft.png`.
