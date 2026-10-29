#!/usr/bin/env bash
# 🧩 Compare engine catalogue rotation to Astropy/ERFA (not measured positions or proper motions).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CXX="${CXX:-g++}"
PYTHON="${PYTHON:-python3}"
MODE=release
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
BUILD="$ROOT/_ClaudeScratch/build/catalogue-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/NightSky/Results/Catalogue-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
"$CXX" "${FLAGS[@]}" -I Frontier Exhibits/NightSky/Code/StarCatalogueProbe.cpp \
    Frontier/Engine/Rendering/CelestialModel.cpp -o "$BUILD/StarCatalogueProbe"
{
    echo "Hipparcos direction model comparison: $MODE"
    "$CXX" --version | head -1
    uname -srm
    echo "Base revision $(git rev-parse --short HEAD), plus working changes if any; see committed source for exact test."
    "$PYTHON" Exhibits/NightSky/Code/ValidateStarCatalogue.py "$BUILD/StarCatalogueProbe"
} | tee "$RESULTS"
