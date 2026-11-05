#!/bin/bash
# 🧩 The scan overlay on a Vulkan device, over the hero car: builds nothing, runs XRayScanValidation and XRayScanCapture from a configured build
#    folder and copies the evidence into this exhibit.
#
#      Exhibits/XRayScan/Code/RunXRayScanCapture.sh <build folder>
#
#    The build folder holds XRayScanValidation, XRayScanCapture and (from XRayScanCaptureShaders) ShaderArchives/XRayScanCapture. The overlay is drawn by
#    the stored EngineContent/ShaderArchives/XRayScan programs. The device is SwiftShader, the CPU Vulkan of ExternalPackages/VulkanCompute, so that
#    the run needs no GPU.
set -euo pipefail
BUILD="$(cd "${1:?usage: RunXRayScanCapture.sh <build folder>}" && pwd)"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SS="$ROOT/ExternalPackages/VulkanCompute/SwiftShader"
EXHIBIT="$ROOT/Exhibits/XRayScan"
EVIDENCE="$BUILD/xray-scan-evidence"
cd "$ROOT"

"$BUILD/XRayScanValidation" "$ROOT" | tail -1

rm -rf "$EVIDENCE"
mkdir -p "$EVIDENCE" "$EXHIBIT/Images" "$EXHIBIT/Results"
FRONTIER_VULKAN_DRIVER="$SS/libvulkan.so.1" VK_ICD_FILENAMES="$SS/vk_swiftshader_icd.json" LD_LIBRARY_PATH="$SS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$BUILD/XRayScanCapture" "$ROOT" "$ROOT/EngineContent/ShaderArchives/XRayScan" "$BUILD/ShaderArchives/XRayScanCapture" "$EVIDENCE"

cp "$EVIDENCE"/*.png "$EXHIBIT/Images/"
cp "$EVIDENCE/Captures.tsv" "$EXHIBIT/Results/Captures.tsv"
echo "Copied $(ls "$EVIDENCE"/*.png | wc -l) images and Captures.tsv to $EXHIBIT"
