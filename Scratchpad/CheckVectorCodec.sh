#!/usr/bin/env bash
# P4 gate: an SVG path converts to figures that draw the right shape, the right way up, for one draw.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }

sed -E 's/\.(xyz|xy|yz|xz|zw)\b([^(])/.\1()\2/g' Engine/Shaders/InterfaceSignedDistance.slang \
    > /tmp/InterfaceSignedDistance.port.inc
mkdir -p Diagnostics

# PixelSpaceLinkStub stands in for GlyphSpace's raster entry points, which reach into ImGui. The codec only calls
#    Flatten, which is pure geometry, so a headless proof should not need an immediate-mode UI to link.
g++ -std=c++20 -O2 -I Scratchpad -I Engine -I . -I "$VulkanInclude" -I ExternalPackages/imgui \
    Scratchpad/VectorCodecTest.cpp Scratchpad/PixelSpaceLinkStub.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/SpatialInterface/InterfacePointerProjection.cpp Engine/SpatialInterface/InterfaceTextProjection.cpp \
    Engine/SpatialInterface/InterfaceScreenSequence.cpp Engine/SpatialInterface/InterfaceVectorCodec.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp Engine/DisplayPresentation/GlyphSpace.cpp \
    -o /tmp/VectorCodecTest || { echo "[VectorCodec] COMPILE FAILED"; exit 1; }
/tmp/VectorCodecTest
S=$?
[ $S -eq 0 ] && echo "[VectorCodec] OK"
exit $S
