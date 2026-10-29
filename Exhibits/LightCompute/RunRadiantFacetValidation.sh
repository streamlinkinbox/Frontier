#!/usr/bin/env bash
# 🧩 Compare the shared radiant-facet distribution with the pinned exhibit implementation.
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
BUILD="$ROOT/_ClaudeScratch/build/radiant-facet-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/RadiantFacet-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target RadiantFacetValidation >/dev/null
# 📝 Compile the actual former implementation from Git, never a second maintained implementation.
git show fe774a5:Exhibits/LightCompute/Code/EmissiveFacetQuadrature.h > "$BUILD/PriorQuadrature.h"
git show fe774a5:Exhibits/LightCompute/Code/LightSurfaceTransport.h > "$BUILD/PriorTransport.h"
python3 - "$BUILD" <<'PY'
from pathlib import Path
import sys
build = Path(sys.argv[1])
quadrature = (build / 'PriorQuadrature.h').read_text()
transport = (build / 'PriorTransport.h').read_text()
old_distribution = quadrature[quadrature.index('void AppendDistribution('):quadrature.index('uint32_t Next(')]
old_vectors = transport[transport.index('SpatialVector Add('):transport.index('SpectrumRgb Colour(')]
source = '''#include "RadiantFacetFixtures.h"
#include <array>
#include <cstdio>
#include <stdexcept>
struct SpatialVector { double X, Y, Z; };
void Require(bool Condition, const char* Message) { if (!Condition) throw std::runtime_error(Message); }
struct LightComputeArchive {
    std::vector<Frontier::PathReplayFacet> Surfaces;
    std::vector<float> Angles;
    std::array<std::array<float, 4>, 12> Camera{};
};
namespace TransportReference {
''' + old_vectors + '''
}
namespace EmissiveQuadrature {
using namespace TransportReference;
''' + old_distribution + '''
}
int main() {
    LightComputeArchive Scene;
    Scene.Surfaces = ConstructRadiantFacetFixture();
    Scene.Angles.push_back(0);
    EmissiveQuadrature::AppendDistribution(Scene);
    return std::fwrite(Scene.Angles.data() + 1, sizeof(float), Scene.Angles.size() - 1, stdout)
        == Scene.Angles.size() - 1 ? 0 : 1;
}
'''
(build / 'PriorDistribution.cpp').write_text(source)
PY
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -I Frontier -I Exhibits/LightCompute/Code \
    "$BUILD/PriorDistribution.cpp" -o "$BUILD/PriorDistribution"
"$BUILD/PriorDistribution" > "$BUILD/prior.bin"
"$BUILD/RadiantFacetValidation" --binary > "$BUILD/engine.bin"
cmp "$BUILD/prior.bin" "$BUILD/engine.bin"
{
    echo "Radiant facet extraction: $MODE; pinned exhibit source fe774a5796476bb055429e9ee3d1b208202e8aa1"
    echo "Current base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    "$CXX" --version | head -1
    uname -srm
    sha256sum Frontier/Engine/Rendering/RadiantFacetDistribution.{h,cpp} \
        Exhibits/LightCompute/Code/{RadiantFacetValidation.cpp,RadiantFacetFixtures.h}
    "$BUILD/RadiantFacetValidation"
    echo "HISTORICAL_PARITY fixture_facets=4096 binary_bytes=$(wc -c < "$BUILD/engine.bin") exact_equal=1"
    sha256sum "$BUILD/prior.bin" "$BUILD/engine.bin"
    echo 'Finite generated inputs, CPU only; the distribution is global and is not clustered lighting.'
} | tee "$RESULTS"
