#!/usr/bin/env bash
# 🧩 Verify optional area/luminance-weighted emissive replay and default-off parity.
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
CHECK="${CHECK:---replay-facet-power}"
case "$CHECK" in
    --replay-facet-power) RESULT_STEM=ReplayFacetPower ;;
    --replay-facet-power-parity) RESULT_STEM=ReplayFacetPowerParity ;;
    *) printf 'Unknown replay power check\n' >&2; exit 1 ;;
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
BUILD="$ROOT/_ClaudeScratch/build/replay-facet-power-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/$RESULT_STEM-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
compile_replay() {
    local SOURCE="$1" DESTINATION="$2"
    for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$SOURCE" -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv \
            -profile spirv_1_3 -emit-spirv-directly -O0 -o "$DESTINATION/$STEM.spv"
    done
}
compile_replay Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang "$BUILD"
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
if [[ "$CHECK" == --replay-facet-power-parity ]]; then
    PRIOR="$BUILD/prior"
    mkdir -p "$PRIOR"
    git show 496a076:Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang > "$PRIOR/PathReplayReservoir.slang"
    compile_replay "$PRIOR/PathReplayReservoir.slang" "$PRIOR"
    cp "$BUILD/Primary.spv" "$PRIOR/Primary.spv"
fi
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" --target LightComputeValidation PathReplayDomainValidation >/dev/null
{
    echo "Power-weighted facet replay; $CHECK; host $MODE"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    echo 'External Vulkan driver and shader compiler are not sanitizer-instrumented.'
    uname -srm
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.h,PathReplayDomain.cpp,RadiantFacetDistribution.h,RadiantFacetDistribution.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering}.slang \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayDomainValidation.cpp,ReplayFacetPowerValidation.h,ReplayImportanceParityValidation.h,ReplayRoomReference.h}
    "$BUILD/PathReplayDomainValidation"
    if [[ "$CHECK" == --replay-facet-power-parity ]]; then
        printf 'Prior replay shader checkpoint: 496a076\n'
        sha256sum "$PRIOR/PathReplayReservoir.slang"
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK" "$PRIOR"
    else
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK"
    fi
} 2>&1 | tee "$RESULTS"
