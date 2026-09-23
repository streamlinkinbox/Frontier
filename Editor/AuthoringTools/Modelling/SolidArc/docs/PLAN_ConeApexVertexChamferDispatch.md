# Stage 4u — bounded native-cone apex vertex chamfer dispatch

Add the smallest distinct vertex-selection layer for an apex chamfer: one explicit apex vertex
of the canonical native right-cone source. The route is intentionally not a general vertex
chamfer or a replacement for arbitrary edge-set chamfers.

## Accepted construction

`ClassifyConeApexChamferVertex(body, vertex, setBack)` accepts only:

- a closed native `BrepBody::Cone` with exact `V2/E2/C4/L2/F2` topology;
- one native right-cone surface and one planar base-cap surface, each with one loop;
- a positive base radius, finite height, zero top radius, unique base-rim/apex pair, and the
  selected apex vertex;
- a finite positive chamfer setback strictly smaller than the cone slant height.

The setback is measured along the cone's straight generator from the apex. Reconstruction retains
the base and replaces the apex with an exact planar cap: for slant `s = hypot(height, baseRadius)`,
the retained frustum height is `height - setback * height / s` and its cap radius is
`setback * baseRadius / s`. The result is constructed transactionally as an exact native frustum.

The returned `ConeApexChamferSpecification` retains the base, normalized axis, base radius,
height, and setback. Reconstruction remains a separate operation.

## Refusal boundary

Refuse base-rim and out-of-range vertices, zero/negative/non-finite/consuming setbacks, frusta,
cylinders, partial/revolved cones, non-native or malformed/non-manifold sources, arbitrary
vertex chamfers, mixed supports, freeform geometry, healing, and source-body replacement.

## Acceptance checks

The distinct verifier must cover exact source topology, exact parameter extraction, deterministic
dispatch, separate `V2/E3/C6/L3/F3` frustum reconstruction, exact cap geometry, outward analytic
normals, source immutability, and all refusal boundaries. The visual proof must use a distinct
sharp 5-by-7 cone beside its setback-chamfered result.
