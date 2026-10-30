#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x ExternalPackages/BuildTools/cmake/data/bin/cmake ]]; then
    CMAKE="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"
fi
BUILD="$ROOT/_ClaudeScratch/build/vehicle-reference-cmake"
RAW="$ROOT/_ClaudeScratch/vehicle-reference/showcase"
OUTPUT="$ROOT/Exhibits/VehicleReference/Capture/Showcase"
node Exhibits/VehicleReference/Code/Materials.mjs
"$CMAKE" -S Exhibits/VehicleReference -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$BUILD" --parallel 2 --target VehicleCapture
"$BUILD/VehicleCapture" "$RAW" --showcase
python3 Exhibits/VehicleReference/Code/CaptureShowcaseImages.py "$RAW" "$OUTPUT"
