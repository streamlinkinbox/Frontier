#!/usr/bin/env bash
# 🧩 Compile and measure isolated Vulkan secondary-vertex transport and deliberate support/measure errors.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if ! command -v "$CTEST" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/ctest" ]]; then CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' '-DCMAKE_CXX_FLAGS_DEBUG=-O1 -g')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/weighted-quartet-$MODE"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/weighted-quartet-capture-$MODE}"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/WeightedQuartet-$MODE.txt}"
mkdir -p "$BUILD" "$OUTPUT" "$(dirname "$RESULTS")"
{
    printf 'Secondary GPU host mode=%s revision=%s working_changes=1 external_driver_sanitized=0\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    "$SLANG" -version
    sha256sum Exhibits/LightCompute/Code/{CompileWeightedQuartet.py,CompileWeightedPair.py,CompileVisibilityReuse.py,CompileRayBudget.py,CompileReceiverBank.py,CompileCandidateBank.py,CompileCacheRefresh.py,CompileVertexCache.py,CompileTriangleReconnection.py,SecondaryReconnectionGpu.slang,WeightedQuartetValidation.h,WeightedPairValidation.h,VisibilityReuseValidation.h,RayBudgetValidation.h,ReceiverBankValidation.h,CandidateBankValidation.h,CacheRefreshValidation.h,VertexCacheValidation.h,TriangleReconnectionValidation.h,LightComputeValidation.cpp,LightComputeFormat.h,RectangleDomainValidation.h,RectangleReconnectionGpuValidation.h,DirectSpatialGpuValidation.h} \
        Exhibits/LightCompute/{RunWeightedQuartet.sh,CMakeLists.txt} \
        Frontier/Engine/Rendering/Shaders/*.slang "$DRIVER" "$SLANG" Automation/ReplayToolDependencies.json
    python3 Exhibits/LightCompute/Code/CompileWeightedQuartet.py "$BUILD/WeightedQuartet.slang"
    sha256sum "$BUILD/WeightedQuartet.slang"
    for PAIR in 'GenerateVertexPayload DiffuseGenerate' 'EvaluateVertexPayload DiffuseResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$BUILD/WeightedQuartet.slang" \
            -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
            -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
    done
    sha256sum "$BUILD"/*.spv
    "$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}"
    "$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --weighted-quartet "$OUTPUT"
    sha256sum "$OUTPUT"/*.f32
} 2>&1 | tee "$RESULTS"
