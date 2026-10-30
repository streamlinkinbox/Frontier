#!/usr/bin/env bash
# 🧩 Validate fresh RGB-norm target cohorts and all previous native units.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-$ROOT/_ClaudeScratch/venv/bin/cmake}"
CTEST="${CTEST:-$ROOT/_ClaudeScratch/venv/bin/ctest}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
    export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
fi
BUILD="$ROOT/_ClaudeScratch/build/facet-norm-$MODE"
mkdir -p "$BUILD"
{
    printf 'FACET_NORM_BUILD mode=%s parent=%s working_changes=1\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    sha256sum Frontier/Engine/Rendering/FacetArea{Reservoir,Proposals,Score,Transport}.{h,cpp} Frontier/Engine/Rendering/CMakeLists.txt \
        Exhibits/LightCompute/{FacetAreaTransport/CMakeLists.txt,Code/FacetAreaProposal{Validation.cpp,Scenes.py},Code/FacetAreaPortValidation.cpp,RunFacetAreaTransport.sh,Code/FacetAreaScoreValidation.cpp,Code/FacetAreaTransportValidation.cpp,Code/FacetAreaTransportScenes.py}
    sha256sum Exhibits/LightCompute/Code/{TriangleReconnection,RgbTrace,GeometricAreaReservoir,DeformedAreaReservoir}Validation.h \
        Exhibits/LightCompute/Code/{LightComputeFormat.h,PerspectiveAreaReservoirValidation.h} Frontier/Engine/Rendering/{TriangleVisibilitySpace,ClusterNesting,PathReplayFootprint}.h
    sha256sum Exhibits/LightCompute/Code/FacetAreaOcclusionValidation.cpp \
        Exhibits/LightCompute/FacetAreaOcclusion/CMakeLists.txt Exhibits/LightCompute/RunFacetAreaOcclusion.sh
    "$CMAKE" -S Exhibits/LightCompute/FacetNorm -B "$BUILD" "${OPTIONS[@]}"
    sha256sum Frontier/Engine/Rendering/FacetAreaPacket.h Exhibits/LightCompute/Code/FacetPacketValidation.cpp \
        Exhibits/LightCompute/FacetPacket/CMakeLists.txt Exhibits/LightCompute/RunFacetPacket.sh
    sha256sum Exhibits/LightCompute/Code/FacetMixedValidation.cpp Exhibits/LightCompute/FacetMixed/CMakeLists.txt \
        Exhibits/LightCompute/RunFacetMixed.sh
    sha256sum Exhibits/LightCompute/Code/FacetNorm{Scenes.py,Validation.cpp} Exhibits/LightCompute/FacetNorm/CMakeLists.txt Exhibits/LightCompute/RunFacetNorm.sh
    "$CMAKE" --build "$BUILD" --target FacetAreaProposalValidation FacetAreaPortValidation FacetAreaScoreValidation FacetAreaTransportValidation FacetAreaOcclusionValidation FacetPacketValidation FacetMixedValidation FacetNormValidation --parallel 1
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/mixed/packet/native/baseline/FacetAreaScoreValidation" --unit
    "$BUILD/mixed/packet/native/baseline/FacetAreaProposalValidation" --unit
    "$BUILD/mixed/packet/native/baseline/FacetAreaPortValidation" --unit
    "$BUILD/mixed/packet/native/baseline/FacetAreaTransportValidation" --unit
    "$BUILD/mixed/packet/native/FacetAreaOcclusionValidation" --unit
    "$BUILD/mixed/packet/FacetPacketValidation" --unit
    "$BUILD/mixed/FacetMixedValidation" --unit
    "$BUILD/FacetNormValidation" --unit
    "$BUILD/FacetNormValidation" "$BUILD/captures"
    sha256sum "$BUILD/mixed/packet/native/baseline/FacetAreaProposalScenes.h" "$BUILD/FacetNormScenes.h" "$BUILD"/captures/*.f64
} 2>&1 | tee "Exhibits/LightCompute/Results/FacetNorm-$MODE.txt"
