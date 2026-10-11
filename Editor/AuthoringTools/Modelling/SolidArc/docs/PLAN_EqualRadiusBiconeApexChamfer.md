# Plan — bounded equal-radius coaxial bicone apex chamfer

## Capability slice

Add the missing full-turn equal-radius companion to the existing unequal-radius bicone apex chamfer.
The source is one native point-contact pair of coaxial right-circular cones with a shared apex and
equal circular base radii. A common axial set-back replaces the apex with one conical chamfer.

The distinct fixture uses radius 3.25, lower/upper heights 5.8/4.9, and a 0.7 axial set-back.

## Source and reconstruction contract

The classifier accepts only the native point-contact `V5/E6/C12/L4/F4` topology: two conical
revolution faces and two planar base caps, one shared apex, coaxial circular rims, and equal positive
rim radii. It derives the apex, canonical axis, equal radius, support heights, and set-back without
mutating the source.

`ReconstructEqualRadiusBiconeApexChamfer` retains both support frusta, inserts the equal-radius
conical bridge between the two axial contact rings, and returns one closed genus-zero
`V6/E9/C18/L5/F5` body with three cone faces and two planar caps. Its volume is checked against the
exact three-frustum identity.

## Refusal boundary

This route accepts only complete-turn, equal-radius, coaxial bicone apex chamfers. Partial, half, or
reflex sectors, unequal radii, mixed cone/plane/cylinder supports, oblique/non-coaxial or freeform
supports, variable-radius laws, arbitrary vertex selections, invalid or consuming set-backs, and
healing remain refused. The existing unequal-radius full-turn chamfer and partial equal-radius
chamfer remain separate routes.

## Verification and proof

`EqualRadiusBiconeApexChamferVerification` builds a distinct native source and checks exact
extraction, topology, contact radii, outward normals, analytic three-frustum volume, transactional
source immutability, refusal boundaries, and the proof
`Proofs/Phase39e_EqualRadiusBiconeApexChamfer.png`.
