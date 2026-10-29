#!/usr/bin/env bash
#==========================================================================================================================================
#                                               RUNREPLAYMATERIALSAMPLING.SH
#==========================================================================================================================================
# 🧩 Verify replay material sampling against an independent double-precision hemisphere integral.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
SHADER_ROOT="$ROOT"
SOURCE="$SHADER_ROOT/Frontier/Engine/Rendering/Shaders"
if [[ "${BASELINE:-0}" == 1 && "${SANITIZE:-0}" == 1 ]]; then
    echo 'Choose the historical shader or host sanitizers, not both.' >&2
    exit 2
fi
if [[ "${BASELINE:-0}" == 1 ]]; then MODE=baseline; fi
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-material-sampling-$MODE}"
SHADERS="$BUILD/shaders"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayMaterialSampling-$MODE.txt}"
mkdir -p "$SHADERS" "$(dirname "$RESULTS")"
rm -f "$SHADERS"/*.spv
if [[ "$MODE" == baseline ]]; then
    BASELINE_REVISION="${BASELINE_REVISION:-dcc1283}"
    SNAPSHOT="$BUILD/baseline-source"
    mkdir -p "$SNAPSHOT"
    git archive "$BASELINE_REVISION" Frontier/Engine/Rendering/Shaders Engine/Render/Shader | tar -x -C "$SNAPSHOT"
    SHADER_ROOT="$SNAPSHOT"
    SOURCE="$SHADER_ROOT/Frontier/Engine/Rendering/Shaders"
fi
"$SLANG" Exhibits/LightCompute/Code/ReplayMaterialSamplingValidation.slang -I "$SOURCE" \
    -entry InspectReplayMaterialSampling -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "$SHADERS/ReplayMaterialSampling.spv"
# 📝 The fixture dispatches the probe directly; these names provide the existing shared descriptor layout.
for NAME in Primary ReplayGenerate ReplayResolve; do cp "$SHADERS/ReplayMaterialSampling.spv" "$SHADERS/$NAME.spv"; done
CMAKE_OPTIONS=(-G Ninja -DCMAKE_BUILD_TYPE=Release)
if [[ "$MODE" == sanitized ]]; then
    CMAKE_OPTIONS=(-G Ninja -DCMAKE_BUILD_TYPE=Debug
                   '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
                   '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
fi
"${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" "${CMAKE_OPTIONS[@]}"
"${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
{
    echo "Authored material hemisphere sampling; SwiftShader and shaders are not sanitizer-instrumented."
    echo "Host base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    if [[ "$MODE" == baseline ]]; then echo "Historical shader $BASELINE_REVISION; a failed acceptance gate is expected."; fi
    if [[ "$MODE" == sanitized ]]; then
        echo "Host ASan+UBSan: ${CMAKE_OPTIONS[*]}; ASAN_OPTIONS=$ASAN_OPTIONS UBSAN_OPTIONS=$UBSAN_OPTIONS"
    fi
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum "$SOURCE"/{LightInfluence,PathReplayReservoir,MaterialRayScattering,MaterialRayTransport,SolarOptics}.slang \
        "$SHADER_ROOT"/Engine/Render/Shader/{ScatteringEvaluation,AutomotivePaint}.slang \
        Exhibits/LightCompute/Code/{ReplayMaterialSamplingValidation.slang,ReplayMaterialSamplingValidation.h,ReplayMaterialPathValidation.h,LightComputeValidation.cpp} \
        "$SHADERS/ReplayMaterialSampling.spv"
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$SHADERS" --replay-material-sampling
} 2>&1 | tee "$RESULTS"
