# Plan — bounded half-turn equal-radius bicone apex toroidal fillet

## Capability slice

Add the exact half-turn equal-radius companion to the established full-turn and strict non-reflex
partial equal-radius bicone apex toroidal fillets. The source is a capped half-turn sector of two
coaxial right-circular cones with equal support radii and a shared apex. The selected fillet radius
must be positive and leave both conical supports and the toroidal core feasible.

The distinct fixture uses radius 3.6, lower/upper heights 6.2/4.7, a half-turn sweep, and fillet
radius 0.58.

## Source and reconstruction contract

The classifier accepts only the capped native half-turn topology `V7/E11/C22/L6/F6`: four
one-loop revolution faces, two planar radial caps, two rational circular rim edges, and one shared
apex incident to four straight generator edges. It requires equal circular support radii, derives
the shared apex, canonical axis, support heights, and exactly `pi` sweep, and leaves the source
unchanged.

`ReconstructHalfTurnEqualRadiusBiconeApexFillet` solves the two equal-radius cone tangencies,
revolves two retained support cones, one analytic torus, and two axial base caps over the half-turn,
and closes both radial sector ends. It returns closed genus-zero `V10/E15/C30/L7/F7` topology with
two cones, one torus, two axial base planes, and two radial sector planes. Volume is checked against
the exact cone-plus-torus meridian identity scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only exact half-turn equal-radius coaxial bicone sources with feasible positive
fillet radii. Complete, strict non-half partial, or reflex sectors, unequal radii, mixed or
freeform/non-coaxial supports, arbitrary vertices, invalid or consuming radii, and healing remain
refused. Chamfer set-backs remain on their dedicated routes.

## Verification and proof

`HalfTurnEqualRadiusBiconeApexFilletVerification` performs failure-oriented checks for exact
half-turn and equal-radius extraction, tangent contacts, torus metadata, topology, radial-cap
closure, normals, sector volume, source immutability, and complete/partial/reflex and unsupported
refusal boundaries. The distinct proof is
`Proofs/Phase39k_HalfTurnEqualRadiusBiconeApexFillet.png`.
