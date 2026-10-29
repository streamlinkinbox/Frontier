#!/usr/bin/env bash
# 🧩 Replay selected rare samples and independently reconstruct their spatial reservoir weights.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then
    CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"
fi
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-rigid-tail-audit-$MODE"
TABLE="${TABLE:-$ROOT/Exhibits/LightCompute/Results/ReplayRigidVarianceRepeatSamples.csv}"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayRigidTailAudit-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$SLANG" Exhibits/LightCompute/Code/ReplayRigidProbeRegion.slang -I Frontier/Engine/Rendering/Shaders \
    -entry RestrictReplayRigidReceivers -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "$BUILD/ReplayProbeRegion.spv"
"$SLANG" Exhibits/LightCompute/Code/ReplayRigidTailAudit.slang -I Frontier/Engine/Rendering/Shaders \
    -entry AuditReplayRigidTail -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "$BUILD/ReplayTailAudit.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" --target LightComputeValidation PathReplayDomainValidation >/dev/null
{
    echo "Rigid-primary rare-sample reservoir audit; host $MODE"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    echo 'External Vulkan driver and shader compiler are not sanitizer-instrumented.'
    uname -srm
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.h,PathReplayDomain.cpp,RadiantFacetDistribution.h,RadiantFacetDistribution.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering}.slang \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,LightComputeFormat.h,PathReplayDomainValidation.cpp,ReplayMotionValidation.h,ReplayTextureValidation.h,ReplayTextureRoomValidation.h,ReplayTextureMotionValidation.h,RoughTextureReplayValidation.h,RoughTextureMotionValidation.h,ReplayRoomReference.h,ReplayFacetMixtureCapture.h,ReplayFacetMixtureMotionValidation.h,ReplayRigidProbeRegion.slang,ReplayRigidTailAudit.slang,ReplayRigidTailBreakdown.slang,ReplayRigidTailAudit.h} \
        Exhibits/LightCompute/RunReplayRigidTailAudit.sh "$TABLE"
    "$BUILD/PathReplayDomainValidation"
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-rigid-tail-audit "$TABLE"
} 2>&1 | tee "$RESULTS"
