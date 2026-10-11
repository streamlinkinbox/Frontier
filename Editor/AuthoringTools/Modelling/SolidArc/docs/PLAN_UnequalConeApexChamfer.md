# Plan — bounded unequal-radius coaxial bicone apex chamfer

## Capability slice

Implement one explicit unequal-radius apex route: two closed, coaxial right-circular cone
supports share one selected apex, their base radii are distinct, and the apex is replaced by one
full-revolution conical chamfer bridge. The first fixture uses a six-unit lower support of radius
four, a five-unit upper support of radius three, and a one-unit axial set-back on both sides.

This is a shared-apex combination route, not a claim of general mixed-support apex editing.

## Source contract

`ClassifyUnequalConeApexChamferVertex` accepts only the native source made from four full-turn
revolved straight profile segments:

- two cone side faces and two planar base faces, all one-loop native revolution faces;
- the exact point-contact source topology `V5/E6/C12/L4/F4`;
- two circular rational rim edges and four straight generator edges;
- one selected apex incident to both cone generators;
- two coaxial base rims on opposite sides of that apex, with positive and unequal radii;
- the validated point-contact report `Solid`, `Hulls == 2`, and `Genus == 1`.

The classifier derives the apex, a deterministic normalized axis, lower/upper radius, lower/upper
height, and the requested finite positive set-back. It refuses every other selected vertex and
leaves the source body untouched.

## Reconstruction contract

The reconstruction keeps the two analytic support frusta and inserts one analytic conical bridge
between the two contact rings. The contact radii are

`lowerRadius * setBack / lowerHeight` and `upperRadius * setBack / upperHeight`.

The bridge is a full-turn revolution of the straight contact-ring generator. The returned body must
be one closed genus-zero solid with `V6/E9/C18/L5/F5` topology, three cone faces, two planar caps,
zero open/non-manifold/misoriented edges, and volume within the kernel's analytic tolerance of the
sum of the three frustum formulas. Reconstruction is a separate transaction; it does not replace
or mutate the point-contact source.

## Refusal boundary

This slice explicitly refuses equal-radius combinations, complete single cones, cylinders,
partial sectors, cone/frustum inputs, mixed cone/plane/cylinder apexes, freeform or variable-radius
supports, non-coaxial or oblique supports, arbitrary vertex selections, consuming set-backs, and
malformed/healing cases. General apex fillets, planar-cap routes, arbitrary mixed apex dispatch,
intersection/trim/sew healing, and source-body replacement remain separate future capabilities.

## Verification and proof

`UnequalConeApexChamferVerification` constructs its own shared-apex source, checks exact extraction,
determinism, source immutability, transactional refusal, the three-frustum volume, analytic face
radii, outward normals, and the exact output topology. It renders the distinct sharp-versus-conical-
cap comparison to `Proofs/Phase38w_UnequalConeApexChamfer.png`.

The focused verifier is registered in CMake and `Tools/Build/CheckSolidArc.sh`; the full dependency-
free gate remains the executable validation path where CMake is unavailable.
