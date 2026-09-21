# SolidArc Set 3 plan — bounded mixed-support curved chamfers

## First slice: complete plane–cone boss-root chamfer

Implement one deliberately bounded mixed-support case before attempting arbitrary/freeform geometry:

- a complete circular root where a planar annular shoulder meets a native coaxial conical frustum boss;
- exact analytic reconstruction of the retained outer cylinder, shoulder of revolution, conical chamfer band, and retained cone;
- measured cone end rows and straight generators must agree with the native cone definition;
- the setback is measured along the conical support and converted to its axial contact height;
- zero, negative, consuming, apex, and malformed support inputs refuse transactionally;
- the result must be one positive-volume solid with one genus-zero hull, no open/non-manifold/misoriented edges;
- the source body must remain unchanged;
- a C++ verification and a visible source/result proof image are required.

The concave boss-root convention follows the existing plane–cylinder curved-root route: the exact conical band bridges the radial shoulder setback to the setback point on the cone. Its added meridian wedge is checked analytically against tessellated volume; it is not silently treated as a generic planar edge cut.

## Explicit non-goals for this slice

The route must continue to refuse arbitrary torus/freeform edges, partial curved loops, mixed cone–cylinder support networks, non-coaxial supports, curved corner patches, concave/non-convex networks, and general intersection/trim/sew healing. Those are separate implementation slices and must not be claimed complete by this verification.

## Follow-up order

1. General curved-edge chamfers — this complete plane–cone case.
2. Arbitrary non-planar edge loops.
3. Phase 33 variable-radius and G2/variable-fillet work, with nonlinear, partial-edge, and unsupported laws documented as refusals until exact routes exist.

## Acceptance artifacts

- `Verification/PlaneConeChamferVerification.cpp`
- CMake and `Tools/Build/CheckSolidArc.sh` entries
- `Proofs/Phase36f_PlaneConeChamfer.png`
- capability documentation updated only after the verification gate and proof pass.
