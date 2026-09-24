# Plan — bounded half-turn equal-radius bicone chamfer with independent set-backs

## Capability slice

Add the exact half-turn companion to the complete-turn and strict non-reflex equal-radius bicone
independent-setback chamfers. The source is one capped half-turn sector of two coaxial right-circular
cones with equal support radii and a shared apex. Lower and upper axial set-backs are positive,
unequal, and independently below their corresponding support heights.

The distinct fixture uses equal radius 3.6, lower/upper heights 6.2/4.7, a half-turn sweep, and
lower/upper set-backs 0.7/1.1.

## Source and reconstruction contract

The classifier accepts only the capped native half-turn topology `V7/E11/C22/L6/F6`, requires four
revolution faces, two radial planar caps, equal circular support rims, and two positive unequal
set-backs below their corresponding support heights. It derives the shared apex, canonical axis,
equal radius, support heights, and exactly `pi` sweep without mutating the source.

`ReconstructHalfTurnEqualRadiusBiconeUnequalSetbackChamfer` retains both support frusta, inserts
a conical bridge between independently derived contact rings, and closes the two radial sector ends.
It returns closed genus-zero `V10/E15/C30/L7/F7` topology with three cone faces, two axial base
planes, and two radial sector planes. Volume is checked against the exact three-frustum identity
scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only exactly half-turn equal-radius coaxial bicone sources with unequal set-backs.
Equal set-backs remain on Phase 39h. Complete, strict non-reflex partial, or reflex sectors, unequal
radii, mixed cone/plane/cylinder supports, freeform or non-coaxial geometry, arbitrary vertices,
invalid or consuming set-backs, and healing remain refused.

## Verification and proof

`HalfTurnEqualRadiusBiconeUnequalSetbackChamferVerification` performs failure-oriented checks for
exact half-turn and equal-radius extraction, independent set-backs, contact radii, topology, radial
cap closure, normals, volume, source immutability, deterministic dispatch, and complete/partial/
reflex and unsupported refusal boundaries. The distinct proof is
`Proofs/Phase39i_HalfTurnEqualRadiusBiconeUnequalSetbackChamfer.png`.
