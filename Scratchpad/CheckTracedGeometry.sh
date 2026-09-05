#!/usr/bin/env bash
# D5 gate: traced geometry follows the rigid bodies.
# -mavx matches how libJolt.a is built (Scripts/BuildJolt.sh); RegisterTypes() aborts on an ISA mismatch.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for Candidate in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$Candidate/vulkan/vulkan.h" ] && VulkanInclude="$Candidate" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }

JoltLib="ExternalPackages/jolt/lib/Release/libJolt.a"
[ -f "$JoltLib" ] || { echo "[TracedGeometry] building Jolt first"; bash Scripts/BuildJolt.sh >/dev/null 2>&1 || exit 1; }

g++ -std=c++20 -O2 -mavx -mpopcnt -mfpmath=sse -pthread -DNDEBUG \
    -I . -I "$VulkanInclude" -I ExternalPackages/jolt -I ExternalPackages/tinybvh \
    -I ExternalPackages/cgltf -I ExternalPackages/ufbx -I ExternalPackages/stb \
    -I ExternalPackages/imgui -I ExternalPackages/glfw/include -I ExternalPackages/thorvg/inc \
    Scratchpad/PhysicsTracedGeometryTest.cpp \
    Projects/Project-Zero/Source/PhysicsInstanceSequence.cpp \
    Projects/Project-Zero/Source/ShowroomStructure.cpp \
    Engine/PhysicalDynamics/RigidBodySolver.cpp \
    Engine/GeometricRaster/TraversalIndex.cpp \
    Engine/GeometricRaster/SceneStructure.cpp \
    Engine/GeometricRaster/GeometryStructure.cpp \
    Engine/ContentInterchange/SceneCodec.cpp \
    Engine/ContentInterchange/MaterialCodec.cpp \
    Engine/ContentInterchange/MaterialIndex.cpp \
    Engine/ContentInterchange/TextureIndex.cpp \
    Engine/DeviceExchange/OrientationClassifier.cpp \
    "$JoltLib" -o /tmp/PhysicsTracedGeometryTest || { echo "[TracedGeometry] COMPILE FAILED"; exit 1; }

/tmp/PhysicsTracedGeometryTest
Status=$?
[ $Status -eq 0 ] && echo "[TracedGeometry] OK"
exit $Status
