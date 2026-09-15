#!/usr/bin/env bash
# P2 gate: a world ray lands on the right figure, at the right place, with the right sign — and a click on the
#    real trial panel drives a real value.
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
    Scratchpad/PointerProjectionTest.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/SpatialInterface/InterfacePointerProjection.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp \
    Projects/Project-Zero/Source/InterfaceTrialSequence.cpp \
    -o /tmp/PointerProjectionTest || { echo "[PointerProjection] COMPILE FAILED"; exit 1; }
/tmp/PointerProjectionTest
S=$?
[ $S -eq 0 ] && echo "[PointerProjection] OK"
exit $S
