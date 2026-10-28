#!/usr/bin/env bash
# 🧩 Recheck compact replay storage and decoded captures after the selected-history source repair.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
BUILD="$ROOT/_ClaudeScratch/build/replay-temporal-contract-release"
OUTPUT="$ROOT/_ClaudeScratch/replay-temporal-compact-regression"
RESULTS="$ROOT/Exhibits/LightCompute/Results/ReplayTemporalContract-regression.txt"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
[[ -x "$BUILD/LightComputeValidation" ]] || { echo 'Run RunReplayTemporalContract.sh first'; exit 1; }
mkdir -p "$OUTPUT"
{
    echo 'Compact regression using the final temporal-contract host and default checked shaders'
    sha256sum Exhibits/LightCompute/RunReplayTemporalRegression.sh \
        Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        "$BUILD"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory}.spv
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-compact-storage
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --capture-replay-compact-storage "$OUTPUT"
    "$PYTHON" - "$OUTPUT" <<'PY'
import hashlib
from pathlib import Path
import sys
from PIL import Image
source = Path(sys.argv[1])
for name in ('CompactReplayLegacy', 'CompactReplayPacked'):
    with Image.open(source / (name + '.ppm')) as current:
        pixels = current.convert('RGB').tobytes()
    with Image.open(Path('Exhibits/LightCompute/Images') / (name + '.png')) as prior:
        assert pixels == prior.convert('RGB').tobytes()
    print(name + ': RGB exact to completed compact checkpoint ' + hashlib.sha256(pixels).hexdigest())
PY
} 2>&1 | tee "$RESULTS"
"$PYTHON" - "$RESULTS" <<'PY' | tee -a "$RESULTS"
from pathlib import Path
import sys
prior = Path('Exhibits/LightCompute/Results/ReplayCompactStorage-release.txt').read_text()
current = Path(sys.argv[1]).read_text()
rows = lambda text: [line for line in text.splitlines() if line.startswith('COMPACT_')]
assert rows(prior) == rows(current)
print(f'PASS {len(rows(prior))} compact rows and two decoded captures unchanged after temporal repair')
PY
