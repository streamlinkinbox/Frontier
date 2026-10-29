#!/usr/bin/env bash
# Diagnostic negative control only: production shaders and SPIR-V remain untouched.
set -euo pipefail
cd "$(dirname "$0")/../.."
Audit="$PWD/_ClaudeScratch/glazing-ray-audit"
mkdir -p "$Audit/Frontier/Engine/Rendering" "$Audit/Build"
cp -r Frontier/Engine/Rendering/Shaders "$Audit/Frontier/Engine/Rendering/"
[[ -e "$Audit/Engine" ]] || ln -s "$PWD/Engine" "$Audit/Engine"
cp _ClaudeScratch/build/light-compute/*.spv _ClaudeScratch/build/light-compute/MaterialScene* "$Audit/Build/"
python3 - <<'PY'
from pathlib import Path
P = Path('_ClaudeScratch/glazing-ray-audit/Frontier/Engine/Rendering/Shaders/SolidDielectricTransport.slang')
import subprocess
Original = subprocess.check_output(['git','show','1b5691e:Frontier/Engine/Rendering/Shaders/SolidDielectricTransport.slang'], text=True)
S = Original.replace('    float DistanceCorrection = 0;',
    '    float DistanceCorrection = 0;\n    float Travel = 0, Shortest = 1e9;')
S = S.replace('        Hit = true;', '        Hit = true;\n        if (Inside) { Travel += max(0, Strike.x + DistanceCorrection); Shortest = min(Shortest, max(0, Strike.x + DistanceCorrection)); }')
S = S.replace('    Capped = 1;', '    Capped = 1;\n    Unsupported = uint(min(1e6, Travel * 1000));\n    InternalReflections = uint(min(65535., Shortest * 1e9));')
P.write_text(S)
PY
ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc "$Audit/Frontier/Engine/Rendering/Shaders/LightInfluence.slang" \
 -entry TraceSurfaceTransport -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Audit/Build/Transport.spv"
export VK_ICD_FILENAMES="$PWD/ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json"
set +e
GLAZING_DEBUG_POSE=4 _ClaudeScratch/build/light-compute/LightComputeValidation \
 "$PWD/ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1" "$Audit/Build" --glazing-motion \
 "$PWD/_ClaudeScratch/primary-motion" "$PWD/_ClaudeScratch/glazing-motion" \
 "$PWD/_ClaudeScratch/glazing-motion/SavedDielectric.bin" 2>&1 | tee Exhibits/LightCompute/Results/GlazingMotionRayDistanceAudit.txt
Status=${PIPESTATUS[0]}
set -e
# The production zero-cap gate MUST fail; this instrumented run is not optical acceptance.
[[ "$Status" == 1 ]]
grep -q 'GLAZING_MOTION_BAD.*pixel=12015.*cap=2' Exhibits/LightCompute/Results/GlazingMotionRayDistanceAudit.txt
