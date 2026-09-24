# Plan — bounded full-turn equal-radius bicone chamfer with independent set-backs

## Capability slice

Add the full-turn equal-radius companion to the existing independent-setback bicone chamfer.
The source is one native point-contact bicone: two coaxial right-circular cones with equal base
radii, opposite support directions, and one shared apex. The lower and upper axial chamfer
set-backs are positive, unequal, and independently below their corresponding support heights.

The distinct fixture uses equal radius 3.6, lower/upper heights 6.2/4.7, and lower/upper
set-backs 0.7/1.1.

## Source and reconstruction contract

The classifier accepts only the native full-turn point-contact bicone topology `V5/E6/C12/L4/F4`,
requires equal circular rims and two positive unequal set-backs below their corresponding support
heights, and derives the shared apex, canonical axis, equal radius, support heights, and full turn
without mutating the source.

`ReconstructEqualRadiusBiconeUnequalSetbackChamfer` retains both equal-radius support frusta,
inserts a conical bridge between independently placed contact rings, and preserves the two axial
base caps. It returns closed genus-zero `V6/E9/C18/L5/F5` topology with three cone faces and two
planar base faces. Volume is checked against the exact three-frustum identity.

## Refusal boundary

This route accepts only complete-turn equal-radius coaxial bicone sources with unequal set-backs.
Equal set-backs remain on Phase 39e. Partial, half-turn, or reflex sectors, unequal radii, mixed
cone/plane/cylinder supports, oblique or non-coaxial geometry, freeform supports, arbitrary
vertices, invalid or consuming set-backs, and healing remain refused.

## Verification and proof

`EqualRadiusBiconeUnequalSetbackChamferVerification` performs failure-oriented checks for exact
equal-radius and independent-setback extraction, contact radii, topology, normals, volume, source
immutability, deterministic dispatch, and full/half/partial/reflex and unsupported refusal
boundaries. The distinct proof is
`Proofs/Phase39h_EqualRadiusBiconeUnequalSetbackChamfer.png`.
