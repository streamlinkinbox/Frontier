# Plan — bounded partial unequal-radius bicone apex chamfer

## Capability slice

Extend the unequal-radius bicone apex family to one strict non-reflex partial sector. Two coaxial
right-circular cone supports share a selected apex, have unequal radii, and retain one exact native
sector sweep below pi. The apex is replaced by one conical bridge while the two meridian ends are
closed by planar radial caps.

The distinct fixture uses lower/upper radii 4.2/2.8, heights 6.4/4.8, a 100-degree sweep, and a
0.9 axial set-back on both supports.

## Source and reconstruction contract

The classifier accepts only the capped native partial-bicone source topology `V7/E11/C22/L6/F6`:
four one-loop revolution faces, two planar radial end caps, two rational circular arc rim edges,
and one shared apex incident to four straight generator edges. It derives the apex, normalized
coaxial axis, unequal radii/heights, positive partial sweep, and set-back without mutating the
source.

`ReconstructPartialUnequalConeApexChamfer` preserves the sector sweep, retains the two analytic
support frusta, inserts one conical bridge, and adds the two radial endpoint caps. It must return
one closed genus-zero `V10/E15/C30/L7/F7` solid with five revolution faces and two planar radial
caps. Sector volume is the full three-frustum identity scaled by `sweep / (2 pi)`.

## Refusal boundary

This phase accepts only strict non-reflex partial sectors. Complete turns, reflex/half-turn sectors,
equal-radius sources, mixed cone/plane/cylinder apexes, oblique/non-coaxial or freeform supports,
variable-radius laws, arbitrary vertices, and healing remain refused. Full-turn routes stay on the
existing 38w/38x combination phases.

## Verification and proof

`PartialUnequalBiconeApexChamferVerification` constructs the capped partial source independently,
checks exact sector extraction, unequal contact radii, radial cap closure, topology, volume, normals,
source immutability, and refusal boundaries. The durable sharp-versus-sector-chamfer proof is
`Proofs/Phase39a_PartialUnequalBiconeApexChamfer.png`.
