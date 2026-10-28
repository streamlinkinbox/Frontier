#!/usr/bin/env bash
# Reproduce the night-sky exhibit: build the native CPU renderer, run its checks, convert captures to PNG.
# PNG conversion needs Pillow (PyPI). Set PYTHON to a Python with Pillow installed, e.g. a virtualenv.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/_ClaudeScratch/build/night-sky"
CAPTURE="$ROOT/_ClaudeScratch/night-sky"
PYTHON="${PYTHON:-python3}"
MODE=release
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror)
DEFAULT_RESULTS="$ROOT/Exhibits/NightSky/Results.txt"
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    DEFAULT_RESULTS="$ROOT/Exhibits/NightSky/Results/NightSky-sanitized.txt"
fi
RESULTS="${RESULTS:-$DEFAULT_RESULTS}"
mkdir -p "$BUILD" "$CAPTURE" "$ROOT/Exhibits/NightSky/Images" "$(dirname "$RESULTS")"
g++ "${FLAGS[@]}" -I "$ROOT/Frontier" \
    "$ROOT/Exhibits/NightSky/Code/NightSkyValidation.cpp" \
    "$ROOT/Frontier/Engine/Rendering/CelestialModel.cpp" \
    "$ROOT/Frontier/Engine/Rendering/AtmosphereModel.cpp" -o "$BUILD/NightSkyValidation"
"$PYTHON" "$ROOT/Automation/PrepareMoonAlbedo.py" "$ROOT/EngineContent/CelestialMaps"
"$BUILD/NightSkyValidation" "$CAPTURE/" "$ROOT/Exhibits/NightSky/Data/Hipparcos8.csv" \
    "$ROOT/EngineContent/CelestialMaps/MoonAlbedo2048.ppm" | tee "$RESULTS"
if [[ "$MODE" == release ]]; then
    "$PYTHON" "$ROOT/Exhibits/NightSky/Code/NightSkyImages.py" "$CAPTURE" "$ROOT/Exhibits/NightSky/Images"
fi
