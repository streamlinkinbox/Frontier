# Stage 4n — bounded eligible-edge dispatch for oblique G2 reconstruction

Extend the Stage 4m selection layer by one deliberately bounded step: classify one explicit edge
from an existing B-rep when it is a strict non-orthogonal planar rectangular corner, then return
the already-verified explicit oblique G2 specification. This is application dispatch, not a general
B-rep fillet solver, and it leaves the source body untouched.

## Accepted construction

`ClassifyObliqueG2RollingBallEdge(body, edge, radius, transitionAngle)` accepts:

- one in-range edge with exactly two adjacent faces;
- a straight manifold edge shared by two planar rectangular faces;
- a strict non-orthogonal, non-reflex interior corner;
- finite positive support widths extracted from the adjacent face vertices;
- unequal widths are allowed because the existing oblique specification has `WidthA` and `WidthB`;
- finite positive radius and transition angle accepted by the Stage 4j oblique G2 route;
- deterministic support-vector orientation.

The returned `ObliqueG2RollingBallPlanarCornerSpecification` is passed to the existing transactional
reconstruction route. The source B-rep remains unchanged.

## Refusal boundary

Refuse orthogonal selections (already covered by Stage 4m), curved/non-planar/non-rectangular
supports, non-manifold or curved edges, invalid radius/transition inputs, consuming tangent
setbacks, degenerate/non-reflex corners, and invalid source bodies. Do not add edge loops, healing,
source-body replacement, freeform support handling, or arbitrary B-rep rolling-ball G2.

## Acceptance checks

The distinct verifier must cover oblique edge discovery, exact frame/angle/width/radius extraction,
separate oblique G2 reconstruction, source immutability, deterministic orientation, outward normals,
`V12/E18/F8/L8` topology, and transactional refusals for orthogonal, curved, non-rectangular, non-manifold,
consuming-radius, and invalid-transition selections, while accepting the unequal-width oblique case.
