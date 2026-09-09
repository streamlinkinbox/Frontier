#!/usr/bin/env bash
# D5 gate: a refitted acceleration structure answers rays exactly as a rebuilt one would, so moving bodies cast
#    shadows and reflections consistent with where they are drawn.
#
# -mavx2 is a property of this TEST: tinybvh's CWBVH *CPU* traversal (the reference trace) hard-requires AVX. The
#    shipped renderer traverses on the GPU and still builds with -Isa SSE2 on a pre-AVX host.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for Candidate in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$Candidate/vulkan/vulkan.h" ] && VulkanInclude="$Candidate" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }

if ! grep -qm1 'avx2' /proc/cpuinfo 2>/dev/null; then
    echo "[TraversalRefit] host has no AVX2 - the CPU reference trace cannot run here, skipping"
    exit 0
fi

g++ -std=c++20 -O2 -mavx2 -mfma -I Engine -I "$VulkanInclude" -I ExternalPackages/tinybvh \
    Scratchpad/TraversalRefitTest.cpp Engine/GeometricRaster/TraversalIndex.cpp \
    -o /tmp/TraversalRefitTest || { echo "[TraversalRefit] COMPILE FAILED"; exit 1; }

/tmp/TraversalRefitTest
Status=$?
[ $Status -eq 0 ] && echo "[TraversalRefit] OK"
exit $Status
