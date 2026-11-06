#!/bin/bash
# 🧩 The Smoke & fire editor with the plume's own picture in its stage: builds nothing, runs FluidPlumeEditorCapture from a configured build folder and
#    copies the evidence into this exhibit, only when every claim held.
#
#      Exhibits/FluidPlumeEditor/Code/RunFluidPlumeEditorCapture.sh <build folder> [low|medium|high]
#
#    The build folder holds FluidPlumeEditorCapture (ninja FluidPlumeEditorCapture). The plume runs on a Vulkan device: the driver FRONTIER_VULKAN_DRIVER
#    names, else the SwiftShader that Automation/PrepareVulkanCompute.py fetches into ExternalPackages/VulkanCompute (the CPU: one capture of the high grade
#    takes minutes there). The grade is the Render settings quality tier the plume's lattice follows; high is the Everest project's own.
set -euo pipefail
BUILD="$(cd "${1:?usage: RunFluidPlumeEditorCapture.sh <build folder> [low|medium|high]}" && pwd)"
GRADE="${2:-high}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EXHIBIT="$ROOT/Exhibits/FluidPlumeEditor"
EVIDENCE="$BUILD/fluid-plume-live-evidence"
SWIFT="$ROOT/ExternalPackages/VulkanCompute/SwiftShader"
cd "$ROOT"

if [ -z "${FRONTIER_VULKAN_DRIVER:-}" ] && [ -f "$SWIFT/libvulkan.so.1" ]; then
    export FRONTIER_VULKAN_DRIVER="$SWIFT/libvulkan.so.1" VK_ICD_FILENAMES="$SWIFT/vk_swiftshader_icd.json" LD_LIBRARY_PATH="$SWIFT${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
rm -rf "$EVIDENCE"
mkdir -p "$EVIDENCE" "$EXHIBIT/Images" "$EXHIBIT/Results"
"$BUILD/FluidPlumeEditorCapture" EngineContent Projects/Everest EngineContent/ShaderArchives "$EVIDENCE" "$GRADE" | tail -3

cp "$EVIDENCE"/SmokeAndFireEditor-Live-*.png "$EXHIBIT/Images/"
cp "$EVIDENCE/LivePlume.tsv" "$EXHIBIT/Results/LivePlume.tsv"
echo "Copied $(ls "$EVIDENCE"/SmokeAndFireEditor-Live-*.png | wc -l) images and LivePlume.tsv to $EXHIBIT"
