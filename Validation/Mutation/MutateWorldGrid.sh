#!/bin/bash
#============================================================================================================================================
#                                                          MUTATEWORLDGRID.SH
#============================================================================================================================================
# 🧩 Mutation check of WorldGridValidation: the driving course's smoke on a grid fixed to the world. Each mutation edits ONE place of the solver or of the
#    course's runtime (what a person could get wrong: the window's box, the rule that moves it, the marcher's reading of it, the solid atlas, the count), rebuilds
#    the validation and runs it; a mutant it does not stop (no assertion fails) SURVIVED. The sources are saved first and put back after every mutant and on exit
#    (a signal included), and the last step runs the control again, so the working tree ends as it began.
#
#        Validation/Mutation/MutateWorldGrid.sh <build folder> [mutant name]
#
#    The build folder is a configured one with ninja; the run needs the repository root as its working directory. The window's rule needs no device and is
#    run first; a mutant it does not stop goes on to the pictures, which need a Vulkan device (SwiftShader, under ExternalPackages/VulkanCompute, when it is
#    there). The shaders are not mutated: they are generated SPIR-V, and the toroidal mapping in them is held by the pictures. With a mutant's name only that
#    mutant is run (and the control at the end).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$(cd "${1:?usage: MutateWorldGrid.sh <build folder> [mutant name]}" && pwd)
ONLY=${2:-}
export PATH="$HOME/.local/bin:$PATH"
cd "$ROOT" || exit 2
SOLVER=Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.cpp
BRIDGE=Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.cpp
SAVED=$(mktemp -d)
cp "$SOLVER" "$SAVED/PyroVolumeSolver.cpp"
cp "$BRIDGE" "$SAVED/FluidPlumeBridge.cpp"
restore() { cp "$SAVED/PyroVolumeSolver.cpp" "$ROOT/$SOLVER"; cp "$SAVED/FluidPlumeBridge.cpp" "$ROOT/$BRIDGE"; }
trap restore EXIT INT TERM

SS="$ROOT/ExternalPackages/VulkanCompute/SwiftShader"
if [ -f "$SS/vk_swiftshader_icd.json" ]; then
  export FRONTIER_VULKAN_LIB="$SS/libvulkan.so.1" FRONTIER_VULKAN_DRIVER="$SS/libvulkan.so.1" VK_ICD_FILENAMES="$SS/vk_swiftshader_icd.json" LD_LIBRARY_PATH="$SS"
  DEVICE=1
else
  DEVICE=0
fi

validate() {
  ninja -C "$BUILD" WorldGridValidation > "$SAVED/build.log" 2>&1 || return 1
  "$BUILD/WorldGridValidation" --only window > "$SAVED/out.log" 2>&1
  if [ "$(grep -c '^FAIL' "$SAVED/out.log")" -gt 0 ]; then return 0; fi
  if [ "$DEVICE" -eq 1 ]; then "$BUILD/WorldGridValidation" --require-vulkan > "$SAVED/out.log" 2>&1; fi
  return 0
}

# 📝 mutate <file> <old> <new>: the literal text, which must be there exactly once.
mutate() {
  python3 - "$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path, encoding="utf-8").read()
if text.count(old) != 1:
    sys.exit(3)
open(path, "w", encoding="utf-8").write(text.replace(old, new))
PY
}

SURVIVORS=0
CAUGHT=0
run() { # name file old new (an empty file for the control)
  local Name=$1 File=${2:-} Old=${3:-} New=${4:-}
  if [ -n "$ONLY" ] && [ -n "$File" ] && [ "$Name" != "$ONLY" ]; then return; fi
  restore
  if [ -n "$File" ]; then
    if ! mutate "$File" "$Old" "$New"; then echo "MUTATION $Name: NO CHANGE (pattern missing or not unique)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
  fi
  if ! validate; then echo "MUTATION $Name: DID NOT COMPILE (see $SAVED/build.log)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
  local Failed
  Failed=$(grep -c '^FAIL' "$SAVED/out.log")
  if [ -z "$File" ]; then echo "CONTROL (the sources as committed): $(tail -1 "$SAVED/out.log")"; return; fi
  if [ "$Failed" -gt 0 ]; then echo "MUTATION $Name: caught ($Failed assertions failed: $(grep '^FAIL' "$SAVED/out.log" | cut -c1-110 | head -2 | tr '\n' '|'))"; CAUGHT=$((CAUGHT + 1))
  else echo "MUTATION $Name: SURVIVED"; SURVIVORS=$((SURVIVORS + 1)); fi
}

