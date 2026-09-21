# Frontier

## SolidArc

The standalone C++20 NURBS/B-rep modelling tool lives at
`Editor/AuthoringTools/Modelling/SolidArc`. The current direct-modelling increment is Phase 36c:
bounded connected same-body face lofts, transactional fixed-topology face edits, native curved-cap chamfers, and the
first general mixed plane/cylinder curved-root chamfer, with explicit refusal for unsupported curved loops and
intersection/trim cases.

Run `Tools/Build/CheckSolidArc.sh` for the focused dependency-free verification gate.
