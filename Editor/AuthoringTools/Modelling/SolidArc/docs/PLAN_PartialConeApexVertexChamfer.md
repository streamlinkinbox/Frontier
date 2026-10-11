# Stage 4v — bounded native partial-cone apex vertex chamfer

Extend apex-chamfer vertex selection to one exact native partial-cone sector. This is a distinct
operation from the Stage 4u complete-cone axial chamfer and from the existing partial-cone spherical
fillet dispatches: the sharp apex is replaced by a planar cap while the native angular sector is
retained.

## Accepted construction

`ClassifyPartialConeApexChamferVertex(body, vertex, setBack)` accepts only:

- a closed, manifold native partial-revolve source with `V4/E6/C8/L3/F3` topology;
- exactly three single-loop native revolution faces;
- one base-center vertex, one axis-apex vertex, and two equal positive base-rim vertices;
- one rational quadratic circular base-rim curve whose unwrapped sweep is strictly between zero and
  a full turn;
- the unique axis apex vertex selected by the caller;
- a finite positive axial set-back strictly less than the recovered height.

The classifier unwraps the native base-rim curve, so the retained `SweepAngle` is the actual native
sweep rather than a minor endpoint angle. The route accepts non-reflex, half-turn, and reflex partial
sectors without creating duplicate proofs for the already-covered fillet stages.

## Reconstruction and transaction boundary

`ReconstructPartialConeApexChamfer` builds the retained frustum, base and apex planar sectors, and
two radial end caps as exact analytic/revolved surfaces. The accepted result is a closed
`V6/E9/C18/L5/F5` solid. It validates manifold topology, outward orientation, and the analytic
sector-frustum volume before returning. Classification and reconstruction never mutate the source.

## Refusal boundary

Refuse non-apex or out-of-range vertices, zero/negative/non-finite/consuming set-backs, zero/full-turn
or malformed sweeps, cylinders, complete native cones, non-native/freeform/mixed supports, arbitrary
vertex-selected chamfers, and general intersection/healing requests.

## Acceptance checks and proof uniqueness

`PartialConeApexVertexChamferVerification` performs 29 checks using a distinct 120-degree, 5-by-7
partial cone. It covers exact base/normalized-axis/radius/height/sweep/set-back extraction,
deterministic dispatch, separate `V6/E9/C18/L5/F5` reconstruction, exact frustum radii, sector
volume, outward normals, source immutability, invalid/non-apex/full-turn/cylinder/malformed refusals,
and the unique planar-cap proof:

`Proofs/Phase38v_PartialConeApexVertexChamfer.png`

The proof does not reuse the 60-degree, half-turn, 270-degree, or complete-cone fixtures from earlier
stages, and it is not a renamed spherical-filleting image.
