# Stage 4q — bounded native-cone apex vertex dispatch

Add the smallest vertex-selection layer over the verified Stage 4b cone-apex spherical-cap route.
Classify one explicit vertex from an existing native right-circular cone B-rep only when the vertex
is the unique apex of the exact canonical cone topology, then return the existing
`ConeApexFilletSpecification`. This is not general apex filleting.

## Accepted construction

`ClassifyConeApexFilletVertex(body, vertex, filletRadius)` accepts:

- one in-range vertex on a closed, oriented, genus-zero native cone solid;
- the canonical two-vertex/two-edge/two-face cone topology;
- one native cone side with a positive base radius and zero apex radius, one planar base, one closed
  circular base rim, and one straight apex seam;
- the unique vertex on the cone axis above the base plane;
- a positive fillet radius that leaves a positive conical frustum;
- a source body that remains untouched while returning `ConeApexFilletSpecification`.

## Refusal boundary

Refuse the base-rim vertex, out-of-range vertices, cylinders and non-cone bodies, invalid or
consuming radii, malformed/non-manifold bodies, non-native or mixed-support apexes, partial sectors,
and arbitrary vertex-selected fillets. Reconstruction remains a separate transactional operation.

## Acceptance checks

The distinct verifier must cover explicit apex discovery, exact base/axis/radius/height extraction,
separate spherical-cap reconstruction, `V4/E5/F3/L3` topology, outward normals, source immutability,
and refusals for the base vertex, invalid/out-of-range/consuming requests, curved non-cone sources,
and malformed topology. The proof uses a distinct native cone size so it is not a duplicate of the
existing Stage 4b specification-only image.
