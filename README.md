# Frontier

## SolidArc

The standalone C++20 NURBS/B-rep modelling tool lives at
`Editor/AuthoringTools/Modelling/SolidArc`. The current direct-modelling increment is Phase 36d:
bounded connected same-body face lofts, native and mixed-support curved chamfers, and a convex closed non-planar
edge-loop chamfer route with shared mitres. Unsupported freeform supports, ambiguous loops, and general
intersection/trim cases refuse transactionally.

Run `Tools/Build/CheckSolidArc.sh` for the focused dependency-free verification gate.
