#!/usr/bin/env bash
#============================================================================================================================
# 🧩 RunVehiclePhysics.sh — full Phase-7 physics proof pipeline.
# 📝 Builds Jolt Physics 5.2.0 once into _ClaudeScratch/jolt (pinned tarball, fetched only if absent, never committed),
#    generates the oracle tables from the demonstration's own JS under node, compiles the production physics + exhibit
#    with warnings-as-errors, runs all checks into Results.txt, and renders the PNG evidence.
#    PYTHON env may point at a Pillow-equipped interpreter (default python3). Exit code = the exhibit's.
#============================================================================================================================
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PYTHON="${PYTHON:-python3}"
case "$PYTHON" in /*) ;; *) if [ -x "$ROOT/$PYTHON" ]; then PYTHON="$ROOT/$PYTHON"; fi ;; esac
JOLT_DIR="_ClaudeScratch/jolt"
JOLT_SRC="$JOLT_DIR/JoltPhysics-5.2.0"

if [ ! -f "$JOLT_DIR/libJolt.a" ]; then
    echo "building Jolt Physics 5.2.0 (one-time)..."
    mkdir -p "$JOLT_DIR/obj"
    if [ ! -d "$JOLT_SRC" ]; then
        curl -sL "https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/refs/tags/v5.2.0" -o "$JOLT_DIR/jolt.tar.gz"
        tar -xzf "$JOLT_DIR/jolt.tar.gz" -C "$JOLT_DIR"
    fi
    find "$JOLT_SRC/Jolt" -name "*.cpp" | xargs -P"$(nproc)" -I{} sh -c \
        'g++ -std=c++17 -O2 -DNDEBUG -I "'"$JOLT_SRC"'" -c {} -o "'"$JOLT_DIR"'/obj/$(echo {} | md5sum | cut -c1-12).o" 2>/dev/null'
    ar rcs "$JOLT_DIR/libJolt.a" "$JOLT_DIR"/obj/*.o
fi

BUILD="_ClaudeScratch/vehiclephysics"
mkdir -p "$BUILD"
echo "generating oracle tables from the demonstration's JS (node)..."
node Exhibits/VehiclePhysics/Code/TyreOracle.mjs > "$BUILD/oracle.txt"

echo "compiling production physics + exhibit..."
for f in VehicleSolver TyreSlipSolver TyreThermalSolver TyreCarcassLattice TyreContinuum; do
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -I Frontier -c "Frontier/Engine/Physics/$f.cpp" -o "$BUILD/$f.o"
done
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I Frontier -I Exhibits/VehiclePhysics/Code -I "$JOLT_SRC" -DNDEBUG \
    Exhibits/VehiclePhysics/Code/VehiclePhysicsExhibit.cpp Exhibits/VehiclePhysics/Code/JoltScene.cpp \
    "$BUILD"/VehicleSolver.o "$BUILD"/TyreSlipSolver.o "$BUILD"/TyreThermalSolver.o "$BUILD"/TyreCarcassLattice.o "$BUILD"/TyreContinuum.o \
    "$JOLT_DIR/libJolt.a" -pthread -o "$BUILD/VehiclePhysicsExhibit"

echo "running checks..."
cd Exhibits/VehiclePhysics
set +e
"$ROOT/$BUILD/VehiclePhysicsExhibit" "$ROOT/$BUILD/oracle.txt" | tee Results.txt
STATUS=${PIPESTATUS[0]}
set -e
"$PYTHON" Code/VehiclePhysicsImages.py
exit $STATUS
