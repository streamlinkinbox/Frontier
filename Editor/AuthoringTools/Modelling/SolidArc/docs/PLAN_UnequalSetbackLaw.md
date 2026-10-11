# Bounded unequal support-setback corner blend

The common-setback routes use one clearance law for both perpendicular supports. This slice
adds independent positive linear laws for the two supports and refuses equal laws so it cannot
be a renamed copy of that route.

## Accepted construction

For station `t`:

- `r(t)` is the positive linear rolling radius;
- `sA(t)` and `sB(t)` are independent positive linear clearances;
- the two support extents are `u(t) = r(t) + sA(t)` and `v(t) = r(t) + sB(t)`;
- the meridian remains an exact quarter-circle of radius `r(t)`;
- four boundary surfaces are lofted between the two non-square sections and sewn with the
  quarter-circle surface and planar end caps.

The result is a one-hull `V10/E15/C30/L7/F7` solid. Its analytic volume is the integral of the
product `u(t)v(t)` minus the integrated quarter-circle corner removal.

## Acceptance checks

`UnequalSetbackCornerVerification` proves independent endpoint laws, station identities,
analytic volume, topology, outward normals, equal-law refusal, non-positive-law refusal,
degenerate-frame refusal, source/result validity after refusals, and a distinct comparison proof.

## Explicit boundary

This is still one complete finite straight edge with perpendicular planar supports. It does not
implement partial edges, rolling-ball G2 continuity, curved or freeform supports, nonlinear
setback laws on both supports, or general intersection/trim/sew healing.
