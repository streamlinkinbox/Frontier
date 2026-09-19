#!/usr/bin/env bash
# Sandbox build for SolidArc (ParametricSketcher): same flags as Scratchpad/BuildSolidArc.sh
# but compiles and links in parallel so the 2-core sandbox finishes in reasonable time.
# Objects/bins land in /tmp/solidarc-build/ ; SolidArc is mirrored into the tool's build/ folder.
set -euo pipefail
cd "$(dirname "$0")/../Editor/EditorTools/ParametricSketcher"

export JOBS="${JOBS:-2}"
export OUT=/tmp/solidarc-build
export INC="-I. -IPresentation -DSOLIDARC_PROOF_FOLDER=\"$PWD/Proofs\" -DFRONTIER_DEVELOPMENT"
export FLAGS="-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Wno-unused-function"

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
    Kernel/BlendSolver.cpp
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

mkdir -p "$OUT/obj"

echo "── compiling sources (jobs=$JOBS)"
printf '%s\n' "${ALL_SRCS[@]}" | xargs -P "$JOBS" -I{} bash -c '
    S="$1"
    OBJ="$OUT/obj/$(echo "$S" | tr / _).o"
    if [ -f "$OBJ" ] && [ "$OBJ" -nt "$S" ]; then exit 0; fi
    g++ $FLAGS $INC -c "$S" -o "$OBJ"' _ {}

OBJS=()
for S in "${ALL_SRCS[@]}"; do OBJS+=("$OUT/obj/$(echo "$S" | tr / _).o"); done
export OBJLIST="${OBJS[*]}"

echo "── linking SolidArc"
g++ $FLAGS $INC $OBJLIST Console/SolidArcConsole.cpp -lpthread -o "$OUT/SolidArc"
mkdir -p build
cp -f "$OUT/SolidArc" build/SolidArc

echo "── building verification binaries (jobs=$JOBS)"
ls Verification/*.cpp | xargs -P "$JOBS" -I{} bash -c '
    S="$1"
    NAME=$(basename "$S" .cpp)
    g++ $FLAGS $INC -DSUITEVERIFICATION_SOURCE_ROOT="\"$PWD\"" $OBJLIST "$S" -lpthread -o "$OUT/$NAME"' _ {}

echo "── done: $(ls "$OUT" | grep -c Verification) verification binaries in $OUT"
