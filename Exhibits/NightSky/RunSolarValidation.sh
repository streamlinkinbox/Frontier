#!/usr/bin/env bash
# 🧩 Validate finite-pixel solar filtering and the actual CPU reference-renderer integration.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CXX="${CXX:-g++}"
CMAKE="${CMAKE:-cmake}"
PYTHON="${PYTHON:-python3}"
MODE="release"
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror)
CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE="sanitized"
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
                 '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
fi
BUILD="$ROOT/_ClaudeScratch/build/solar-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/NightSky/Results/Solar-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
"$CXX" "${FLAGS[@]}" -I Frontier Exhibits/NightSky/Code/SolarDiscValidation.cpp -o "$BUILD/SolarDiscValidation"
# Compile the changed reference integration and its test with strict warnings too.
"$CXX" "${FLAGS[@]}" -I Frontier -c Frontier/Engine/Rendering/ReferenceRenderer.cpp -o "$BUILD/ReferenceRenderer-strict.o"
"$CXX" "${FLAGS[@]}" -I Frontier -c Exhibits/NightSky/Code/SolarReferenceValidation.cpp -o "$BUILD/SolarReferenceValidation-strict.o"
"$CMAKE" -S Exhibits/VehicleReference -B "$BUILD/reference" -DCMAKE_CXX_COMPILER="$CXX" "${CMAKE_FLAGS[@]}" >/dev/null
"$CMAKE" --build "$BUILD/reference" --target SolarReferenceValidation -j2 >/dev/null
{
    echo "Solar pixel/filter and reference-renderer tests: $MODE"
    "$CXX" --version | head -1
    uname -srm
    echo "Base revision $(git rev-parse --short HEAD), plus working changes if any; see committed source for exact test."
    "$BUILD/SolarDiscValidation"
    if [[ "${CAPTURE:-0}" == 1 ]]; then
        "$BUILD/reference/SolarReferenceValidation" "$BUILD/reference.ppm"
    else
        "$BUILD/reference/SolarReferenceValidation"
    fi
    echo 'Numerical model/invariant tests, not calibrated camera, atmosphere or physical-hardware measurements.'
} | tee "$RESULTS"
if [[ "${CAPTURE:-0}" == 1 ]]; then
    "$PYTHON" - "$BUILD/reference.ppm" "$ROOT/Exhibits/NightSky/Images/SolarReferenceMixed.png" <<'PY'
from PIL import Image
import sys
Image.open(sys.argv[1]).save(sys.argv[2])
PY
fi
