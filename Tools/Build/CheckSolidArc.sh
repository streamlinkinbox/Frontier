#!/usr/bin/env bash
# Dependency-free SolidArc gate for environments without CMake.
# It compiles the modelling kernel and runs the direct-modelling proofs, including bounded non-box face editing through Phase 41.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/Editor/AuthoringTools/Modelling/SolidArc"
CXX_BIN="${CXX:-g++}"

if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    echo "[SolidArc] SKIPPED — no C++ compiler ($CXX_BIN) on PATH"
    exit 0
fi

WORK="$(mktemp -d /tmp/SolidArcGate.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
PROOF_FOLDER="${SOLIDARC_PROOF_FOLDER:-$WORK/proofs}"
mkdir -p "$WORK/obj" "$PROOF_FOLDER"

FLAGS=(-std=c++20 -Wall -Wextra -Wpedantic -Wno-unused-function
       -I"$SRC" -I"$SRC/Presentation"
       "-DSOLIDARC_PROOF_FOLDER=\"$PROOF_FOLDER\"")

CORE=(
    Kernel/CurveSpecification.cpp
    Kernel/SurfaceSpecification.cpp
    Kernel/TopologySpecification.cpp
    Kernel/SkinSolver.cpp
    Kernel/IntersectionSolver.cpp
    Kernel/FairPatchSolver.cpp
    Kernel/ProfileSolver.cpp
    Kernel/ConstraintSolver.cpp
    Kernel/ConstraintGraph.cpp
    Kernel/MirrorSolver.cpp
    Kernel/BlendSolver.cpp
    Kernel/TweakSolver.cpp
    Kernel/FaceEditSolver.cpp
    Presentation/SoftwareRaster.cpp
    Presentation/ScenePresentation.cpp
    Interaction/CameraProjection.cpp
    Interaction/SnapResolution.cpp
    Interaction/InputEvent.cpp
    Interaction/HotkeyChart.cpp
    Interaction/ToolSession.cpp
    Interaction/TransformGizmo.cpp
    Document/SceneDocument.cpp
    Document/FigureRecipe.cpp
    Document/UndoSequence.cpp
    Console/CommandCodec.cpp
    Console/ConsoleHost.cpp
    Console/ConsoleInteraction.cpp
    Console/ConsoleSelection.cpp
)

OBJECTS=()
for REL in "${CORE[@]}"; do
    OBJ="$WORK/obj/${REL//\//_}.o"
    "$CXX_BIN" "${FLAGS[@]}" -c "$SRC/$REL" -o "$OBJ"
    OBJECTS+=("$OBJ")
done

"$CXX_BIN" "${FLAGS[@]}" -c "$SRC/Console/SolidArcConsole.cpp" -o "$WORK/obj/Console.o"
"$CXX_BIN" "${OBJECTS[@]}" "$WORK/obj/Console.o" -o "$WORK/SolidArc"
"$WORK/SolidArc" --help >/dev/null

echo "[SolidArc] kernel, console and interaction targets link"

