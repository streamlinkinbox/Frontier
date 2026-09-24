# Plan — bounded partial equal-radius bicone chamfer with independent set-backs

## Capability slice

Add the equal-radius companion to the partial unequal-radius independent-setback chamfer. The
source remains one strict non-reflex partial sector of two coaxial right-circular cones with equal
base radii and a shared apex, while the lower and upper axial chamfer set-backs are independent.
Both sector ends are closed by radial planar caps.

The distinct fixture uses equal radius 3.7, lower/upper heights 6.4/4.8, a 115-degree sweep, and
lower/upper set-backs 0.65/1.05.

## Source and reconstruction contract

The classifier accepts only the capped native partial-bicone topology `V7/E11/C22/L6/F6`, requires
equal circular rims and two positive unequal set-backs below their corresponding support heights,
and derives the shared apex, canonical axis, equal radius, support heights, and native sweep without
mutating the source.

`ReconstructPartialEqualRadiusBiconeUnequalSetbackChamfer` retains both equal-radius support frusta,
inserts a conical bridge between independently placed contact rings, and closes both radial ends.
It returns closed genus-zero `V10/E15/C30/L7/F7` topology with three cone faces, two axial base
planes, and two radial sector planes. Volume is checked against the exact three-frustum identity
scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only equal-radius strict non-reflex partial sectors with unequal set-backs. Equal
set-backs remain on Phase 39d; complete, half, or reflex sectors, unequal radii, mixed
cone/plane/cylinder apexes, oblique/non-coaxial or freeform supports, variable-radius laws,
arbitrary vertices, invalid or consuming set-backs, and healing remain refused.

## Verification and proof

`PartialEqualRadiusBiconeUnequalSetbackChamferVerification` performs failure-oriented checks for
exact equal-radius and independent-setback extraction, contact radii, topology, radial-cap closure,
normals, sector volume, source immutability, deterministic dispatch, and refusal boundaries. The
distinct proof is `Proofs/Phase39g_PartialEqualBiconeUnequalSetbackChamfer.png`.
