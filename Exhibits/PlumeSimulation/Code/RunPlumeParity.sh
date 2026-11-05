#!/bin/bash
#============================================================================================================================================
#                                                          RUNPLUMEPARITY.SH
#============================================================================================================================================
# 🧩 The plume's parity run: the demo's WGSL plume (SmokeSequence over PlumeSpace) runs on Dawn over SwiftShader and leaves a reference folder; the C++
#    port runs the same burnout on Vulkan over the same SwiftShader and compares what its Slang programs leave, element for element.
#
#        Exhibits/PlumeSimulation/Code/RunPlumeParity.sh <build folder> [grade low|medium|high] [refreshes] [trace WxH] [screen WxH]
#
#    Needs: npm ci in Experimental/VehicleHmiPrototype/Tools (webgpu 0.6.2), node RunOnSoftwareGpu.mjs --print once (fetches SwiftShader for Dawn),
#    python Automation/PrepareVulkanCompute.py (SwiftShader for the C++ side), and the build's PlumeSimulationCapture.
set -eu
Root=$(cd "$(dirname "$0")/../../.." && pwd)
Build=$(cd "${1:?usage: RunPlumeParity.sh <build folder> [grade] [refreshes] [trace WxH] [screen WxH]}" && pwd)
Grade=${2:-low}
Refreshes=${3:-170}
Trace=${4:-128x80}
Screen=${5:-256x160}
Scratch=${PLUME_PARITY_SCRATCH:-$Root/_ClaudeScratch/plume}
Reference=$Scratch/reference-$Grade-$Refreshes
Swift=$Root/ExternalPackages/VulkanCompute/SwiftShader
Shaders=${PLUME_SHADERS:-$Build/ShaderArchives/PlumeSimulation}

echo "① the demo's WGSL on Dawn → $Reference"
(cd "$Root/Experimental/VehicleHmiPrototype/Tools" && node RunOnSoftwareGpu.mjs node PlumeParityGpu.mjs "$Reference" "$Grade" "$Refreshes" "$Trace" "$Screen")

echo "② the C++ port on Vulkan, compared"
FRONTIER_VULKAN_DRIVER=$Swift/libvulkan.so.1 VK_ICD_FILENAMES=$Swift/vk_swiftshader_icd.json LD_LIBRARY_PATH=$Swift \
    "$Build/PlumeSimulationCapture" parity "$Root" "$Shaders" "$Reference" "$Root/Exhibits/PlumeSimulation/Results"