for TEST in FaceLoft Tweak DirectModeling ChamferLoop TransformTweak CurvedTweak ConcaveChamfer ConeChamfer ConnectedFaceLoft GeneralConnectedFaceLoft GeneralCurvedChamfer PlaneConeChamfer CylinderConeChamfer PartialCurvedChamfer SectorCurvedChamfer PartialPlaneConeChamfer PartialConeCylinderChamfer PartialConeConeChamfer ApexPlaneConeChamfer PartialApexPlaneConeChamfer PartialPlaneConeFillet CylinderConeFillet ConeConeFillet PartialConeConeFillet PartialConeCylinderFillet ArbitraryNonPlanarEdgeLoop Phase33VariableRadius VariableRadiusCornerFillet VariableSetbackCornerFillet NonlinearVariableRadiusCorner G2PlanarCorner NonlinearVariableSetbackCorner UnequalSetbackCorner NonlinearUnequalSetbackCorner PartialEdgeFillet ConeApexFillet QuadraticPartialEdgeFillet PartialConeApexFillet ObliquePlanarCornerFillet ObliquePartialEdgeFillet ObliqueQuadraticPartialEdgeFillet ObliqueQuadraticEdgeFillet G2RollingBall ObliqueG2RollingBall VariableG2RollingBall ObliqueVariableG2RollingBall EligibleG2EdgeDispatch EligibleObliqueG2EdgeDispatch EligibleVariableG2EdgeDispatch EligibleObliqueVariableG2EdgeDispatch ConeApexVertexDispatch PartialConeApexVertexDispatch GeneralPartialConeApexVertexDispatch ReflexPartialConeApexVertexDispatch ConeApexChamfer PartialConeApexVertexChamfer UnequalConeApexChamfer UnequalSetbackBiconeApexChamfer UnequalConeApexFillet EqualRadiusBiconeApexFillet PartialUnequalBiconeApexChamfer PartialUnequalBiconeApexFillet PartialEqualRadiusBiconeApexFillet PartialEqualRadiusBiconeApexChamfer EqualRadiusBiconeApexChamfer PartialUnequalBiconeUnequalSetbackChamfer PartialEqualRadiusBiconeUnequalSetbackChamfer EqualRadiusBiconeUnequalSetbackChamfer HalfTurnEqualRadiusBiconeUnequalSetbackChamfer HalfTurnUnequalRadiusBiconeUnequalSetbackChamfer HalfTurnEqualRadiusBiconeApexFillet HalfTurnUnequalRadiusBiconeApexFillet HalfTurnEqualRadiusBiconeApexChamfer HalfTurnUnequalRadiusBiconeApexChamfer ReflexUnequalRadiusBiconeApexChamfer ExtrudedConvexPrismShell PentagonalPrismFaceOffset HoledPrismFaceOffset EllipticalPrismFaceOffset ObliqueTriangularPrismFaceOffset TriangularPrismDraft TwinHoledPrismFaceOffset ConcavePrismFaceOffset CylinderChamfer CylinderFillet PlaneCylinderFillet TangentChainFillet OpenChainFillet MultiEdgeFillet SectorEndpointFillet CornerFillet PlaneConeFillet FaceEdit; do
    TEST_OBJ="$WORK/obj/${TEST}Verification.o"
    "$CXX_BIN" "${FLAGS[@]}" -c "$SRC/Verification/${TEST}Verification.cpp" -o "$TEST_OBJ"
    "$CXX_BIN" "${OBJECTS[@]}" "$TEST_OBJ" -o "$WORK/${TEST}Verification"
    "$WORK/${TEST}Verification"
done

# Keep the focused gate's normal scratch behaviour, but make the nine newly covered baseline
# artifacts and the current bounded-slice proof durable. Their verifier names are unchanged;
# this is an explicit proof-coverage export rather than a blanket export of every temporary render.
PERSISTED_PROOFS=(
    Phase25_CylinderChamfers.png
    Phase26_CylinderFillets.png
    Phase31_PlaneCylinderFillet.png
    Phase32a_TangentChainFillet.png
    Phase32b_OpenChainFillet.png
    Phase32c_MultiEdgeFillet.png
    Phase32d_SectorEndpointFillet.png
    Phase32e_CornerFillet.png
    Phase32z_PlaneConeFillet.png
    Phase38a_PartialEdgeFillet.png
    Phase38b_ConeApexFillet.png
    Phase38c_QuadraticPartialEdgeFillet.png
    Phase38d_PartialConeApexFillet.png
    Phase37g_NonlinearUnequalSetbackCorner.png
    Phase38e_ObliquePlanarCornerFillet.png
    Phase38f_ObliquePartialEdgeFillet.png
    Phase38g_ObliqueQuadraticPartialEdgeFillet.png
    Phase38h_ObliqueQuadraticEdgeFillet.png
    Phase38i_G2RollingBall.png
    Phase38j_ObliqueG2RollingBall.png
    Phase38k_VariableG2RollingBall.png
    Phase38l_ObliqueVariableG2.png
    Phase38m_EligibleG2EdgeDispatch.png
    Phase38n_EligibleObliqueG2EdgeDispatch.png
    Phase38o_EligibleVariableG2EdgeDispatch.png
    Phase38p_EligibleObliqueVariableG2EdgeDispatch.png
    Phase38q_ConeApexVertexDispatch.png
    Phase38r_PartialConeApexVertexDispatch.png
    Phase38s_GeneralPartialConeApexVertexDispatch.png
    Phase38t_ReflexPartialConeApexVertexDispatch.png
    Phase38u_ConeApexVertexChamfer.png
    Phase38v_PartialConeApexVertexChamfer.png
    Phase38w_UnequalConeApexChamfer.png
    Phase38x_UnequalSetbackBiconeApexChamfer.png
    Phase38y_UnequalConeApexFillet.png
    Phase38z_EqualRadiusBiconeApexFillet.png
    Phase39a_PartialUnequalBiconeApexChamfer.png
    Phase39b_PartialUnequalBiconeApexFillet.png
    Phase39c_PartialEqualBiconeApexFillet.png
    Phase39d_PartialEqualBiconeApexChamfer.png
    Phase39e_EqualRadiusBiconeApexChamfer.png
    Phase39f_PartialUnequalBiconeUnequalSetbackChamfer.png
    Phase39g_PartialEqualBiconeUnequalSetbackChamfer.png
    Phase39h_EqualRadiusBiconeUnequalSetbackChamfer.png
    Phase39i_HalfTurnEqualRadiusBiconeUnequalSetbackChamfer.png
    Phase39j_HalfTurnUnequalRadiusBiconeUnequalSetbackChamfer.png
    Phase39k_HalfTurnEqualRadiusBiconeApexFillet.png
    Phase39l_HalfTurnUnequalRadiusBiconeApexFillet.png
    Phase39m_HalfTurnEqualRadiusBiconeApexChamfer.png
    Phase39n_HalfTurnUnequalRadiusBiconeApexChamfer.png
    Phase39o_ReflexUnequalRadiusBiconeApexChamfer.png
    Phase40_ExtrudedConvexPrismShell.png
    Phase41_PentagonalPrismFaceOffset.png
    Phase42_HoledPrismFaceOffset.png
    Phase43_EllipticalPrismFaceOffset.png
    Phase44_ObliqueTriangularPrismFaceOffset.png
    Phase45_TriangularPrismDraft.png
    Phase46_TwinHoledPrismFaceOffset.png
    Phase47_ConcavePrismFaceOffset.png
)
mkdir -p "$ROOT/Proofs"
for PROOF in "${PERSISTED_PROOFS[@]}"; do
    if [[ ! -s "$PROOF_FOLDER/$PROOF" ]]; then
        echo "[SolidArc] missing persisted baseline proof: $PROOF" >&2
        exit 1
    fi
    cp "$PROOF_FOLDER/$PROOF" "$ROOT/Proofs/$PROOF"
