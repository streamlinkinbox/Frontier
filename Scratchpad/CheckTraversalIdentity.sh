#!/usr/bin/env bash
# D1 gate: the BLAS/TLAS split must not perturb the acceleration structure for a single identity instance.
#
# Built with -mavx2 because tinybvh's CWBVH *CPU* traversal (used only by the reference trace in this harness)
#    hard-requires AVX and aborts otherwise. That is a property of this test, NOT of the shipped renderer: the
#    GPU kernel traverses the blobs in Slang and the engine still builds fine on a pre-AVX host such as the
#    i3-2120. If the host lacks AVX the construction half still runs; only the ray-agreement half is skipped.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Simd="-mavx2 -mfma"
if ! grep -qm1 ' avx2 ' /proc/cpuinfo 2>/dev/null && ! grep -qm1 'avx2' /proc/cpuinfo 2>/dev/null; then
    echo "[Traversal] host has no AVX2 - construction checks only, reference trace will abort (expected)"
    Simd=""
fi

# TraversalIndex.cpp reaches RayTracingCapabilitySet.h, which includes <vulkan/vulkan.h>. Only the declarations
#    are needed (nothing Vulkan is called here), so headers alone suffice; fetch them if the SDK is absent.
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for Candidate in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$Candidate/vulkan/vulkan.h" ] && VulkanInclude="$Candidate" && break
    done
fi
if [ -z "$VulkanInclude" ]; then
    echo "[Traversal] fetching Vulkan headers to /tmp/vkh (declarations only)"
    git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1
    VulkanInclude=/tmp/vkh/include
fi

g++ -std=c++20 -O2 $Simd -I Engine -I ExternalPackages/tinybvh -I "$VulkanInclude" \
    Scratchpad/TraversalIdentityTest.cpp Engine/GeometricRaster/TraversalIndex.cpp \
    -o /tmp/TraversalIdentityTest || { echo "[Traversal] COMPILE FAILED"; exit 1; }

/tmp/TraversalIdentityTest
Status=$?
[ $Status -ne 0 ] && exit $Status

# ── The same gate against real decoded scenes, including the R0 bit-identity reference ──────────────────────
Scenes=""
for S in Projects/Project-Zero/Content/Scenes/CornellBox.gltf Projects/Project-Zero/Content/Scenes/Showroom.gltf; do
    [ -f "$S" ] && Scenes="$Scenes $S"
done
if [ -n "$Scenes" ] && [ -f ExternalPackages/cgltf/cgltf.h ] && [ -f ExternalPackages/stb/stb_image.h ]; then
    g++ -std=c++20 -O2 $Simd -I "$VulkanInclude" -I . -I ExternalPackages/tinybvh \
        -I ExternalPackages/cgltf -I ExternalPackages/ufbx -I ExternalPackages/stb \
        Scratchpad/SceneTraversalIdentityTest.cpp Engine/GeometricRaster/TraversalIndex.cpp \
        Engine/ContentInterchange/SceneCodec.cpp Engine/ContentInterchange/MaterialCodec.cpp \
        Engine/ContentInterchange/MaterialIndex.cpp Engine/ContentInterchange/TextureIndex.cpp \
        Engine/GeometricRaster/SceneStructure.cpp Engine/GeometricRaster/GeometryStructure.cpp \
        Engine/DeviceExchange/OrientationClassifier.cpp -o /tmp/SceneTraversalIdentityTest \
        || { echo "[Traversal] scene harness COMPILE FAILED"; exit 1; }
    for S in $Scenes; do
        /tmp/SceneTraversalIdentityTest "$S" || { echo "[Traversal] $S DIVERGED"; exit 1; }
    done
else
    echo "[Traversal] scene check skipped (submodules or scene files absent)"
fi

echo "[Traversal] OK"
exit 0
