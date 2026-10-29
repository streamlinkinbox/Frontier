#!/usr/bin/env bash
# 🧩 Reproduce sensitivity and timestep diagnostics for the unchanged VehicleHMI spring model.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MODE="release"
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror -I Frontier)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE="sanitized"
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I Frontier)
fi
CXX="${CXX:-g++}"
BUILD="$ROOT/_ClaudeScratch/vehicle-stability-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/VehiclePhysics/Results/SpringStability-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
while read -r HASH PATHNAME; do
    [[ "$(git hash-object "Experimental/VehicleHmiReference/$PATHNAME")" == "$HASH" ]] || {
        echo "Reference hash mismatch: $PATHNAME" >&2; exit 1; }
done < <(node -e "for (const f of require('./Experimental/VehicleHmiReference/SourceManifest.json').files) console.log(f.git_blob+' '+f.path)")
node Exhibits/VehiclePhysics/Code/SpringOneStepReference.mjs > "$BUILD/one-step.txt"
"$CXX" "${FLAGS[@]}" Frontier/Engine/Physics/VehicleSolver.cpp Frontier/Engine/Physics/TyreSlipSolver.cpp \
    Frontier/Engine/Physics/TyreThermalSolver.cpp Frontier/Engine/Physics/TyreCarcassLattice.cpp \
    Frontier/Engine/Physics/TyreContinuum.cpp Exhibits/VehiclePhysics/Code/SpringOneStepValidation.cpp -o "$BUILD/one-step"
{
    echo 'VehicleHMI spring source: SultanAladin/Frontier commit 6afb6f00b5f25c45f7cda92c7da65b64845768f3'
    echo "All retained reference hashes verified; $MODE; numerical simulations, no measurements."
    echo "Native base $(git rev-parse --short HEAD), plus working changes if any; see committed source for exact test."
    "$CXX" --version | head -1
    node --version
    uname -srm
    echo '== Synchronized JS/native one-step comparison =='
    "$BUILD/one-step" "$BUILD/one-step.txt"
    echo '== Initial-height sensitivity and hull-contact control =='
    node Exhibits/VehiclePhysics/Code/SpringSensitivity.mjs
    echo '== Direct-Step timestep study =='
    node Exhibits/VehiclePhysics/Code/SpringTimeStepStudy.mjs
} | tee "$RESULTS"
