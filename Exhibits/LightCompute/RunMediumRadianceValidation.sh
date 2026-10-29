#!/usr/bin/env bash
# 🧩 Compare Engine cloud/fog shader payloads with the pinned exhibit inputs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then
    CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"
fi
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/medium-radiance-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/MediumRadiance-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target MediumRadianceValidation >/dev/null
# 📝 Extract the actual previous payload builders from Git into scratch; no second maintained copy.
git show fe774a5:Exhibits/LightCompute/Code/CloudCubeValidation.h > "$BUILD/PriorCloud.h"
git show fe774a5:Exhibits/LightCompute/Code/FogScatteringValidation.h > "$BUILD/PriorFog.h"
python3 - "$BUILD" <<'PY'
from pathlib import Path
import sys
build = Path(sys.argv[1])
cloud = (build / 'PriorCloud.h').read_text()
fog = (build / 'PriorFog.h').read_text()
cloud = cloud[cloud.index('uint32_t AppendCloudParameters('):cloud.index('void ValidateCloudCube(')]
fog = fog[fog.index('uint32_t AppendFogParameters('):fog.index('SpatialVector FogCameraRay(')]
source = '''#include "MediumRadianceFixtures.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace Frontier;
struct LightComputeArchive {
    std::vector<float> Angles;
    std::array<std::array<float, 4>, 12> Camera{};
};
''' + cloud + fog + '''
int main() {
    LightComputeArchive Scene;
    for (uint32_t Index = 0; Index < 64; ++Index) {
        const auto Specification = ConstructCloudRadianceFixture(Index);
        Scene.Camera[0][2] = float(Specification.ObserverHeightM);
        Scene.Angles.clear();
        AppendCloudParameters(Scene, Specification.Medium, Specification.SolarDirection,
                              Specification.SolarRadiance, Specification.ViewSamples);
        if (std::fwrite(Scene.Angles.data(), sizeof(float), Scene.Angles.size(), stdout) != Scene.Angles.size())
            return 1;
    }
    Scene.Angles.clear();
    AppendFogParameters(Scene);
    return std::fwrite(Scene.Angles.data(), sizeof(float), Scene.Angles.size(), stdout) == Scene.Angles.size() ? 0 : 1;
}
'''
(build / 'PriorMedium.cpp').write_text(source)
PY
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -I Frontier -I Exhibits/LightCompute/Code "$BUILD/PriorMedium.cpp" -o "$BUILD/PriorMedium"
"$BUILD/PriorMedium" > "$BUILD/prior.bin"
"$BUILD/MediumRadianceValidation" --binary > "$BUILD/engine.bin"
cmp "$BUILD/prior.bin" "$BUILD/engine.bin"
{
    echo "Medium payload extraction: $MODE; pinned exhibit source fe774a5796476bb055429e9ee3d1b208202e8aa1"
    echo "Current base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    "$CXX" --version | head -1
    uname -srm
    sha256sum Frontier/Engine/Rendering/MediumRadiancePayload.{h,cpp} \
        Exhibits/LightCompute/Code/{MediumRadianceValidation.cpp,MediumRadianceFixtures.h}
    "$BUILD/MediumRadianceValidation"
    echo "HISTORICAL_PARITY cloud_cases=64 fog_cases=1 binary_bytes=$(wc -c < "$BUILD/engine.bin") exact_equal=1"
    sha256sum "$BUILD/prior.bin" "$BUILD/engine.bin"
    echo 'Bounded float32 ABI checks only; no physical sky, hardware-GPU or editor integration claim.'
} | tee "$RESULTS"
