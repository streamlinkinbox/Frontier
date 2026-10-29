#!/usr/bin/env bash
# 🧩 Repeat a rigid-primary discrepancy with disjoint seeds and spatial on/off controls.
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
CHECK="${CHECK:---replay-rigid-variance-repeat}"
case "$CHECK" in
    --replay-rigid-variance-repeat) RESULT_STEM=ReplayRigidVarianceRepeat ;;
    --replay-rigid-variance-repeat-smoke) RESULT_STEM=ReplayRigidVarianceRepeatSmoke ;;
    *) printf 'Unknown rigid replay repeat check\n' >&2; exit 1 ;;
esac
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-rigid-variance-repeat-$MODE"
PROBE="$BUILD/probe"
PREVIOUS="${PREVIOUS:-$ROOT/Exhibits/LightCompute/Results/ReplayFacetMixtureMotionRepeatSamples.csv}"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/$RESULT_STEM-$MODE.txt}"
if [[ "$MODE" == release && "$CHECK" == --replay-rigid-variance-repeat ]]; then
    TABLE="${TABLE:-$ROOT/Exhibits/LightCompute/Results/ReplayRigidVarianceRepeatSamples.csv}"
else
    TABLE="${TABLE:-$BUILD/$RESULT_STEM-$MODE.csv}"
fi
mkdir -p "$BUILD" "$PROBE" "$(dirname "$RESULTS")" "$(dirname "$TABLE")"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
    cp "$BUILD/$STEM.spv" "$PROBE/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
cp "$BUILD/Primary.spv" "$PROBE/Primary.spv"
"$SLANG" Exhibits/LightCompute/Code/ReplayRigidProbeRegion.slang -I Frontier/Engine/Rendering/Shaders \
    -entry RestrictReplayRigidReceivers -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "$PROBE/ReplayProbeRegion.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" --target LightComputeValidation PathReplayDomainValidation >/dev/null
{
    echo "Rigid-primary disjoint-seed repeat; $CHECK; host $MODE"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    echo 'External Vulkan driver and shader compiler are not sanitizer-instrumented.'
    uname -srm
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.h,PathReplayDomain.cpp,RadiantFacetDistribution.h,RadiantFacetDistribution.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering}.slang \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,LightComputeFormat.h,PathReplayDomainValidation.cpp,ReplayMotionValidation.h,ReplayTextureValidation.h,ReplayTextureRoomValidation.h,ReplayTextureMotionValidation.h,RoughTextureReplayValidation.h,RoughTextureMotionValidation.h,ReplayRoomReference.h,ReplayFacetMixtureCapture.h,ReplayFacetMixtureMotionValidation.h,ReplayRigidProbeRegion.slang,ReplayRigidVarianceRepeat.h} \
        Exhibits/LightCompute/RunReplayRigidVarianceRepeat.sh "$PREVIOUS"
    "$BUILD/PathReplayDomainValidation"
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK" "$PROBE" "$PREVIOUS" "$TABLE"
    sha256sum "$TABLE"
} 2>&1 | tee "$RESULTS"
