# Plan — bounded half-turn unequal-radius bicone apex chamfer

## Capability slice

Add the exact half-turn unequal-radius companion to the established complete-turn and strict
non-reflex partial unequal-radius bicone apex chamfers. The source is a capped half-turn sector of
two coaxial right-circular cones with a shared apex and distinct support radii. One positive axial
set-back replaces the sharp apex with a conical chamfer bridge while leaving positive retained
frusta on both supports.

The distinct fixture uses lower/upper radii 4.2/2.8, lower/upper heights 6.4/4.8, a half-turn
sweep, and set-back 0.9.

## Source and reconstruction contract

The classifier accepts only the capped native half-turn topology `V7/E11/C22/L6/F6`: four
one-loop revolution faces, two planar radial caps, two rational circular rim edges, and one shared
apex incident to four straight generator edges. It requires unequal circular support radii, derives
the shared apex, canonical axis, support radii, heights, and exactly `pi` sweep, and leaves the
source unchanged.

`ReconstructHalfTurnUnequalRadiusBiconeApexChamfer` retains both support frusta, inserts a conical
bridge at the shared axial set-back, and closes both radial sector ends. It returns closed genus-zero
`V10/E15/C30/L7/F7` topology with three cone faces, two axial base planes, and two radial sector
planes. Volume is checked against the exact three-frustum identity scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only exact half-turn unequal-radius coaxial bicone sources with one positive,
non-consuming set-back. Complete, strict non-half partial, or reflex sectors, equal radii, mixed or
freeform/non-coaxial supports, arbitrary vertices, invalid or consuming set-backs, and healing remain
refused. Independent lower/upper set-backs remain on their dedicated routes.

## Verification and proof

`HalfTurnUnequalRadiusBiconeApexChamferVerification` performs failure-oriented checks for exact
half-turn and unequal-radius extraction, contact rings, topology, radial-cap closure, normals,
sector volume, source immutability, and complete/partial/reflex and unsupported refusal boundaries.
The distinct proof is `Proofs/Phase39n_HalfTurnUnequalRadiusBiconeApexChamfer.png`.
