# Plan — bounded unequal-radius coaxial bicone apex fillet

## Capability slice

Implement one analytic rolling fillet for the existing exact shared-apex unequal-radius coaxial
bicone source. The lower and upper cone sides remain right-circular and coaxial, but the point apex
is replaced by one toroidal surface of revolution tangent to both cone generators. The distinct
fixture uses radii 4.5/2.75, heights 6.5/4.5, and a fillet radius of 0.75.

This is a toroidal two-cone apex route, not general apex filleting.

## Geometry contract

For lower and upper cone half-angles `alphaLower` and `alphaUpper`, the meridian torus center and
major radius are derived from the requested tube radius `q` by the two tangent-line equations:

- `major = q (sin(alphaLower) + sin(alphaUpper)) / sin(alphaLower + alphaUpper)`;
- `centerOffset = q (cos(alphaUpper) - cos(alphaLower)) / sin(alphaLower + alphaUpper)`.

The lower and upper contact points are derived analytically from those values. The route requires
positive forward contact distances within both finite cone supports, a positive major radius that
does not cross the axis, and a non-consuming torus arc.

The reconstruction preserves the two tangent cone frusta, inserts one torus patch, and retains the
two planar base caps. It must return one closed genus-zero `V6/E9/C18/L5/F5` solid with three
analytic support classifications plus one torus and two planes, outward normals, and volume within
the kernel's analytic tolerance of the two frusta plus the torus-meridian integral.

## Refusal boundary

This route refuses equal-radius/equal-support generalization, partial sectors, mixed cone/plane/
cylinder apexes, oblique or non-coaxial supports, freeform/variable-radius supports, consuming
fillet radii, arbitrary selected vertices, and healing. The existing 38w/38x conical chamfer routes
remain separate.

## Verification and proof

A distinct verifier will use the existing shared-apex topology but a unique toroidal fillet radius
and explicit tangent-contact checks. It will test exact extracted dimensions, tangent residuals,
source immutability, topology, normals, torus metadata, analytic volume, refusal boundaries, and a
new sharp-versus-toroidal-cap proof at `Proofs/Phase38y_UnequalConeApexFillet.png`.
