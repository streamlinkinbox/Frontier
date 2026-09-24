# Plan — Phase 39o bounded reflex unequal-radius bicone apex chamfer

## Capability slice

Add one distinct reflex-sector companion to the completed strict partial, exact half-turn, and
complete/independent-setback unequal-radius bicone apex chamfer routes. The source is a capped
native reflex sector of two coaxial right-circular cones with a shared apex and unequal support
radii. One positive equal axial set-back replaces the sharp apex with a conical chamfer bridge;
the retained sector is closed at its two radial ends.

The distinct fixture uses lower/upper radii 4.7/2.6, lower/upper heights 6.3/5.2, an exact
four-thirds-pi reflex sweep, and set-back 0.8. The route intentionally accepts this one canonical
reflex angle rather than claiming arbitrary reflex-sector support.

## Source and reconstruction contract

The classifier accepts only the capped native reflex-bicone topology `V7/E11/C22/L6/F6`: four
one-loop revolution faces, two planar radial caps, two rational circular arc rim edges, and one
shared apex incident to four straight generator edges. It derives the apex, canonical coaxial axis,
unequal radii/heights, and the oriented reflex sweep from the rim arc midpoint as well as its end
points. It leaves the source unchanged.

`ReconstructReflexUnequalRadiusBiconeApexChamfer` retains both support frusta, inserts one unequal
conical bridge at the shared axial set-back, and closes both radial sector ends without healing. It
must return one closed genus-zero `V10/E15/C30/L7/F7` solid with three cone faces, two axial base
planes, and two radial sector planes. Volume is checked against the three-frustum identity scaled
by `sweep / (2 pi)`.

## Refusal boundary

This phase accepts only the canonical reflex angle `4 pi / 3`, unequal coaxial circular bicone
supports, one positive non-consuming equal set-back, the selected apex, and valid manifold source
geometry. Complete turns, strict non-reflex partial sectors, exact half-turn sectors, equal radii,
mixed/freeform/non-coaxial supports, arbitrary vertices, invalid or consuming set-backs, malformed
sources, and healing remain refused. Independent lower/upper set-backs remain on their dedicated
routes.

## Verification and proof

`ReflexUnequalRadiusBiconeApexChamferVerification` will construct the capped reflex source
independently and check exact oriented sweep extraction, unequal contact radii, radial-cap closure,
topology, outward normals, the sector-scaled three-frustum volume identity, source immutability,
and failure-oriented refusal boundaries. The durable proof will be
`Proofs/Phase39o_ReflexUnequalRadiusBiconeApexChamfer.png`.
