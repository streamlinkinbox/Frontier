# Plan — bounded partial equal-radius bicone apex chamfer

## Capability slice

Add the equal-radius companion to the bounded partial bicone apex chamfer family. One strict
non-reflex partial sector of two coaxial right-circular cones with one shared apex receives an
axial set-back chamfer on both supports. The finite meridian ends are closed by planar radial caps.

The distinct fixture uses equal radius 3.8, lower/upper heights 6.1/4.6, a 105-degree sweep, and
a 0.85 axial set-back.

## Source and reconstruction contract

The classifier accepts only the capped native partial-bicone topology `V7/E11/C22/L6/F6`:
four one-loop revolution faces, two planar radial end caps, two rational circular rim edges, and
one shared apex incident to four straight generator edges. It requires equal rim radii, derives the
apex, normalized coaxial axis, support heights, positive strict sweep, and set-back without mutating
the source.

`ReconstructPartialEqualRadiusBiconeApexChamfer` keeps the finite sweep, retains the lower and upper
support frusta, inserts one equal-radius conical bridge, and adds both radial endpoint caps. It must
return one closed genus-zero `V10/E15/C30/L7/F7` solid with three cone faces, two axial base planes,
and two radial sector planes. Volume is checked against the exact three-frustum identity scaled by
`sweep / (2 pi)`.

## Refusal boundary

This route accepts only equal-radius, strict non-reflex partial sectors. Complete turns, half/reflex
sectors, unequal radii, mixed cone/plane/cylinder apexes, oblique/non-coaxial or freeform supports,
variable-radius laws, arbitrary vertices, invalid or consuming set-backs, and healing remain refused.
The unequal-radius partial chamfer and full-turn equal-radius fillet remain separate routes.

## Verification and proof

`PartialEqualRadiusBiconeApexChamferVerification` builds the source independently and performs
failure-oriented checks for exact equal-radius extraction, sweep retention, topology, radial cap
closure, normals, sector volume, source immutability, and explicit refusal boundaries. The distinct
proof is `Proofs/Phase39d_PartialEqualBiconeApexChamfer.png`.
