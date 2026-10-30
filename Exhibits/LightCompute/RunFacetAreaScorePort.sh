#!/usr/bin/env bash
# 🧩 Build compensated area targets and prior proposal/selection primitives without Vulkan or renderer activation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-$ROOT/_ClaudeScratch/venv/bin/cmake}"
CTEST="${CTEST:-$ROOT/_ClaudeScratch/venv/bin/ctest}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
    export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
fi
BUILD="$ROOT/_ClaudeScratch/build/facet-area-score-port-$MODE"
mkdir -p "$BUILD"
{
    printf 'FACET_AREA_SCORE_BUILD mode=%s parent=%s working_changes=1\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    sha256sum Frontier/Engine/Rendering/FacetArea{Reservoir,Proposals,Score}.{h,cpp} Frontier/Engine/Rendering/CMakeLists.txt \
        Exhibits/LightCompute/{FacetAreaScorePort/CMakeLists.txt,Code/FacetAreaProposal{Validation.cpp,Scenes.py},Code/FacetAreaPortValidation.cpp,RunFacetAreaScorePort.sh,Code/FacetAreaScoreValidation.cpp}
    sha256sum Exhibits/LightCompute/Code/{TriangleReconnection,RgbTrace,GeometricAreaReservoir,DeformedAreaReservoir}Validation.h \
        Exhibits/LightCompute/Code/LightComputeFormat.h Frontier/Engine/Rendering/{TriangleVisibilitySpace,ClusterNesting,PathReplayFootprint}.h
    "$CMAKE" -S Exhibits/LightCompute/FacetAreaScorePort -B "$BUILD" "${OPTIONS[@]}"
    "$CMAKE" --build "$BUILD" --target FacetAreaProposalValidation FacetAreaPortValidation FacetAreaScoreValidation --parallel 1
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/FacetAreaScoreValidation" --unit
    "$BUILD/FacetAreaScoreValidation" --fixtures "$BUILD/source-fixtures.bin"
    "$BUILD/FacetAreaProposalValidation" --unit
    "$BUILD/FacetAreaPortValidation" --unit
    "$BUILD/FacetAreaProposalValidation" --capture "$BUILD/proposals.bin" "$BUILD/fixtures.bin"
    sha256sum "$BUILD/FacetAreaProposalScenes.h" "$BUILD/proposals.bin" "$BUILD/fixtures.bin" "$BUILD/source-fixtures.bin"
} 2>&1 | tee "Exhibits/LightCompute/Results/FacetAreaScorePort-$MODE.txt"
