#!/usr/bin/env bash
# Build SolidArc + all verification binaries using g++ directly. Used when
# the sandbox doesn't have cmake/ninja. Mirrors Editor/EditorTools/ParametricSketcher/CMakeLists.txt
# (13 verification executables + the main SolidArc console + the new Phase 17 one).
set -euo pipefail
cd "$(dirname "$0")/../Editor/EditorTools/ParametricSketcher"

SRC_ROOT="."
INC="-I. -IPresentation -DSOLIDARC_PROOF_FOLDER=\"$PWD/Proofs\" -DFRONTIER_DEVELOPMENT"
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wno-unused-function -Wno-unused-parameter"

# All sources (the project is small enough to just compile them in one pass).
ALL_SRCS=(
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

mkdir -p /tmp/solidarc-build/obj

echo "── compiling all sources"
OBJS=()
for S in "${ALL_SRCS[@]}"; do
    O="/tmp/solidarc-build/obj/$(echo "$S" | tr / _).o"; OBJS+=("$O")
    g++ $FLAGS $INC -c "$SRC_ROOT/$S" -o "$O"
done

echo "── building SolidArc"
g++ $FLAGS $INC "${OBJS[@]}" Console/SolidArcConsole.cpp -lpthread -o /tmp/solidarc-build/SolidArc

# Mirror to Editor/EditorTools/ParametricSketcher/build/SolidArc so SuiteVerification (which resolves that path) sees the new binary.
mkdir -p build
cp -f /tmp/solidarc-build/SolidArc build/SolidArc

# Verification binaries: one per .cpp in Verification/, each linked against the same object set.
VERIF_DIR="Verification"
for S in "$VERIF_DIR"/*.cpp; do
    NAME=$(basename "$S" .cpp)
    echo "── building $NAME"
    g++ $FLAGS $INC -DSUITEVERIFICATION_SOURCE_ROOT="\"$PWD\"" \
        "${OBJS[@]}" "$S" -lpthread -o "/tmp/solidarc-build/$NAME"
done

echo "── all built into /tmp/solidarc-build/"
ls -la /tmp/solidarc-build/SolidArc* | head -5
