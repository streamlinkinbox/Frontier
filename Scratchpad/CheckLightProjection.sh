#!/usr/bin/env bash
# Gate: the panel becomes a real emitter — present in the LUMINAIRE table, not merely appended as geometry.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }

Common="Scratchpad/PixelSpaceLinkStub.cpp
        Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp
        Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp
        Engine/SpatialInterface/InterfacePointerProjection.cpp Engine/SpatialInterface/InterfaceTextProjection.cpp
        Engine/SpatialInterface/InterfaceScreenSequence.cpp Engine/SpatialInterface/InterfaceVectorCodec.cpp
        Engine/SpatialInterface/InterfaceLightProjection.cpp
        Engine/DisplayPresentation/MotionIntegrator.cpp Engine/DisplayPresentation/GlyphSpace.cpp
        Engine/GeometricRaster/SceneStructure.cpp Engine/GeometricRaster/GeometryStructure.cpp
        Engine/ContentInterchange/MaterialIndex.cpp Engine/DeviceExchange/OrientationClassifier.cpp"
Includes="-I Engine -I . -I $VulkanInclude -I ExternalPackages/cgltf -I ExternalPackages/ufbx -I ExternalPackages/stb -I ExternalPackages/imgui"

g++ -std=c++20 -O2 -Wall -Wextra $Includes Scratchpad/LightProjectionTest.cpp $Common \
    -o /tmp/LightProjectionTest || { echo "[LightProjection] COMPILE FAILED"; exit 1; }
/tmp/LightProjectionTest || exit 1

# Same check against the real decoded showroom.
g++ -std=c++20 -O2 $Includes Scratchpad/ShowroomLightTest.cpp $Common \
    Projects/Project-Zero/Source/ShowroomStructure.cpp Projects/Project-Zero/Source/InterfaceTrialSequence.cpp \
    Engine/ContentInterchange/SceneCodec.cpp Engine/ContentInterchange/MaterialCodec.cpp \
    Engine/ContentInterchange/TextureIndex.cpp \
    -o /tmp/ShowroomLightTest || { echo "[LightProjection] showroom harness COMPILE FAILED"; exit 1; }
/tmp/ShowroomLightTest || exit 1

echo "[LightProjection] OK"
