# Plan — bounded partial unequal-radius bicone chamfer with independent set-backs

## Capability slice

Add one finite-sector companion to the existing unequal-setback full-turn bicone chamfer. The
source remains one strict non-reflex partial sector of two coaxial right-circular cones with a
shared apex and unequal radii, but the lower and upper axial chamfer set-backs are independent.
Both sector ends are closed with radial planar caps.

The distinct fixture uses lower/upper radii 4.8/2.6, heights 6.7/4.3, a 110-degree sweep, and
lower/upper set-backs 0.75/1.15.

## Source and reconstruction contract

The classifier accepts only the capped native partial-bicone topology `V7/E11/C22/L6/F6`:
four one-loop revolution faces, two planar radial caps, two rational circular rim edges, and one
shared apex incident to four straight generator edges. It requires unequal radii, two positive
unequal set-backs below their corresponding support heights, derives the apex, canonical axis,
support heights, and native sweep, and leaves the source unchanged.

`ReconstructPartialUnequalBiconeUnequalSetbackChamfer` retains the finite sweep, preserves both
support frusta, inserts a conical bridge between independently placed contact rings, and closes
both radial ends. It returns closed genus-zero `V10/E15/C30/L7/F7` topology with three cone faces,
two axial base planes, and two radial sector planes. Volume is checked against the exact
three-frustum identity scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only strict non-reflex partial sectors with unequal radii and unequal set-backs.
Equal set-backs remain on the Phase 39a route; complete, half, or reflex sectors, equal radii,
mixed cone/plane/cylinder apexes, oblique/non-coaxial or freeform supports, variable-radius laws,
arbitrary vertices, invalid or consuming set-backs, and healing remain refused.

## Verification and proof

`PartialUnequalBiconeUnequalSetbackChamferVerification` performs failure-oriented checks for exact
independent-setback extraction, contact radii, topology, radial-cap closure, normals, sector volume,
source immutability, deterministic dispatch, and refusal boundaries. The distinct proof is
`Proofs/Phase39f_PartialUnequalBiconeUnequalSetbackChamfer.png`.
