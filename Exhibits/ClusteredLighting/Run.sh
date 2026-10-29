#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
Cmake="${CMAKE:-cmake}"
if ! command -v "$Cmake" >/dev/null && [[ -x ExternalPackages/BuildTools/cmake/data/bin/cmake ]]; then
 Cmake="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"
fi
Build="$PWD/_ClaudeScratch/build/vehicle-reference-cmake"
Output="Exhibits/ClusteredLighting"
node Exhibits/VehicleReference/Code/Materials.mjs
"$Cmake" -S Exhibits/VehicleReference -B "$Build" -DCMAKE_BUILD_TYPE=Release
"$Cmake" --build "$Build" --parallel 2 --target VehicleCapture LightValidation TriangleVisibilityValidation TriangleDistanceValidation CompleteRendererValidation
"$Build/LightValidation" | tee "$Output/ValidationResults.txt"
Ctest="$(dirname "$(command -v "$Cmake")")/ctest"
"$Ctest" --test-dir "$Build" --output-on-failure | tee "$Output/CtestResults.txt"
"$Build/VehicleCapture" "$PWD/_ClaudeScratch/vehicle-reference" --clustered-lights | tee "$Output/CaptureResults.txt"
node --test Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Tools/ClusteredLighting.test.mjs > "$Output/PrototypeAResults.txt"
node --test Experimental/SlateReference/B/Exhibits/IlluminationHost/Tools/LightingSystem.test.mjs > "$Output/PrototypeBResults.txt"
node Exhibits/RendererPlanning/Code/CompareReferences.mjs > "$Output/BinComparisonResults.txt"
python3 Exhibits/RecoveryAudit/Code/ValidateRecovered.py > "$Output/CoreResults.txt"
if [[ "${SANITIZE:-0}" == 1 ]]; then
 Sources=(Frontier/Engine/Diagnostics/Log{RingStore,Categories,Stream}.cpp)
 for Name in ClusterNesting VisibilityCull DiscrepancySequence ReservoirSampling VarianceFiltering DistanceField SurfaceCards AtmosphereModel CelestialModel CloudField FogMedium ReferenceRenderer; do
  Sources+=("Frontier/Engine/Rendering/$Name.cpp")
 done
 "${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -IFrontier "$Output/Code/LightValidation.cpp" "${Sources[@]}" -o _ClaudeScratch/build/LightSanitized
 _ClaudeScratch/build/LightSanitized > "$Output/SanitizerResults.txt" 2>&1
fi
python3 -c "from pathlib import Path; p=Path('$Output/CoreResults.txt'); p.write_text(p.read_text().rstrip()+'\n')"
python3 "$Output/Code/Images.py"
python3 "$Output/Code/Evidence.py"
