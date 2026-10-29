#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 Exhibits/LightCompute/Code/FaintPathProbe.py
Build="$PWD/_ClaudeScratch/build/faint-path"
cp _ClaudeScratch/build/light-compute/*.spv "$Build/"
ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc "$Build/FaintPath.slang" -entry EvaluateFaintPath \
 -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/Receivers.spv"
export VK_ICD_FILENAMES="$PWD/ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json"
_ClaudeScratch/build/light-compute/LightComputeValidation "$PWD/ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1" \
 "$Build" --faint-path | tee Exhibits/LightCompute/Results/FaintPath.txt
python3 - <<'PY'
from pathlib import Path
import hashlib, json
Root = Path.cwd()
Log = (Root / 'Exhibits/LightCompute/Results/FaintPath.txt').read_text()
assert 'FAINT_PATH_PASS' in Log
Build = Root / '_ClaudeScratch/build/faint-path'
Report = {'production_block_sha256': (Build / 'ProductionBlock.sha256').read_text().strip(),
          'generated_shader_sha256': hashlib.sha256((Build / 'FaintPath.slang').read_bytes()).hexdigest(),
          'compiled_shader_sha256': hashlib.sha256((Build / 'Receivers.spv').read_bytes()).hexdigest(),
          'log': Log, 'scope': 'Actual production survival-block branch/weight parity and exact paired-draw RGB first moments. Does not statistically certify PRNG independence or remove finite hard-cap limitations.'}
(Root / 'Exhibits/LightCompute/Results/FaintPath.json').write_text(json.dumps(Report,indent=2)+'\n')
PY
