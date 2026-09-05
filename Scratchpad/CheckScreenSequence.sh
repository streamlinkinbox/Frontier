#!/usr/bin/env bash
# P3 gate: screens fade, slide and wipe; transitions land exactly on their endpoints; a moving or retired screen
#    cannot be clicked; and the whole thing still costs one draw and no new GPU field.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }
g++ -std=c++20 -O2 -Wall -Wextra -I Engine -I . -I "$VulkanInclude" \
    Scratchpad/ScreenSequenceTest.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/SpatialInterface/InterfacePointerProjection.cpp Engine/SpatialInterface/InterfaceTextProjection.cpp \
    Engine/SpatialInterface/InterfaceScreenSequence.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp \
    -o /tmp/ScreenSequenceTest || { echo "[ScreenSequence] COMPILE FAILED"; exit 1; }
/tmp/ScreenSequenceTest
S=$?
[ $S -eq 0 ] && echo "[ScreenSequence] OK"
exit $S
