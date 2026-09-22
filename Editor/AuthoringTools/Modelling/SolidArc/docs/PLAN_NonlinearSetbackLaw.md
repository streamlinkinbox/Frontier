# Stage 3c plan — bounded nonlinear support-setback law

Stage 37b accepted a linear support-setback law and Stage 37c accepted a nonlinear radius law
with constant setback. This slice adds a genuinely nonlinear setback law while keeping the
rolling radius independently constant in its proof fixture.

## Accepted construction

At station `t ∈ [0, 1]`:

- the rolling radius law is a positive quadratic interpolant, and may be constant or linear;
- the support setback law is a positive, genuinely nonlinear quadratic interpolant through
  endpoint and middle-station values;
- support extent is `d(t) = r(t) + s(t)`;
- five quadratic lofts pass through the exact sections at `t = 0, 0.5, 1`.

The surface construction is therefore separate from the linear ruled route and retains the
exact quarter-circle section at each accepted station.

## Acceptance checks

The verifier establishes:

1. the setback is nonlinear and reaches its endpoint/middle values;
2. the radius remains independently constant in the proof fixture;
3. `d(t) - r(t) = s(t)` at multiple stations;
4. the result is a one-hull `V10/E15/C30/L7/F7` solid with outward normals;
5. volume matches the integrated quadratic outer extent minus the integrated corner removal;
6. linear, non-positive, interior-negative, degenerate, and zero-length requests refuse;
7. a nonlinear-setback result renders beside a linear-setback comparison.

## Explicit boundary

This remains one complete finite straight corner with common perpendicular support directions. It
does not implement unequal setback laws on the two supports, partial edges, apexes, freeform
supports, arbitrary healing, or rolling-ball G2 continuity. The latter remains a separate
construction problem because a circular rolling section has non-zero curvature at a planar join.
