# SolidArc Set 3 plan — bounded general-angle partial curved chamfer

## Bounded next slice

Extend the half-turn partial curved-root route to one canonical general-angle sector:

- use the existing measured two-member partial circular chain on a planar shoulder/cylindrical boss;
- accept one non-reflex, non-full sweep with two radial endpoint caps;
- reconstruct the exact outer wall, shoulder, conical chamfer band, retained boss, endpoint meridians, and radial cap faces;
- heal the two radial endpoint caps transactionally and require exact one-hull topology;
- verify angular-fraction volume, chain selection, source immutability, and a visible proof.

## Explicit non-goals

This remains a topology-specific sector route. Reflex sectors, arbitrary arc counts, branched/incomplete chains, mixed
cone/cylinder partial roots, freeform edges, curved networks/corner patches, and general intersection/trim/sew healing
remain explicit refusals. No arbitrary partial-loop support is claimed.

## Acceptance artifacts

- general-angle branch in `ChamferPlaneCylinderBossRoot` using bounded radial-cap healing;
- `Verification/SectorCurvedChamferVerification.cpp`;
- CMake and dependency-free gate entries;
- `Proofs/Phase36i_GeneralSectorChamfer.png`;
- roadmap and blend-limit documentation updated only after the gate and proof pass.
