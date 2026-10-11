# Plan — bounded unequal-setback unequal-radius bicone apex chamfer

## Capability slice

Extend the completed unequal-radius shared-apex chamfer by one narrow variant: the lower and
upper coaxial cone supports use distinct axial set-backs. This phase accepts no new topology or
mixed support class; its distinction is the independent lower and upper contact distances and a
new analytic fixture.

The fixture uses a radius-five lower support of height seven, a radius-two-and-a-half upper support
of height four, a lower set-back of 0.8, and an upper set-back of 1.3.

## Contract

`ClassifyUnequalConeApexUnequalSetbackChamferVertex` delegates structural recognition to the
completed exact native shared-apex source route, then retains two finite positive set-backs. Both
must be strictly below their corresponding support heights and must be unequal. The two support
radii must remain unequal. The source is not mutated.

`ReconstructUnequalConeApexUnequalSetbackChamfer` preserves the two support frusta and inserts one
full-turn conical bridge between the contact rings. Contact radii are independently derived as
`lowerRadius * lowerSetBack / lowerHeight` and `upperRadius * upperSetBack / upperHeight`.
The output remains one genus-zero `V6/E9/C18/L5/F5` solid with three cones and two planar caps.

## Refusal boundary

Equal set-backs remain handled by the prior 38w route rather than this variant. Equal radii,
consuming/invalid set-backs, single cones, cylinders, partial sectors, mixed cone/plane/cylinder
apexes, oblique/non-coaxial supports, freeform or variable-radius surfaces, and healing remain
refused.

## Verification and proof

A new verifier uses a distinct geometry and checks independent setter extraction, deterministic
classification, source immutability, exact contact radii, normals, topology, three-frustum volume,
and refusal boundaries. Its sharp-versus-unequal-setback conical-cap proof is
`Proofs/Phase38x_UnequalSetbackBiconeApexChamfer.png`.
