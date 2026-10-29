#!/usr/bin/env bash
# 🧭 Independently compare the compact Sun/Moon position models with the offline JPL DE421 ephemeris.
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
BUILD="$ROOT/_ClaudeScratch/build/ephemeris-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/NightSky/Results/Ephemeris-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
"$CXX" "${FLAGS[@]}" -I Frontier Exhibits/NightSky/Code/EphemerisProbe.cpp \
    Frontier/Engine/Rendering/CelestialModel.cpp -o "$BUILD/EphemerisProbe"
{
    echo "Sun and Moon independent ephemeris comparison, $MODE; model comparison, not measured positions."
    echo "Native base $(git rev-parse --short HEAD), plus working changes if any; see committed source for exact test."
    "$CXX" --version | head -1
    "$PYTHON" --version
    uname -srm
    "$PYTHON" Exhibits/NightSky/Code/ValidateEphemeris.py "$BUILD/EphemerisProbe"
} | tee "$RESULTS"
