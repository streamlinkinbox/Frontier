#!/usr/bin/env bash
# D4 gate: real Jolt bodies drive real instance transforms. Everything the renderer would upload, minus the GPU.
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
[ -f "$JoltLib" ] || { echo "[PhysicsInstances] building Jolt first"; bash Scripts/BuildJolt.sh >/dev/null 2>&1 || { echo "  Jolt build FAILED"; exit 1; }; }

# -mavx must match how libJolt.a was built: Jolt derives JPH_USE_* from the compiler macros and RegisterTypes()
#    aborts on a mismatch. Scripts/BuildJolt.sh uses -mavx, so this does too.
g++ -std=c++20 -O2 -mavx -mpopcnt -mfpmath=sse -pthread -DNDEBUG \
    -I . -I "$VulkanInclude" -I ExternalPackages/jolt \
    -I ExternalPackages/cgltf -I ExternalPackages/ufbx -I ExternalPackages/stb \
    Scratchpad/PhysicsInstanceTest.cpp \
    Projects/Project-Zero/Source/PhysicsInstanceSequence.cpp \
    Projects/Project-Zero/Source/ShowroomStructure.cpp \
    Engine/PhysicalDynamics/RigidBodySolver.cpp \
    Engine/GeometricRaster/SceneStructure.cpp \
    Engine/GeometricRaster/GeometryStructure.cpp \
    Engine/DeviceExchange/OrientationClassifier.cpp \
    Engine/ContentInterchange/MaterialIndex.cpp \
    Engine/ContentInterchange/SceneCodec.cpp \
    Engine/ContentInterchange/MaterialCodec.cpp \
    Engine/ContentInterchange/TextureIndex.cpp \
    "$JoltLib" -o /tmp/PhysicsInstanceTest || { echo "[PhysicsInstances] COMPILE FAILED"; exit 1; }

/tmp/PhysicsInstanceTest
Status=$?
[ $Status -eq 0 ] && echo "[PhysicsInstances] OK"
exit $Status
