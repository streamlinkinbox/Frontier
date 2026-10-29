#!/usr/bin/env bash
# 🧩 Compare native spring/XPBD/solid behaviour against unchanged current VehicleHMI JavaScript.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
BUILD="$ROOT/_ClaudeScratch/tyre-continuum"
RESULTS="${RESULTS:-$ROOT/Exhibits/VehiclePhysics/Results/DemoTyres.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
REFERENCE=Experimental/VehicleHmiReference
[[ "$(git hash-object "$REFERENCE/Source/TyreCarcassProjection.js")" == 8d2719e5c9959f88c7b8f4fda8c81d1cbd1ab26e ]]
[[ "$(git hash-object "$REFERENCE/Source/TyreContinuumElasticity.js")" == 0d62f516175845d2b842b73ff9ff491419451208 ]]
[[ "$(git hash-object "$REFERENCE/Source/VehicleSolver.js")" == 6e5f9afa0e7d358fdd4e9d904a2c707ee30977d2 ]]
[[ "$(git hash-object "$REFERENCE/Shader/VehicleVisual.wgsl")" == 22359e7e799da472c7cb4e6a7d518458278c5813 ]]
node Exhibits/VehiclePhysics/Code/TyreContinuumReference.mjs > "$BUILD/continuum.txt"
node Exhibits/VehiclePhysics/Code/SpringReference.mjs > "$BUILD/spring.txt"
node Exhibits/VehiclePhysics/Code/TyreWiringReference.mjs > "$BUILD/wiring.txt"
FLAGS=(-std=c++17 -Wall -Wextra -Werror -I Frontier)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    FLAGS+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
else
    FLAGS+=(-O2)
fi
CXX="${CXX:-g++}"
PHYSICS=(Frontier/Engine/Physics/VehicleSolver.cpp Frontier/Engine/Physics/TyreSlipSolver.cpp
         Frontier/Engine/Physics/TyreThermalSolver.cpp Frontier/Engine/Physics/TyreCarcassLattice.cpp
         Frontier/Engine/Physics/TyreContinuum.cpp)
"$CXX" "${FLAGS[@]}" Frontier/Engine/Physics/TyreContinuum.cpp \
    Exhibits/VehiclePhysics/Code/TyreContinuumValidation.cpp -o "$BUILD/continuum-validation"
"$CXX" "${FLAGS[@]}" "${PHYSICS[@]}" Exhibits/VehiclePhysics/Code/SpringValidation.cpp -o "$BUILD/spring-validation"
"$CXX" "${FLAGS[@]}" "${PHYSICS[@]}" Exhibits/VehiclePhysics/Code/TyreWiringValidation.cpp -o "$BUILD/wiring-validation"
{
    echo 'Reference: SultanAladin/Frontier VehicleHMI 6afb6f00b5f25c45f7cda92c7da65b64845768f3'
    echo 'Four source/shader Git blob hashes verified; prescribed pressure/grip for vehicle comparisons.'
    "$CXX" --version | head -1
    node --version
    uname -srm
    "$BUILD/continuum-validation" "$BUILD/continuum.txt"
    "$BUILD/spring-validation" "$BUILD/spring.txt"
    "$BUILD/wiring-validation" "$BUILD/wiring.txt"
} | tee "$RESULTS"
