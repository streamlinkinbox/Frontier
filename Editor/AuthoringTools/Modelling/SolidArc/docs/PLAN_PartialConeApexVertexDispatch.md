# Stage 4r — bounded half-turn partial-cone apex vertex dispatch

Extend the native-cone vertex selection layer to one explicit half-turn partial cone. This is the
smallest distinct vertex-selection step over the verified Stage 4d partial spherical-apex route;
it does not claim general partial-sector or arbitrary apex filleting.

## Accepted construction

`ClassifyPartialConeApexFilletVertex(body, vertex, filletRadius)` accepts only:

- a closed, oriented half-turn partial-cone B-rep with the canonical `V4/E6/C12/L4/F4`
  topology;
- one planar base, three native revolution side/meridian surfaces, a unique base-center/axis pair,
  two equal-radius base-rim vertices, and a strict half-turn between those rim radii;
- the unique axis vertex above the base plane as the selected apex;
- a positive fillet radius that leaves a positive partial frustum and spherical cap.

The returned `PartialConeApexFilletSpecification` uses the derived base, axis, radius, height, and
`SweepAngle = pi`. Reconstruction remains separate and transactional.

## Refusal boundary

Refuse base or rim vertices, full/general sweeps, non-half-turn sectors, native full cones, invalid or
consuming radii, malformed/non-manifold bodies, non-coaxial/mixed/freeform apexes, and arbitrary
vertex-selected partial fillets.

## Acceptance checks

The distinct verifier must cover explicit half-turn apex discovery, exact base/axis/radius/height
extraction, separate `V6/E9/F5/L5` reconstruction, outward normals, source immutability, and
refusals for non-apex vertices, full/non-half-turn sources, invalid/consuming radii, non-cone
sources, and malformed topology. The proof uses a distinct half-turn cone size.
