#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# CMake >= 3.20, C++17, Node and Python required; no Vulkan SDK or GPU inference.
Cmake="${CMAKE:-cmake}"
if ! command -v "$Cmake" >/dev/null && [[ -x ExternalPackages/BuildTools/cmake/data/bin/cmake ]]; then
 Cmake="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"
fi
Build="$PWD/_ClaudeScratch/build/vehicle-reference-cmake"
node Exhibits/VehicleReference/Code/Materials.mjs
"$Cmake" -S Exhibits/VehicleReference -B "$Build" -DCMAKE_BUILD_TYPE=Release
"$Cmake" --build "$Build" --parallel 2 --target VehicleCapture TriangleVisibilityValidation TriangleDistanceValidation CompleteRendererValidation
"$Build/TriangleVisibilityValidation" | tee Exhibits/VehicleReference/VisibilityResults.txt
"$Build/complete/CompleteRendererValidation" | tee Exhibits/VehicleReference/RendererResults.txt
"$Build/TriangleDistanceValidation" | tee Exhibits/VehicleReference/DistanceResults.txt
"$Build/VehicleCapture" "$PWD/_ClaudeScratch/vehicle-reference" | tee Exhibits/VehicleReference/DistanceCaptureResults.txt
"$Build/TriangleDistanceValidation" --vehicle | tee Exhibits/VehicleReference/DistanceVehicleResults.txt
python3 Exhibits/VehicleReference/Code/Images.py
python3 Exhibits/VehicleReference/Code/DistanceImages.py > Exhibits/VehicleReference/DistanceImageResults.txt
python3 Exhibits/VehicleReference/Code/Evidence.py