done

echo "[SolidArc] Phase 33/34a–34f, Phase 35a–35d, Phase 36a–36s, Stages 1–3c/3e, unequal setbacks, bounded partial-edge/apex blends, bounded quadratic partial-edge, bounded partial cone-apex/oblique corner, bounded oblique quadratic partial/full-edge, bounded orthogonal/oblique/variable/oblique-variable rolling-ball-core G2, bounded eligible orthogonal/oblique/variable/oblique-variable G2 edge dispatch, bounded native-cone, half-turn, non-reflex and reflex partial-cone apex vertex dispatch, bounded native-cone and native partial-cone apex vertex chamfer, bounded unequal-radius coaxial bicone apex chamfer, bounded unequal-setback unequal-radius bicone apex chamfer, bounded unequal-radius coaxial bicone toroidal apex fillet, bounded equal-radius coaxial bicone toroidal apex fillet, bounded partial unequal-radius bicone apex chamfer, bounded partial unequal-radius bicone toroidal apex fillet, bounded partial equal-radius bicone toroidal apex fillet, bounded partial equal-radius bicone apex chamfer, bounded full-turn equal-radius bicone apex chamfer, bounded partial unequal-radius bicone chamfer with independent set-backs, bounded partial equal-radius bicone chamfer with independent set-backs, bounded full-turn equal-radius bicone chamfer with independent set-backs, bounded half-turn equal-radius bicone chamfer with independent set-backs, bounded half-turn unequal-radius bicone chamfer with independent set-backs, bounded half-turn equal-radius bicone apex toroidal fillet, bounded half-turn unequal-radius bicone apex toroidal fillet, bounded half-turn equal-radius bicone apex chamfer, bounded half-turn unequal-radius bicone apex chamfer, bounded reflex unequal-radius bicone apex chamfer, bounded non-box extruded convex-prism shell, bounded non-box pentagonal-prism face offset, bounded genus-one holed-prism face offset, bounded elliptical-prism face offset, bounded oblique triangular-prism face offset, bounded triangular-prism draft, bounded genus-two twin-holed-prism face offset, bounded orthogonal concave-prism face offset, and durable Phase 25/26/31/32/32z/38w/38x/38y/38z/39a/39b/39c/39d/39e/39f/39g/39h/39i/39j/39k/39l/39m/39n/39o/40/41/42/43/44/45/46/47 proof gates passed"
