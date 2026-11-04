#!/bin/bash
#============================================================================================================================================
#                                                         CAPTUREREFERENCE.SH
#============================================================================================================================================
# 🧩 Runs the original VehicleHMI demo headless (Dawn + SwiftShader) and records the reference stream for the C++ port.
#
#    CaptureReference.sh <DemoFolder> <OutFolder> [Frames] [Quality] [Size]
#    DemoFolder: Experimental/VehicleHmiPrototype extracted from SultanAladin/Frontier 6afb6f00 (see README).
set -e
Demo=$(realpath "$1")
Out=$(realpath -m "$2")
Frames=${3:-12}
Quality=${4:-low}
Size=${5:-480x270}
Here=$(dirname "$(realpath "$0")")
Root=$(realpath "$Here/../../..")
SwiftShader=${SWIFTSHADER:-$Root/ExternalPackages/VulkanCompute/SwiftShader}
mkdir -p "$Out"
cd "$Demo/Tools"
VK_ICD_FILENAMES="$SwiftShader/vk_swiftshader_icd.json" LD_LIBRARY_PATH="$SwiftShader" BACKEND=vulkan SIZE="$Size" \
HARNESS_TIMEOUT=6000000 OUT="$Out/Present.png" CAPTURE_OUT="$Out" CAPTURE_FRAMES="${CAPTURE_FRAMES:-0,1,2,5,11}" \
CAPTURE_SETTINGS='{"CloudEnabled":false,"CloudShadows":false,"FlareEnabled":false,"SmokeEnabled":false,"Sparks":false,"TyreMarks":false}' \
CAPTURE_BRINGUP="${CAPTURE_BRINGUP:-Occupant distance space,Card albedo,Card normal,Card emission,Card depth}" \
CAPTURE_TEXTURES="${CAPTURE_TEXTURES:-Scene distance cascade 0,Scene distance cascade 1,Scene distance cascade 2,Scene distance cascade 3,Sky view,Atmosphere transmittance 256×64,Atmosphere multiple scattering 32×32,Sky-view transmission,Shadow cascades,Card direct irradiance,Card final radiance,Surface depth,Surface attributes,Surface motion,Direct radiance,Resampling guide A,Resampling guide B,Resampled irradiance,Reflection radiance,Diffuse integrated,Diffuse screen,Glossy integrated,Glossy screen,Recombined radiance,Resolved radiance A,Resolved radiance B,Post radiance,Capture,region:Reservoirs 0,region:Reservoirs 1,region:Radiosity sites}" \
HOOKS="$Here/CaptureHooks.mjs" node HeadlessHarness.mjs .. "$Frames" "?capture=1&quality=$Quality&omit=${CAPTURE_OMIT:-Cabin}"
