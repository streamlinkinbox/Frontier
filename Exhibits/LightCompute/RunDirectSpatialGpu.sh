#!/usr/bin/env bash
# 📝 Compare historical and current Vulkan direct reservoirs against exhaustive light summation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
BUILD="$ROOT/_ClaudeScratch/build/direct-spatial-gpu"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if [[ "${PREPARE:-0}" == 1 ]]; then python3 Exhibits/LightCompute/Code/Prepare.py; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
mkdir -p "$BUILD/original" "$BUILD/current" Exhibits/LightCompute/Results
git show 98fbc76e8b25908b8961072d1bfe79bbea5ff993:Frontier/Engine/Rendering/Shaders/LightInfluence.slang > "$BUILD/original/LightInfluence.slang"
for VARIANT in original current; do
    SOURCE="$BUILD/original/LightInfluence.slang"
    if [[ "$VARIANT" == current ]]; then SOURCE="Frontier/Engine/Rendering/Shaders/LightInfluence.slang"; fi
    for PAIR in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' \
                'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$SOURCE" -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv \
            -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$VARIANT/$STEM.spv"
    done
done
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/DirectSpatial-vulkan.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    "$SLANG" -version
    printf 'Historical shader: 98fbc76e8b25908b8961072d1bfe79bbea5ff993\n'
    sha256sum "$BUILD/original/LightInfluence.slang" Frontier/Engine/Rendering/Shaders/LightInfluence.slang \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,DirectSpatialGpuValidation.h}
    printf '%s\n' '--- original ---'
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD/original" --direct-spatial-observe
    printf '%s\n' '--- current ---'
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD/current" --direct-spatial
} | tee "$RESULTS"
