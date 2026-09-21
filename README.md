# Frontier

## SolidArc

The standalone C++20 NURBS/B-rep modelling tool lives at
`Editor/AuthoringTools/Modelling/SolidArc`. The current direct-modelling increment is Phase 36e:
bounded connected same-body face lofts, native and mixed-support curved chamfers, convex closed non-planar edge-loop
mitres, and the first explicit linear variable-radius ruled-solid foundation. Unsupported freeform, partial-edge, G2,
and general intersection/trim cases refuse transactionally.

Run `Tools/Build/CheckSolidArc.sh` for the focused dependency-free verification gate.
