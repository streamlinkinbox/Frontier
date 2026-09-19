#!/usr/bin/env bash
# Parallel, incremental variant of Scratchpad/BuildSolidArc.sh — identical flags, two compile jobs at a time.
#    Usage: bash Scratchpad/BuildSolidArcParallel.sh            (objects, SolidArc and every verification binary → $OUT)
#           OUT=/some/dir bash Scratchpad/BuildSolidArcParallel.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export PS="$(cd "$HERE/../Editor/EditorTools/ParametricSketcher" && pwd)"
export OUT=${OUT:-/tmp/solidarc-build}
export JOBS=${JOBS:-2}
cd "$PS"
ALL_SRCS=(Kernel/CurveSpecification.cpp Kernel/SurfaceSpecification.cpp Kernel/TopologySpecification.cpp Kernel/SkinSolver.cpp Kernel/IntersectionSolver.cpp Kernel/FairPatchSolver.cpp Kernel/ProfileSolver.cpp Kernel/ConstraintSolver.cpp Kernel/ConstraintGraph.cpp Kernel/MirrorSolver.cpp Kernel/BlendSolver.cpp Presentation/SoftwareRaster.cpp Presentation/ScenePresentation.cpp Interaction/CameraProjection.cpp Interaction/SnapResolution.cpp Interaction/InputEvent.cpp Interaction/HotkeyChart.cpp Interaction/ToolSession.cpp Interaction/TransformGizmo.cpp Document/SceneDocument.cpp Document/FigureRecipe.cpp Document/UndoSequence.cpp Console/CommandCodec.cpp Console/ConsoleHost.cpp Console/ConsoleInteraction.cpp Console/ConsoleSelection.cpp)
mkdir -p "$OUT/obj"
echo "── compiling all sources"
printf '%s\n' "${ALL_SRCS[@]}" | xargs -P "$JOBS" -I{} bash "$HERE/BuildSolidArcOne.sh" obj {}
OBJS=(); for S in "${ALL_SRCS[@]}"; do OBJS+=("$OUT/obj/$(echo "$S" | tr / _).o"); done
OBJSTR="${OBJS[*]}"; export OBJLIST="$OBJSTR"
echo "── building SolidArc"
if [ ! -f "$OUT/SolidArc" ] || [ -n "$(find "$OUT/obj" -newer "$OUT/SolidArc" | head -1)" ] || [ Console/SolidArcConsole.cpp -nt "$OUT/SolidArc" ]; then
    g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Wno-unused-function -I. -IPresentation "-DSOLIDARC_PROOF_FOLDER=\"$PS/Proofs\"" -DFRONTIER_DEVELOPMENT $OBJSTR Console/SolidArcConsole.cpp -lpthread -o "$OUT/SolidArc"
fi
mkdir -p build && cp -f "$OUT/SolidArc" build/SolidArc
echo "── building verification binaries"
ls Verification/*.cpp | xargs -P "$JOBS" -I{} bash "$HERE/BuildSolidArcOne.sh" verif {}
echo "── all built into $OUT"
