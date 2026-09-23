# Stage 4i — bounded rolling-ball-core G2 planar corner transition

This slice adds a distinct, bounded transition for the long-standing rolling-ball G2 gap. It
must not relabel the existing Stage 3b quintic profile: that route is a non-rolling profile and
remains separately verified.

## Accepted construction

Accept one explicit finite straight edge, two perpendicular planar supports, one positive constant
radius, and one strict transition angle. The cross-section consists of:

- a quintic support transition from each plane whose endpoint curvature is zero;
- one exact rational circular rolling-ball core of radius `r`;
- G2 curvature matching at both plane/transition joins and both transition/circular-core joins;
- extrusion along the explicit edge and fixed retained support boundaries;
- explicit finite end caps produced by the existing sewing route.

The rolling-ball claim is deliberately bounded: only the central core is an exact circular
rolling-ball section. The support transitions are curvature-matched polynomial pieces, not a claim
that a pure quarter-circle can be G2 to a plane. No arbitrary rolling-ball surface solver is
introduced.

## Acceptance checks

The dedicated verifier must establish:

1. transition angle, radius, edge length, and support width feasibility;
2. quintic transitions with zero support curvature;
3. exact rational circular core and core radius residual;
4. curvature continuity at transition/core joins within a declared tolerance;
5. closed one-hull genus-zero topology, outward normals, positive volume, and deterministic seams;
6. volume from the Green line integral of the exact profile pieces;
7. transactional refusals for zero/negative radius, consuming width, invalid transition angle,
   degenerate edge, non-perpendicular frame, partial request, variable radius, and apex request;
8. a distinct filled proof comparing the G2 rolling-core result with the existing pure rolling
   quarter-cylinder fixture.

## Unsupported boundary

Arbitrary edge selection, curved/freeform supports, oblique supports, variable-radius rolling
cores, pure quarter-circle G2-to-plane joins, rolling-ball G2 on arbitrary B-reps, apexes, and
general healing remain explicit refusals. Stage 3b's quintic non-rolling G2 route is not changed
or renamed.
