# SolidArc Set 3 plan — bounded apex plane–cone chamfer

## Bounded next slice

Extend the native plane–cone chamfer family to one canonical coaxial conical frustum whose upper endpoint is a true apex:

- accept the complete circular plane–cone boss-root topology with a native cone terminating at radius zero;
- keep the support complete and coaxial for this slice; do not infer arbitrary apex trims or non-coaxial intersections;
- measure the root circle, cone axis, finite slant, shoulder/outer wall, and apex endpoint explicitly;
- reconstruct the exact shoulder setback band and retained apex cone, preserving the apex as a single vertex;
- require one genus-zero manifold with exact apex topology, analytic meridian volume, source immutability, console dispatch,
  refusal boundaries, and a visible proof.

## Explicit non-goals

Partial apex sectors, non-coaxial or oblique cone/cylinder or cone/cone supports, multiple or blended apex networks,
arbitrary trimmed/reflex/branched/incomplete curved roots, curved corner patches, freeform supports, and general
intersection/trim/sew healing remain explicit refusals. This slice does not alter the bounded partial cone–cone or
cone–cylinder sector routes.

## Acceptance artifacts

- bounded complete coaxial apex plane–cone classifier/reconstruction in `BlendSolver.cpp`;
- `Verification/ApexPlaneConeChamferVerification.cpp`;
- CMake and dependency-free gate entries;
- `Proofs/Phase36m_ApexPlaneConeChamfer.png`;
- README, capability roadmap, and blend-limit documentation updated only after the implementation gate passes.
