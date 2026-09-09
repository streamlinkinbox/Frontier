#!/usr/bin/env bash
# D3 gate: per-instance transforms are driven correctly, statics are untouched, and PreviousWorld tracks the last
#    frame so motion vectors and ReSTIR reprojection stay valid. Runs without a GPU and without physics — the point
#    is to rule the transform maths out before D4 connects Jolt.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for Candidate in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$Candidate/vulkan/vulkan.h" ] && VulkanInclude="$Candidate" && break
    done
fi
if [ -z "$VulkanInclude" ]; then
    git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1
    VulkanInclude=/tmp/vkh/include
fi

g++ -std=c++20 -O2 -Wall -Wextra -I . -I "$VulkanInclude" \
    Scratchpad/InstanceMotionTest.cpp Projects/Project-Zero/Source/InstanceMotionSequence.cpp \
    -o /tmp/InstanceMotionTest || { echo "[InstanceMotion] COMPILE FAILED"; exit 1; }

/tmp/InstanceMotionTest
Status=$?
[ $Status -eq 0 ] && echo "[InstanceMotion] OK"
exit $Status
