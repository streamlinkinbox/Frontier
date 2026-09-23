# Plan — bounded equal-radius coaxial bicone apex toroidal fillet

## Capability slice

Add one explicit equal-radius combination route after the unequal-radius toroidal fillet. The
source remains two closed coaxial right-circular cone supports meeting at one selected apex, but
both base radii are equal while the support heights may differ. The apex is replaced by one
full-turn toroidal patch tangent to both cone generators.

The distinct fixture uses radius 3.5, lower/upper heights 6.0/4.5, and a toroidal fillet radius
of 0.65.

## Contract

`ClassifyEqualRadiusBiconeApexFilletVertex` accepts only the native point-contact
`V5/E6/C12/L4/F4` two-cone/two-cap revolution topology, its unique shared apex, coaxial circular
rims, and equal positive rim radii. It derives the normalized axis and the two support heights,
retains the fillet radius, and leaves the source untouched.

`ReconstructEqualRadiusBiconeApexFillet` solves the same two tangent-line equations as the unequal
route, but requires equal support radii at the classifier boundary. It returns one genus-zero
`V6/E9/C18/L5/F5` body with two cone faces, one torus, and two planar caps, plus analytic tangent
contact and torus-meridian volume checks.

## Refusal boundary

Unequal-radius sources remain on the 38y route. Partial sectors, mixed cone/plane/cylinder apexes,
oblique/non-coaxial or freeform supports, variable-radius laws, arbitrary vertex selections,
consuming fillet radii, and healing remain unsupported.

## Verification and proof

A distinct verifier uses the radius-3.5, height-6/4.5 source and a 0.65 torus radius. It checks
exact equal-radius extraction, tangent contacts, torus metadata, normals, topology, analytic
volume, source immutability, and refusal boundaries. The durable proof is
`Proofs/Phase38z_EqualRadiusBiconeApexFillet.png`.
