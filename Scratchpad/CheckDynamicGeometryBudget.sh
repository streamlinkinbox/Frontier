#!/usr/bin/env bash
# D6 gate: the per-frame cost of keeping traced geometry in step with physics stays inside its budget.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }
JoltLib="ExternalPackages/jolt/lib/Release/libJolt.a"
[ -f "$JoltLib" ] || { bash Scripts/BuildJolt.sh >/dev/null 2>&1 || exit 1; }
g++ -std=c++20 -O2 -mavx -mpopcnt -mfpmath=sse -pthread -DNDEBUG \
    -I . -I "$VulkanInclude" -I ExternalPackages/jolt -I ExternalPackages/tinybvh \
    -I ExternalPackages/cgltf -I ExternalPackages/ufbx -I ExternalPackages/stb \
    -I ExternalPackages/imgui -I ExternalPackages/glfw/include -I ExternalPackages/thorvg/inc \
    Scratchpad/DynamicGeometryBudget.cpp \
    Projects/Project-Zero/Source/PhysicsInstanceSequence.cpp Projects/Project-Zero/Source/ShowroomStructure.cpp \
    Engine/PhysicalDynamics/RigidBodySolver.cpp Engine/GeometricRaster/TraversalIndex.cpp \
    Engine/GeometricRaster/SceneStructure.cpp Engine/GeometricRaster/GeometryStructure.cpp \
    Engine/ContentInterchange/SceneCodec.cpp Engine/ContentInterchange/MaterialCodec.cpp \
    Engine/ContentInterchange/MaterialIndex.cpp Engine/ContentInterchange/TextureIndex.cpp \
    Engine/DeviceExchange/OrientationClassifier.cpp "$JoltLib" -o /tmp/DynamicGeometryBudget \
    || { echo "[Budget] COMPILE FAILED"; exit 1; }
/tmp/DynamicGeometryBudget
S=$?
[ $S -eq 0 ] && echo "[Budget] OK"
exit $S
