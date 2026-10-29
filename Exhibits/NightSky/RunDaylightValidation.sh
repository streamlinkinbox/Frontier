#!/usr/bin/env bash
# 🧩 Reproduce the daylight, celestial-light and static lunar-bounce checks.
# Requires CMake, a C++17 compiler and Pillow. Set CMAKE/PYTHON to installed tools if needed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CXX="${CXX:-g++}"
CMAKE="${CMAKE:-cmake}"
PYTHON="${PYTHON:-python3}"
MODE=release
CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
                 '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
fi
BUILD="$ROOT/_ClaudeScratch/build/daylight-$MODE"
CAPTURES="$ROOT/_ClaudeScratch/night-sky/daylight-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/NightSky/Results/Daylight-$MODE.txt}"
mkdir -p "$BUILD" "$CAPTURES" "$(dirname "$RESULTS")"
"$PYTHON" "$ROOT/Automation/PrepareMoonAlbedo.py" "$ROOT/EngineContent/CelestialMaps" > "$CAPTURES/preparation.txt"
"$CMAKE" -S Exhibits/VehicleReference -B "$BUILD" -DCMAKE_CXX_COMPILER="$CXX" "${CMAKE_FLAGS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --target DaylightCelestialValidation -j2 >/dev/null
{
    echo "Reference-renderer daylight, celestial lighting and static lunar bounce: $MODE"
    "$CXX" --version | head -1
    uname -srm
    echo "Base revision $(git rev-parse --short HEAD), plus working changes if any; see committed source for exact test."
    sha256sum EngineContent/CelestialMaps/MoonAlbedoSource2048.jpg Exhibits/NightSky/Data/Hipparcos8.csv
    if [[ "${CAPTURE:-0}" == 1 ]]; then
        "$BUILD/DaylightCelestialValidation" "$ROOT/EngineContent/CelestialMaps/MoonAlbedo2048.ppm" "$CAPTURES"
    else
        "$BUILD/DaylightCelestialValidation" "$ROOT/EngineContent/CelestialMaps/MoonAlbedo2048.ppm"
    fi
    echo 'Analytic/numerical and CPU-renderer checks; no calibrated sky observations or GPU/ReSTIR pipeline claim.'
} | tee "$RESULTS"
if [[ "${CAPTURE:-0}" == 1 ]]; then
    "$PYTHON" - "$CAPTURES" "$ROOT/Exhibits/NightSky/Images" <<'PY'
from pathlib import Path
from PIL import Image
import sys
src, dst = map(Path, sys.argv[1:])
dst.mkdir(parents=True, exist_ok=True)
for name, target in (("day-moon", "DayMoonReference.png"),
                     ("day-sky", "DaySkyReference.png"),
                     ("day-stars-boosted", "DayStarsBoostedReference.png"),
                     ("night-moon", "NightMoonReference.png"),
                     ("night-stars", "NightStarsReference.png")):
    Image.open(src / f"{name}.ppm").save(dst / target)
PY
fi
