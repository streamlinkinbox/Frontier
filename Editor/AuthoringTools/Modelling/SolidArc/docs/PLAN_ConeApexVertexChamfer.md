# Stage 4u — bounded native-cone apex vertex chamfer

Add one conservative vertex-selection layer for the canonical native right-circular cone. The
operation is intentionally not a general B-rep vertex chamfer: it recognizes one exact native
source, derives its geometry from topology, and reconstructs a separate truncated cone.

## Accepted construction

`ClassifyConeApexChamferVertex(body, vertex, setBack)` accepts only:

- a closed, manifold, genus-zero native cone with `V2/E2/C4/L2/F2` topology;
- one native conical face with positive base radius and zero apex radius;
- one planar base face, one circular base rim, and one straight base-to-apex seam;
- the unique axis apex vertex selected by the caller;
- a finite positive axial set-back strictly less than the recovered cone height.

The classifier derives the base, normalized axis, base radius, and height from the source. `SetBack`
is the axial distance removed from the apex. Reconstruction retains the base and axis and replaces
the sharp apex with a planar cap at `height - setBack`, whose exact radius is
`baseRadius * (height - setBack) / height`.

## Reconstruction and transaction boundary

`ReconstructConeApexChamfer` builds the exact native frustum through the existing cone surface and
capping route. The accepted result is a closed `V2/E3/C6/L3/F3` solid with one conical face and two
planar caps. It validates manifold topology, outward orientation, and the analytic frustum volume
before returning. Classification and reconstruction never mutate the source body.

## Refusal boundary

Refuse non-apex or out-of-range vertices, zero/negative/non-finite/consuming set-backs, frustums,
cylinders, non-native or malformed topology, mixed/freeform supports, arbitrary vertex-selected
chamfers, and general apex chamfer networks.

## Acceptance checks

`ConeApexChamferVerification` covers exact native source topology, exact base/normalized-axis/
radius/height/set-back extraction, deterministic dispatch, source immutability, separate
`V2/E3/C6/L3/F3` reconstruction, exact planar cap placement, outward normals, malformed/cylinder/
frustum and invalid-radius refusals, and a distinct sharp-versus-planar-cap proof:
`Proofs/Phase38u_ConeApexVertexChamfer.png`.
