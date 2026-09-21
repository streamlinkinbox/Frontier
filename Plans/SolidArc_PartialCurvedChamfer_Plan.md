# SolidArc Set 3 plan — bounded partial curved-root chamfer

## Bounded next slice

Extend the plane–cylinder curved-root chamfer from complete circles to one physical half-turn:

- accept a measured two-member semicircular root chain shared by a planar shoulder and a cylindrical boss;
- rebuild the outer wall, annular shoulder, conical chamfer band, retained boss and the two endpoint meridian caps;
- retain exact endpoint topology and close one genus-zero manifold half-turn;
- select either chain member transactionally and apply the complete half-turn operation once;
- verify source immutability, analytic support identity, positive/negative material change as appropriate, and visible proof.

## Explicit non-goals

General-angle partial sectors, arbitrary partial arcs, incomplete or branched chains, mixed cone/cylinder partial roots,
freeform edges, multiple curved roots, corner patches, concave/non-convex networks, and general intersection/trim/sew
healing remain explicit refusals. The half-turn is a topology-specific slice, not a claim of arbitrary partial-loop support.

## Acceptance artifacts

- bounded half-turn branch in `ChamferPlaneCylinderBossRoot`;
- `Verification/PartialCurvedChamferVerification.cpp`;
- CMake and dependency-free gate entries;
- `Proofs/Phase36h_PartialCurvedChamfer.png`;
- roadmap and blend-limit documentation updated only after the verification gate and proof pass.
