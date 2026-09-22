# Frontier

## SolidArc

The standalone C++20 NURBS/B-rep modelling tool lives at
`Editor/AuthoringTools/Modelling/SolidArc`. The current direct-modelling increment is Phase 36r:
bounded connected same-body face lofts, native and mixed-support curved chamfers, partial and complete cone–cone
root fillets, convex closed non-planar edge-loop mitres, and the explicit linear variable-radius ruled-solid foundation.
Unsupported freeform, arbitrary partial-edge, nonlinear, G2, and general intersection/trim cases refuse transactionally.
The Phase 36r proof is `Proofs/Phase36r_PartialConeConeFillet.png`; the bounded slice is planned in
`Plans/SolidArc_PartialConeConeFillet_Plan.md`.


Run `Tools/Build/CheckSolidArc.sh` for the focused dependency-free verification gate.
