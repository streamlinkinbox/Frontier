# Stage 4m — bounded eligible-edge dispatch for G2 corner reconstruction

Add the smallest selection layer after the explicit G2 routes: classify one user-supplied edge from
an existing B-rep only when it is a straight, manifold, rectangular planar corner with perpendicular
supports. The classifier produces the already-verified explicit G2 specification; it does not heal,
modify, or silently approximate the source body.

## Accepted construction

`ClassifyG2RollingBallEdge(body, edge, radius, transitionAngle)` accepts:

- one in-range edge index with exactly two adjacent faces;
- a straight edge shared by two planar rectangular faces;
- a strict orthogonal interior corner and resolvable in-face directions;
- finite positive radius and transition angle accepted by the Stage 4i G2 route;
- widths measured from the adjacent face vertices, with deterministic support ordering.

The returned explicit specification is then passed to the existing transactional
`ReconstructG2RollingBallPlanarCorner` route. Classification leaves the source untouched.

## Acceptance checks

The verifier covers valid edge discovery, deterministic frame/width/radius extraction, explicit
reconstruction, source immutability, and refusals for out-of-range, curved, non-rectangular,
non-orthogonal, non-manifold, unequal-width, consuming-radius, and invalid-transition requests.

## Unsupported boundary

This is not arbitrary edge selection, whole-B-rep filleting, oblique dispatch, curved/freeform
support selection, edge-loop selection, healing, or source-body replacement. Those remain explicit
refusals.
