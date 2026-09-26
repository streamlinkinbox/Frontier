#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
Stage=$(mktemp -d)
trap 'rm -rf "$Stage"' EXIT
DO_STAGE="$Stage" python3 Exhibits/Workbench/Materials/StageAtrousDenoise.py
Flags=(-std=c++20 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DFRONTIER_CPU_PORT
 -IExhibits/Workbench/Materials -IEngine/DisplayPresentation -IEngine/Shaders
 -IExternalPackages/vulkan-headers/include -I"$Stage")
if [[ "${SANITIZE:-0}" == 1 ]]; then Flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-g++}" "${Flags[@]}" Tools/Build/Gates/ProgressiveDenoiseGate.cpp \
 Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp Engine/DisplayPresentation/ReSTIRIntegrator.cpp \
 Engine/GeometricRaster/CameraProjection.cpp Engine/DeviceExchange/OrientationClassifier.cpp \
 -o "$Stage/ProgressiveDenoiseGate"
"$Stage/ProgressiveDenoiseGate"
