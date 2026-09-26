#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
Stage=$(mktemp -d)
trap 'rm -rf "$Stage"' EXIT
export FRONTIER_PATCH_CACHE="$Stage/cache"
Flags=(-std=c++20 -O1 -g -I. -IExternalPackages/vulkan-headers/include)
if [[ "${SANITIZE:-0}" == 1 ]]; then Flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-g++}" "${Flags[@]}" Tools/Build/Gates/PatchGeometryGate.cpp \
 Engine/GeometricRaster/SceneStructure.cpp Engine/GeometricRaster/GeometryStructure.cpp \
 Engine/ContentInterchange/MaterialIndex.cpp Engine/DeviceExchange/OrientationClassifier.cpp \
 Engine/Editor/ConstructWorld.cpp -o "$Stage/PatchGeometryGate"
"$Stage/PatchGeometryGate" "${PATCH_EVIDENCE:-$Stage/images}"
