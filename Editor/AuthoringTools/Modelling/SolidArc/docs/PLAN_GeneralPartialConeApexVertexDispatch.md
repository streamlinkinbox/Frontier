# Stage 4s — bounded non-reflex partial-cone apex vertex dispatch

Extend the Stage 4r half-turn vertex layer to one canonical non-reflex general-angle partial-cone
revolution. The source is deliberately restricted to the exact native revolve surface topology;
this is not arbitrary partial-sector healing or general apex filleting.

## Accepted construction

`ClassifyGeneralPartialConeApexFilletVertex(body, vertex, filletRadius)` accepts:

- the canonical non-reflex partial-cone revolve surface set with `V4/E6/C8/L3/F3` topology and
  three native revolution surfaces;
- one base-center/axis-apex pair with two equal positive base-rim radii;
- a strict sweep `0 < sweep < pi`, derived from the two rim vectors;
- the unique axis apex vertex as the selected vertex;
- a positive fillet radius leaving a feasible partial frustum and spherical cap.

The returned `PartialConeApexFilletSpecification` is checked by the existing transactional partial
spherical-cap route. The source sheet/B-rep remains untouched.

## Refusal boundary

Refuse the half-turn route (Stage 4r), full turns, reflex/zero sweeps, base/rim vertices, invalid or
consuming radii, cylinders/full cones, malformed topology, and arbitrary partial-sector or mixed-
support apex fillets. No healing, source replacement, or freeform support inference is added.

## Acceptance checks

The distinct verifier must cover explicit non-reflex apex discovery, exact base/axis/radius/height/
sweep extraction, separate `V6/E9/F5/L5` reconstruction, outward normals, source immutability,
and refusals for half/full/non-reflex-invalid sources, non-apex vertices, invalid/consuming radii,
non-cone sources, and malformed topology. The proof uses a distinct 60-degree native revolve.
