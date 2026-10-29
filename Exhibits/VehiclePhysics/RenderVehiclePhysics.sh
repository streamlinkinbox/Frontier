#!/usr/bin/env bash
# Renders the ControlVehicle scene stills + GIFs with the software rasterizer.
# Reuses the production physics (Frontier/Engine/Physics) + pinned Jolt 5.2.0.
# RunVehiclePhysics.sh must have produced _ClaudeScratch/jolt/libJolt.a (this script builds it if absent).
# Outputs land in Exhibits/VehiclePhysics/Visuals/.
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD=_ClaudeScratch/vehiclephysics
FRAMES=_ClaudeScratch/frames
JOLT=_ClaudeScratch/jolt
ARCHIVE=Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Archive
mkdir -p "$BUILD" "$FRAMES/hero" "$FRAMES/tyre" "$FRAMES/drive" "$FRAMES/burnout" "$FRAMES/jolt" Exhibits/VehiclePhysics/Visuals

if [ ! -f "$JOLT/libJolt.a" ]; then
  echo "== fetching + building Jolt 5.2.0 =="
  mkdir -p "$JOLT/obj"
  curl -sL "https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/refs/tags/v5.2.0" -o "$JOLT/jolt.tar.gz"
  tar -xzf "$JOLT/jolt.tar.gz" -C "$JOLT"
  find "$JOLT/JoltPhysics-5.2.0/Jolt" -name "*.cpp" | xargs -P2 -I{} sh -c \
    'g++ -std=c++17 -O2 -DNDEBUG -I '"$JOLT"'/JoltPhysics-5.2.0 -c {} -o '"$JOLT"'/obj/$(echo {} | md5sum | cut -c1-12).o 2>/dev/null'
  ar rcs "$JOLT/libJolt.a" "$JOLT"/obj/*.o
fi

echo "== compiling production physics + renderer =="
for f in VehicleSolver TyreSlipSolver TyreThermalSolver TyreCarcassLattice TyreContinuum; do
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I Frontier -c "Frontier/Engine/Physics/$f.cpp" -o "$BUILD/$f.o"
done
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I Frontier -I "$JOLT/JoltPhysics-5.2.0" -DNDEBUG \
  Exhibits/VehiclePhysics/Code/SceneRender.cpp \
  "$BUILD/VehicleSolver.o" "$BUILD/TyreSlipSolver.o" "$BUILD/TyreThermalSolver.o" "$BUILD/TyreCarcassLattice.o" "$BUILD/TyreContinuum.o" \
  "$JOLT/libJolt.a" -pthread -o "$BUILD/SceneRender"

echo "== rendering =="
"$BUILD/SceneRender" hero        "$ARCHIVE" "$FRAMES/hero"
"$BUILD/SceneRender" tyre-stills "$ARCHIVE" "$FRAMES/tyre"
"$BUILD/SceneRender" gif-drive   "$ARCHIVE" "$FRAMES/drive"
"$BUILD/SceneRender" gif-burnout "$ARCHIVE" "$FRAMES/burnout"
"$BUILD/SceneRender" gif-jolt    "$ARCHIVE" "$FRAMES/jolt"

echo "== assembling PNGs + GIFs =="
PY=python3
[ -x _ClaudeScratch/venv/bin/python ] && PY=_ClaudeScratch/venv/bin/python
"$PY" Exhibits/VehiclePhysics/Code/AssembleVisuals.py "$FRAMES" Exhibits/VehiclePhysics/Visuals
echo "== done =="
