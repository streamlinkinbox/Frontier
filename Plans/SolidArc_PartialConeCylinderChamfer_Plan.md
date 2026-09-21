# SolidArc Set 3 plan — bounded partial cone–cylinder root chamfer

## Bounded next slice

Add the next mixed curved-edge chamfer route as a deliberately narrow canonical coaxial cone–cylinder sector:

- accept one open non-reflex circular root chain shared by a native coaxial conical frustum and a native coaxial cylinder;
- support the canonical half-turn and one general non-reflex sector with explicit radial endpoint caps;
- measure the cone endpoints/radii, cylinder extent/radius, common root frame, and cap positions instead of trusting face order;
- derive equal support setbacks analytically in the meridian, retaining the cone below the root and cylinder above the root;
- reconstruct exact cone, chamfer-band revolution, cylinder, axial caps, and radial endpoint-cap healing;
- require exact genus-zero manifold topology, angular-fraction volume, transactional source immutability, console dispatch, and visible proof.

## Explicit non-goals

Cone–cone pairs, non-coaxial or oblique supports, apex/zero-radius cones, arbitrary trimmed or reflex arcs, branched or
incomplete chains, mixed partial networks, curved corner patches, freeform supports, and general intersection/trim/sew
healing remain explicit refusals. This slice does not broaden the existing canonical complete cylinder–cone route.

## Acceptance artifacts

- bounded partial cone–cylinder classifier/reconstruction in `BlendSolver.cpp`;
- `Verification/PartialConeCylinderChamferVerification.cpp`;
- CMake and dependency-free gate entries;
- `Proofs/Phase36k_PartialConeCylinderChamfer.png`;
- README, capability roadmap, and blend-limit documentation updated only after the implementation gate passes.
