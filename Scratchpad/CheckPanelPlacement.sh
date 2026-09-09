#!/usr/bin/env bash
# Gate: the 3D interface is composed into the room at the level's anchor, facing the camera.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }
g++ -std=c++20 -O2 -I . -I "$VulkanInclude" \
    -I ExternalPackages/cgltf -I ExternalPackages/ufbx -I ExternalPackages/stb \
    Scratchpad/PanelPlacementTest.cpp \
    Projects/Project-Zero/Source/InterfaceTrialSequence.cpp Projects/Project-Zero/Source/ShowroomStructure.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp \
    Engine/ContentInterchange/SceneCodec.cpp Engine/ContentInterchange/MaterialCodec.cpp \
    Engine/ContentInterchange/MaterialIndex.cpp Engine/ContentInterchange/TextureIndex.cpp \
    Engine/GeometricRaster/SceneStructure.cpp Engine/GeometricRaster/GeometryStructure.cpp \
    Engine/DeviceExchange/OrientationClassifier.cpp -o /tmp/PanelPlacementTest \
    || { echo "[PanelPlacement] COMPILE FAILED"; exit 1; }
/tmp/PanelPlacementTest
S=$?
[ $S -eq 0 ] && echo "[PanelPlacement] OK"
exit $S
