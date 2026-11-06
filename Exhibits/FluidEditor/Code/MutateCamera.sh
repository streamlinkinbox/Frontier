#!/bin/bash
#============================================================================================================================================
#                                                           MUTATECAMERA.SH
#============================================================================================================================================
# 🧩 Mutation check of PyroVolumeValidation's comparison of the native orbit camera with the prototype's own camera (Reference/CameraTrace.toml, written by
#    DumpCameraTrace.mjs from Experimental/Fluid/src/camera.js). Each mutation edits ONE number of PyroCamera.cpp, rebuilds the validation and runs it; a mutant it
#    does not stop (no assertion fails) SURVIVED. The source is saved first and put back after every mutant and on exit (a signal included), and the last step
#    runs the control again, so the working tree ends as it began.
#
#        Exhibits/FluidEditor/Code/MutateCamera.sh <build folder>
#
#    The build folder is a configured one with ninja; the run needs the repository root as its working directory (it reads Experimental/Fluid and the
#    reference). The camera claims need no device. The first three mutants are the numbers the other agent's camera had different or this port might.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=$(cd "${1:?usage: MutateCamera.sh <build folder>}" && pwd)
export PATH="$HOME/.local/bin:$PATH"
cd "$ROOT" || exit 2
SOURCE=Frontier/Engine/VolumetricDynamics/PyroCamera.cpp
SAVED=$(mktemp -d)
cp "$SOURCE" "$SAVED/PyroCamera.cpp"
restore() { cp "$SAVED/PyroCamera.cpp" "$ROOT/$SOURCE"; }
trap restore EXIT INT TERM

validate() { ninja -C "$BUILD" PyroVolumeValidation > "$SAVED/build.log" 2>&1 || return 1; "$BUILD/PyroVolumeValidation" > "$SAVED/out.log" 2>&1; return 0; }

SURVIVORS=0
CAUGHT=0
run() { # name sed-expression ("" for the control)
  local Name=$1 Expr=$2
  restore
  if [ -n "$Expr" ]; then
    sed -i "$Expr" "$SOURCE"
    if cmp -s "$SOURCE" "$SAVED/PyroCamera.cpp"; then echo "MUTATION $Name: NO CHANGE (pattern missing)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
  fi
  if ! validate; then echo "MUTATION $Name: DID NOT COMPILE (see $SAVED/build.log)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
  local Failed
  Failed=$(grep -c '^FAIL' "$SAVED/out.log")
  if [ -z "$Expr" ]; then echo "CONTROL (the camera as committed): $(tail -1 "$SAVED/out.log")"; return; fi
  if [ "$Failed" -gt 0 ]; then echo "MUTATION $Name: caught ($Failed assertions failed: $(grep '^FAIL' "$SAVED/out.log" | cut -c1-96 | head -3 | tr '\n' '|'))"; CAUGHT=$((CAUGHT + 1))
  else echo "MUTATION $Name: SURVIVED"; SURVIVORS=$((SURVIVORS + 1)); fi
}

run Control ""
# 📝 The other agent's orbit speed (0.0065 rad a pixel) in both axes.
run OrbitSpeed "s/DeltaX \* 0.0075f/DeltaX * 0.0065f/; s/DeltaY \* 0.0075f/DeltaY * 0.0065f/"
# 📝 The zoom's far limit (9.5 m) widened.
run ZoomLimit "/std::max(0.85f, std::min(9.5f,/s/9.5f/12.0f/"
# 📝 How deep into the box the pointer's ray lands (0.45 of the way through it).
run RayDepth "s/(TMax - TEnter) \* 0.45f/(TMax - TEnter) * 0.5f/"
# 📝 The polar limit at the top.
run PolarLimit "s/std::max(0.12f, /std::max(0.1f, /"
# 📝 The pan's speed.
run PanSpeed "s/TargetDistance \* 0.0014f/TargetDistance * 0.0012f/"
# 📝 The zoom's speed.
run ZoomSpeed "s/DeltaPixels \* 0.001f/DeltaPixels * 0.002f/"
# 📝 The turntable's speed.
run TurntableSpeed "s/DtSeconds, 0.05f) \* 0.35f/DtSeconds, 0.05f) * 0.3f/"
# 📝 The easing toward the aim.
run Easing "s/DtSeconds \* 16.0f/DtSeconds * 12.0f/"
# 📝 How far the focus stands back (2.3 widths of the domain).
run FocusDistance "s/std::max(Width, Height) \* 2.3f/std::max(Width, Height) * 2.0f/"
# 📝 The opening distance of a reset.
run ResetDistance "47s/3.35f/3.5f/"
restore
run Control ""
echo "$CAUGHT mutants stopped, $SURVIVORS survived"
[ "$SURVIVORS" -eq 0 ]
