#!/usr/bin/env bash
# Dependency-free SolidArc gate for environments without CMake.
# It compiles the modelling kernel and runs the direct-modelling proofs, including Phase 36a face editing.
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

for TEST in FaceLoft Tweak DirectModeling ChamferLoop TransformTweak CurvedTweak ConcaveChamfer ConeChamfer ConnectedFaceLoft GeneralConnectedFaceLoft GeneralCurvedChamfer PlaneConeChamfer CylinderConeChamfer ArbitraryNonPlanarEdgeLoop Phase33VariableRadius FaceEdit; do
    TEST_OBJ="$WORK/obj/${TEST}Verification.o"
    "$CXX_BIN" "${FLAGS[@]}" -c "$SRC/Verification/${TEST}Verification.cpp" -o "$TEST_OBJ"
    "$CXX_BIN" "${OBJECTS[@]}" "$TEST_OBJ" -o "$WORK/${TEST}Verification"
    "$WORK/${TEST}Verification"
done

echo "[SolidArc] Phase 33/34a–34f, Phase 35a–35d, Phase 36a–36g face-loft, blend and face-edit gates passed"
