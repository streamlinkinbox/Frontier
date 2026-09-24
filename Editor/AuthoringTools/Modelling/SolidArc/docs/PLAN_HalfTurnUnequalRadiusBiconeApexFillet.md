# Plan — bounded half-turn unequal-radius bicone apex toroidal fillet

## Capability slice

Add the exact half-turn unequal-radius companion to the established full-turn and strict non-reflex
partial unequal-radius bicone apex toroidal fillets. The source is a capped half-turn sector of two
coaxial right-circular cones with a shared apex and distinct support radii. The selected fillet
radius must be positive and leave both conical supports and the toroidal core feasible.

The distinct fixture uses lower/upper radii 4.5/2.7, lower/upper heights 6.8/4.6, a half-turn
sweep, and fillet radius 0.62.

## Source and reconstruction contract

The classifier accepts only the capped native half-turn topology `V7/E11/C22/L6/F6`: four
one-loop revolution faces, two planar radial caps, two rational circular rim edges, and one shared
apex incident to four straight generator edges. It requires unequal circular support radii, derives
the shared apex, canonical axis, support radii, heights, and exactly `pi` sweep, and leaves the
source unchanged.

`ReconstructHalfTurnUnequalRadiusBiconeApexFillet` solves the two unequal-radius cone tangencies,
revolves two retained support cones, one analytic torus, and two axial base caps over the half-turn,
and closes both radial sector ends. It returns closed genus-zero `V10/E15/C30/L7/F7` topology with
two cone faces, one torus, two axial base planes, and two radial sector planes. Volume is checked
against the exact cone-plus-torus meridian identity scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only exact half-turn unequal-radius coaxial bicone sources with feasible positive
fillet radii. Complete, strict non-half partial, or reflex sectors, equal radii, mixed or
freeform/non-coaxial supports, arbitrary vertices, invalid or consuming radii, and healing remain
refused. Chamfer set-backs remain on their dedicated routes.

## Verification and proof

`HalfTurnUnequalRadiusBiconeApexFilletVerification` performs failure-oriented checks for exact
half-turn and unequal-radius extraction, tangent contacts, torus metadata, topology, radial-cap
closure, normals, sector volume, source immutability, and complete/partial/reflex and unsupported
refusal boundaries. The distinct proof is
`Proofs/Phase39l_HalfTurnUnequalRadiusBiconeApexFillet.png`.