[ -z "$ONLY" ] && run Control
# 📝 The box of the window ignores where the window is: the smoke is sourced, and the focus is held, in a box that never moves.
run BoxIgnoresOrigin "$SOLVER" "const float MinX   = static_cast<float>(WorldGridOriginX) * CellXZ;" "const float MinX   = 0.0f * CellXZ;"
# 📝 The box is mirrored on the second axis.
run BoxSignFlip "$SOLVER" "const float MinZ   = static_cast<float>(WorldGridOriginZ) * CellXZ;" "const float MinZ   = -static_cast<float>(WorldGridOriginZ) * CellXZ;"
# 📝 The window never moves once it is anchored.
run NeverRecentre "$SOLVER" "if (static_cast<float>(std::abs(FocusX - CenterX)) > Reach ||" "if (false && static_cast<float>(std::abs(FocusX - CenterX)) > Reach ||"
# 📝 The window waits until the focus is at its very edge before it moves.
run DeadZoneWide "$SOLVER" "const float   Reach   = 0.5f * static_cast<float>(Half);" "const float   Reach   = 1.0f * static_cast<float>(Half);"
# 📝 The window moves as soon as the focus is a few cells off its middle.
run DeadZoneNarrow "$SOLVER" "const float   Reach   = 0.5f * static_cast<float>(Half);" "const float   Reach   = 0.15f * static_cast<float>(Half);"
# 📝 The window moves by one cell, not onto the focus: it follows the focus a cell at a time, as the other agent's did.
run MoveByOneCell "$SOLVER" "        WorldGridOriginX = FocusX - Half;
        WorldGridOriginZ = FocusZ - Half;
        ++WorldGridRelocationCount;" "        WorldGridOriginX += (FocusX > CenterX) ? 1 : -1;
        WorldGridOriginZ += (FocusZ > CenterZ) ? 1 : (FocusZ < CenterZ ? -1 : 0);
        ++WorldGridRelocationCount;"
# 📝 Putting the window on its first place is counted as a relocation (the other agent's counter did).
run FirstAnchorCounted "$SOLVER" "        WorldGridHasFocus     = true;
        return;" "        WorldGridHasFocus     = true;
        ++WorldGridRelocationCount;
        return;"
# 📝 The standalone marcher reads the lattice as plain window cells (it did before the window was passed to it): the smoke is drawn where its storage slot is.
run StandaloneMarcherBlind "$SOLVER" "                          WorldGridEnabled, WorldGridOriginX, WorldGridOriginZ,
                          WorldGridOriginX, WorldGridOriginZ);

    Ubo.DomainBoxMax[3]       = Cam.Aspect;
    Ubo.CamPosAndFov[0]       = Cam.Position[0];
    Ubo.CamPosAndFov[1]       = Cam.Position[1];
    Ubo.CamPosAndFov[2]       = Cam.Position[2];
    Ubo.CamPosAndFov[3]       = std::tan(Cam.FovRadians * 0.5f);
    Ubo.CamForwardAndComp[0]  = Cam.Forward[0];
    Ubo.CamForwardAndComp[1]  = Cam.Forward[1];
    Ubo.CamForwardAndComp[2]  = Cam.Forward[2];
    Ubo.CamForwardAndComp[3]  = 0.0f;" "                          false, 0, 0, 0, 0);

    Ubo.DomainBoxMax[3]       = Cam.Aspect;
    Ubo.CamPosAndFov[0]       = Cam.Position[0];
    Ubo.CamPosAndFov[1]       = Cam.Position[1];
    Ubo.CamPosAndFov[2]       = Cam.Position[2];
    Ubo.CamPosAndFov[3]       = std::tan(Cam.FovRadians * 0.5f);
    Ubo.CamForwardAndComp[0]  = Cam.Forward[0];
    Ubo.CamForwardAndComp[1]  = Cam.Forward[1];
    Ubo.CamForwardAndComp[2]  = Cam.Forward[2];
    Ubo.CamForwardAndComp[3]  = 0.0f;"
# 📝 The solid atlas is rebuilt on every second tick only: a step right after the window has moved holds the course's solids where they are not.
run StaleSolidAtlas "$BRIDGE" "if ((WorldTicks % 2u) == 0u || StepWindow != SolidAtlasWindow)" "if ((WorldTicks % 2u) == 0u)"
# 📝 The tyres' smoke is put into the world three metres from the tyres.
run SourcesOffset "$BRIDGE" "int SrcCount = TyreIntake.ProjectIntoDomain(ZeroOrigin, WorldBox, ExtSources, 7, FireLights);" "int SrcCount = TyreIntake.ProjectIntoDomain({ 3.0f, 0.0f, 0.0f }, WorldBox, ExtSources, 7, FireLights);"
restore
run Control
echo "$CAUGHT mutants stopped, $SURVIVORS survived"
[ "$SURVIVORS" -eq 0 ]
