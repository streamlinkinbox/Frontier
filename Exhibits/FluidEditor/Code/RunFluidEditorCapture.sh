#!/bin/bash
# 🧩 The Fluid editor window with the Pyro solver's own picture in its viewport: builds nothing, runs FluidEditorCapture from a configured build folder and
#    copies the evidence into this exhibit, only when every claim held.
#
#      Exhibits/FluidEditor/Code/RunFluidEditorCapture.sh <build folder>
#
#    The build folder holds FluidEditorCapture (ninja FluidEditorCapture). The solver runs on a Vulkan device: the driver FRONTIER_VULKAN_DRIVER names, else the
#    SwiftShader that Automation/PrepareVulkanCompute.py fetches into ExternalPackages/VulkanCompute (the CPU: one capture takes minutes there).
set -euo pipefail
BUILD="$(cd "${1:?usage: RunFluidEditorCapture.sh <build folder>}" && pwd)"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EXHIBIT="$ROOT/Exhibits/FluidEditor"
EVIDENCE="$BUILD/fluid-editor-evidence"
SWIFT="$ROOT/ExternalPackages/VulkanCompute/SwiftShader"
cd "$ROOT"

if [ -z "${FRONTIER_VULKAN_DRIVER:-}" ] && [ -f "$SWIFT/libvulkan.so.1" ]; then
    export FRONTIER_VULKAN_LIB="$SWIFT/libvulkan.so.1" FRONTIER_VULKAN_DRIVER="$SWIFT/libvulkan.so.1" VK_ICD_FILENAMES="$SWIFT/vk_swiftshader_icd.json" LD_LIBRARY_PATH="$SWIFT${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
rm -rf "$EVIDENCE"
mkdir -p "$EVIDENCE" "$EXHIBIT/Images" "$EXHIBIT/Results"
"$BUILD/FluidEditorCapture" EngineContent Projects/Everest "$EVIDENCE" | tail -3

cp "$EVIDENCE"/FluidEditor-*.png "$EXHIBIT/Images/"
cp "$EVIDENCE/FluidEditor.tsv" "$EXHIBIT/Results/FluidEditor.tsv"
echo "Copied $(ls "$EVIDENCE"/FluidEditor-*.png | wc -l) images and FluidEditor.tsv to $EXHIBIT"
