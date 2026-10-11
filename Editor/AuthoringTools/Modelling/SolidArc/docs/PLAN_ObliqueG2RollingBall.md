# Stage 4j — bounded oblique rolling-ball-core G2 transition

Extend the completed Stage 4i rolling-ball-core G2 construction to one explicit strict
non-orthogonal planar wedge. This is a new route, not a renamed orthogonal fixture.

## Accepted construction

Accept one finite straight edge, two explicit planar support directions perpendicular to that edge,
one strict non-reflex interior angle, unequal positive support widths, one positive constant radius,
and one transition angle that leaves a circular core. The cross-section contains:

- a quintic zero-curvature transition from support A into the circular core;
- an exact rational circular core spanning the oblique tangent arc;
- a quintic circular-to-zero-curvature transition into support B;
- retained finite support strips and the outer support boundary;
- explicit finite end caps from sewing.

The exact tangent distance remains `r cot(theta/2)`. The circular core spans the oblique
removed-corner arc `pi - theta`; it is not a pure quarter-circle and does not claim a general
rolling-ball solver.

## Acceptance checks

The verifier must cover:

1. strict oblique angle, explicit frame orientation, widths, tangent-distance feasibility, and
   transition-core feasibility;
2. exact rational core radius and curvature `1/r`;
3. zero support curvature and G2 curvature matching at both transition/core joins;
4. closed one-hull genus-zero topology, outward normals, and deterministic split seams;
5. removed-corner area from the profile line integral and integrated extruded volume;
6. transactional refusals for orthogonal/parallel/edge-consuming frames, invalid angles,
   consuming widths, degenerate dimensions, and zero/negative radius;
7. a distinct sharp-versus-oblique-G2 exterior proof.

## Unsupported boundary

Variable radius, arbitrary edge selection, curved/freeform supports, mixed-support networks,
apexes, pure quarter-circle G2-to-plane joins, arbitrary B-rep rolling-ball G2, and general
healing remain explicit refusals. Stage 4i and Stage 3b remain unchanged.
