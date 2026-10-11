# Stage 4t — bounded reflex partial-cone apex vertex dispatch

Extend the partial-cone apex vertex layer to one strictly reflex native partial-revolve sector.
The source selection remains deliberately structural and narrow: derive the actual sweep by
unwrapping the native base-rim curve, rather than confusing a reflex sweep with its minor endpoint
angle.

## Accepted construction

`ClassifyReflexPartialConeApexFilletVertex(body, vertex, filletRadius)` accepts:

- the exact native partial-revolve surface set with `V4/E6/C8/L3/F3` topology;
- three native revolution surfaces, a unique base-center/axis-apex pair, and two equal positive
  base-rim vertices;
- a sampled circular base-rim path whose unwrapped sweep is strictly greater than `pi` and less
  than `2*pi`;
- the unique axis apex vertex and a positive fillet radius that leaves the reflex partial frustum.

The returned `PartialConeApexFilletSpecification` retains the actual reflex sweep. Reconstruction
remains a separate transactional operation.

## Refusal boundary

Refuse non-apex vertices, zero/non-reflex/half/full-turn sources, invalid or consuming radii,
non-native/malformed/non-manifold sources, mixed/freeform supports, arbitrary partial sectors, and
general vertex-selected apex fillets.

## Acceptance checks

The distinct verifier must cover actual reflex-sweep extraction, exact base/axis/radius/height,
separate `V6/E9/F5/L5` reconstruction, outward normals, source immutability, deterministic dispatch,
and refusals for non-reflex/half/full sweeps, non-apex/invalid/consuming requests, cylinders, and
malformed topology. The proof uses a distinct 270-degree, 5-by-7 partial cone.
