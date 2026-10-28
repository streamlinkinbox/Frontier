#!/usr/bin/env bash
# 🧩 Verify bounded initial replay candidates, revisions and prior-shader parity.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-candidates-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayCandidates-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
compile_replay() {
    local SOURCE="$1" DESTINATION="$2"
    for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$SOURCE" -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
            -emit-spirv-directly -O0 -o "$DESTINATION/$STEM.spv"
    done
}
compile_replay Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang "$BUILD"
for PAIR in 'GatherReplayReconstruction ReplayGatherReconstruction' 'FinalizeReplayReconstruction ReplayFinalizeReconstruction'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReconstruction.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
if [[ "${PARITY:-0}" == 1 ]]; then
    PRIOR="$BUILD/prior"
    mkdir -p "$PRIOR"
    git show b0cf5c9:Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang > "$PRIOR/PathReplayReservoir.slang"
    compile_replay "$PRIOR/PathReplayReservoir.slang" "$PRIOR"
    cp "$BUILD/Primary.spv" "$PRIOR/Primary.spv"
fi
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" \
    --target LightComputeValidation PathReplayDomainValidation PathReplayVulkanValidation >/dev/null
{
    echo "Replay initial candidates; host $MODE; external Vulkan driver and shaders are not sanitizer-instrumented"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,PathReplayReconstruction,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp} \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayDomainValidation.cpp,PathReplayVulkanValidation.cpp,ReplayCandidateValidation.h,ReplayReconstructionValidation.h,ReplayRoomReference.h,PathReplayGpuValidation.h,ReplayRigidTailAudit.h,ReplayImportanceParityValidation.h,ReplayFacetMixtureValidation.h} \
        Exhibits/LightCompute/RunReplayCandidates.sh
    "$BUILD/PathReplayDomainValidation"
    "$BUILD/PathReplayVulkanValidation"
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-candidates
    if [[ "${PARITY:-0}" == 1 ]]; then
        printf 'Prior replay shader checkpoint: b0cf5c9\n'
        sha256sum "$PRIOR/PathReplayReservoir.slang"
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-candidates-parity "$PRIOR"
    fi
} 2>&1 | tee "$RESULTS"
