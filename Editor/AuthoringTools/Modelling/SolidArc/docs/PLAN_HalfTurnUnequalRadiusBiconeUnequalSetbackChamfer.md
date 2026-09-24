# Plan — bounded half-turn unequal-radius bicone chamfer with independent set-backs

## Capability slice

Add the exact half-turn unequal-radius companion to the independent-setback bicone chamfer family.
The source is one capped half-turn sector of two coaxial right-circular cones with a shared apex
and distinct support radii. Lower and upper axial set-backs are positive, unequal, and independently
below their corresponding support heights.

The distinct fixture uses lower/upper radii 4.8/2.6, lower/upper heights 6.7/4.3, a half-turn
sweep, and lower/upper set-backs 0.75/1.15.

## Source and reconstruction contract

The classifier accepts only the capped native half-turn topology `V7/E11/C22/L6/F6`: four
one-loop revolution faces, two planar radial caps, two rational circular rim edges, and one shared
apex incident to four straight generator edges. It requires unequal support radii, two positive
unequal set-backs below their corresponding support heights, derives the apex, canonical axis,
radii, support heights, and exactly `pi` sweep, and leaves the source unchanged.

`ReconstructHalfTurnUnequalRadiusBiconeUnequalSetbackChamfer` retains both support frusta, inserts
a conical bridge between independently placed contact rings, and closes both radial sector ends.
It returns closed genus-zero `V10/E15/C30/L7/F7` topology with three cone faces, two axial base
planes, and two radial sector planes. Volume is checked against the exact three-frustum identity
scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only exact half-turn unequal-radius coaxial bicone sources with unequal set-backs.
Equal set-backs remain on their existing equal-setback routes; complete, strict non-half partial, or
reflex sectors, equal radii, mixed cone/plane/cylinder supports, oblique/non-coaxial or freeform
supports, arbitrary vertices, invalid or consuming set-backs, and healing remain refused.

## Verification and proof

`HalfTurnUnequalRadiusBiconeUnequalSetbackChamferVerification` performs failure-oriented checks for
exact half-turn and unequal-radius extraction, independent set-backs, contact radii, topology,
radial-cap closure, normals, sector volume, source immutability, deterministic dispatch, and
complete/partial/reflex and unsupported refusal boundaries. The distinct proof is
`Proofs/Phase39j_HalfTurnUnequalRadiusBiconeUnequalSetbackChamfer.png`.
