#!/usr/bin/env bash
# R7a gate: the running mean is reprojected through the motion vectors, and does so without smearing.
#
# Three checks:
#   1. CPU harness  — the reprojection rule ported from ResolveSurface(): inheritance across a pan, disocclusion
#                     restart on a normal or depth mismatch, off-screen rejection, and a measured noise reduction.
#   2. SPIR-V       — ReSTIRViewport.slang still compiles with the new binding.
#   3. Bindings     — the C++ binding map and the shader agree. A silent disagreement here would bind the texture
#                     table over the history surface image, which is the failure mode worth a dedicated check.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Glslang="${1:-$(command -v glslang || command -v glslangValidator || echo /tmp/gl/build/StandAlone/glslang)}"

echo "[TemporalReprojection] CPU reprojection harness"
g++ -std=c++20 -O2 -Wall -Wextra Scratchpad/TemporalReprojectionTest.cpp -o /tmp/TemporalReprojectionTest || exit 1
/tmp/TemporalReprojectionTest || exit 1

echo
echo "[TemporalReprojection] binding agreement"

Count=$(grep -oP 'kComputeBindingCount\s*=\s*\K[0-9]+' Engine/DeviceExchange/SwapchainExchange.h | head -1)
ShaderSurface=$(grep -oP 'layout\(rgba16f, binding = \K[0-9]+(?=\) uniform image2D HistorySurfaceImage)' Engine/Shaders/ReSTIRViewport.slang)
ShaderTextures=$(grep -oP 'layout\(binding = \K[0-9]+(?=\) uniform sampler2D Textures)' Engine/Shaders/ReSTIRViewport.slang)

Fail=0
# 26 bindings since A7 added the sky-record UBO at 24, pushing Textures[] to 25. HistorySurfaceImage stayed at
# 18 and is still shared with the denoiser for its edge-stopping weights.
[ "$Count" = "26" ]           || { echo "  kComputeBindingCount is $Count, expected 26"; Fail=1; }
[ "$ShaderSurface" = "18" ]   || { echo "  HistorySurfaceImage is at $ShaderSurface, expected 18"; Fail=1; }
[ "$ShaderTextures" = "25" ]  || { echo "  Textures[] is at $ShaderTextures, expected 25"; Fail=1; }

# The variable-count bindless array must be the highest binding in the set — Vulkan requires it.
[ "$ShaderTextures" = "$((Count - 1))" ] \
    || { echo "  Textures[] must be the last binding (kComputeBindingCount - 1 = $((Count - 1)))"; Fail=1; }

# The descriptor write for the new image must exist, or the binding would be left undefined.
grep -q 'WriteImage (18u, HistorySurfaceInfo)' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  no descriptor write for binding 18"; Fail=1; }

# The feature bit must agree between the shader and the C++ mirror.
ShaderBit=$(grep -oP 'kFeatureTemporalReprojection\s*=\s*\K[0-9]+' Engine/Shaders/ReSTIRViewport.slang)
[ "$ShaderBit" = "64" ] || { echo "  shader feature bit is $ShaderBit, expected 64 (1 << 6)"; Fail=1; }
grep -q 'DispatchFeatureTemporalReprojection\s*=\s*1u << 6' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  C++ feature bit mirror missing or not 1u << 6"; Fail=1; }

[ "$Fail" = "0" ] && echo "  bindings and feature bit agree                                   PASS"
[ "$Fail" = "0" ] || exit 1

if [ ! -x "$Glslang" ]; then
    echo
    echo "[TemporalReprojection] glslang not found - SPIR-V check skipped"
    echo "[TemporalReprojection] OK (CPU only)"
    exit 0
fi

echo
echo "[TemporalReprojection] compiling ReSTIRViewport.slang to SPIR-V"
Stage=$(mktemp -d); trap 'rm -rf "$Stage"' EXIT
mkdir -p "$Stage/Shaders"
cp Engine/Shaders/*.slang "$Stage/Shaders/"
"$Glslang" -V --target-env vulkan1.2 -S comp -I"$Stage" "$Stage/Shaders/ReSTIRViewport.slang" \
    -o "$Stage/ReSTIRViewport.spv" >/dev/null || { echo "  SPIR-V COMPILE FAILED"; exit 1; }

# The reflection must actually expose the new image at 18 and the texture table at 19.
Reflection=$("$Glslang" -V --target-env vulkan1.2 -S comp -I"$Stage" -q "$Stage/Shaders/ReSTIRViewport.slang" 2>/dev/null)
echo "$Reflection" | grep -q 'HistorySurfaceImage:.*binding 18' \
    || { echo "  reflection does not show HistorySurfaceImage at binding 18"; exit 1; }
echo "$Reflection" | grep -q 'Textures:.*binding 25' \
    || { echo "  reflection does not show Textures[] at binding 25"; exit 1; }
echo "  SPIR-V compiles and reflects the expected bindings              PASS"

echo
echo "[TemporalReprojection] OK"
