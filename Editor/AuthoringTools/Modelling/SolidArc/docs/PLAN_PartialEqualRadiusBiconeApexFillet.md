# Plan — bounded partial equal-radius bicone apex toroidal fillet

## Capability slice

Add a distinct equal-radius companion to the partial unequal-radius toroidal route. One strict
non-reflex native sector of two coaxial right-circular cones with the same base radius receives one
constant-radius toroidal apex fillet. The finite meridian ends are closed with radial planar caps.

The fixture uses radius 3.6, lower/upper heights 6.2/4.7, a 100-degree sweep, and a 0.58 fillet
radius.

## Source and reconstruction contract

The classifier accepts only the capped native partial-bicone source topology `V7/E11/C22/L6/F6`,
requires the two rim radii to be equal, derives the shared apex, canonical coaxial axis, support
heights, and native sweep, and preserves the source transactionally.

`ReconstructPartialEqualRadiusBiconeApexFillet` solves the two cone tangent contacts, revolves the
lower cone, torus meridian, upper cone, and two axial base caps over the finite sweep, and adds the
two radial sector caps. It returns closed genus-zero `V10/E15/C30/L7/F7` topology with two cones,
one torus, two base planes, and two radial planes. Volume is checked against the full meridian
cone/torus identity scaled by `sweep / (2 pi)`.

## Refusal boundary

This route accepts only equal-radius, strict non-reflex partial sectors. Full turns, half/reflex
sectors, unequal radii, mixed cone/plane/cylinder apexes, oblique/non-coaxial or freeform supports,
variable-radius laws, arbitrary selections, invalid/consuming radii, and healing remain refused.
The full-turn equal-radius fillet and partial unequal-radius fillet remain separate routes.

## Verification and proof

`PartialEqualRadiusBiconeApexFilletVerification` performs 31 checks over the distinct fixture,
covering exact equal-radius extraction, sweep, tangent contacts, torus metadata, topology, radial
cap closure, normals, sector volume, source immutability, refusal boundaries, and the proof
`Proofs/Phase39c_PartialEqualBiconeApexFillet.png`.
