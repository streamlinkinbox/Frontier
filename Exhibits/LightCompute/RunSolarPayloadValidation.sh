#!/usr/bin/env bash
# 📝 Validate solar payload and sensor-FWHM conversion, with optional C++ sanitizers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MODE=release
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
BUILD="$ROOT/_ClaudeScratch/build/solar-payload-$MODE"
mkdir -p "$BUILD" Exhibits/LightCompute/Results
"${CXX:-g++}" "${FLAGS[@]}" -I Frontier Exhibits/LightCompute/Code/SolarPayloadValidation.cpp -o "$BUILD/test"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/SolarPayload-$MODE.txt}"
{
    sha256sum Frontier/Engine/Rendering/SolarOpticsPayload.h Exhibits/LightCompute/Code/SolarPayloadValidation.cpp
    "$BUILD/test"
} | tee "$RESULTS"
