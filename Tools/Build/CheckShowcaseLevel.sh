#!/usr/bin/env bash
# Showcase level gate. Builds ShowcaseStructure against a minimal Vulkan type stub (the structure needs only the
#    TriangleIndex/Vector3 declarations that ride SwapchainExchange.h) and a SceneCodec::Encode stub, then runs the
#    checks in Gates/ShowcaseLevelGate.cpp. No Vulkan SDK, no submodules and no GPU required — the point is to catch a
#    level that cannot be lit long before anyone launches the renderer to find out.
#
#    usage: bash Tools/Build/CheckShowcaseLevel.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

Compiler="${CXX:-g++}"
if ! command -v "$Compiler" >/dev/null 2>&1; then
    echo "[Showcase] SKIPPED — no C++ compiler ($Compiler) on PATH"
    exit 0
fi

Work="$(mktemp -d /tmp/ShowcaseGate.XXXXXX)"
trap 'rm -rf "$Work"' EXIT
mkdir -p "$Work/stub/vulkan"

# The structure pulls SwapchainExchange.h for TriangleIndex/Vector3/TriangleSpanRecord, which transitively includes
#    <vulkan/vulkan.h> for handle types it never uses here. These opaque declarations are enough to parse it.
cat > "$Work/stub/vulkan/vulkan.h" <<'STUB'
#pragma once
#include <cstdint>
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T*         VkDevice;
typedef struct VkQueue_T*          VkQueue;
typedef struct VkCommandPool_T*    VkCommandPool;
typedef struct VkCommandBuffer_T*  VkCommandBuffer;
typedef struct VkImage_T*          VkImage;
typedef struct VkBuffer_T*         VkBuffer;
typedef struct VkInstance_T*       VkInstance;
STUB

if ! "$Compiler" -std=c++20 -I. -I"$Work/stub" \
        Tools/Build/Gates/ShowcaseLevelGate.cpp \
        Tools/Build/Gates/ShowcaseLevelGateCodecStub.cpp \
        Engine/ContentInterchange/ShowcaseStructure.cpp \
        Engine/DeviceExchange/OrientationClassifier.cpp \
        -o "$Work/gate" 2> "$Work/build.log"; then
    echo "[Showcase] RED — the gate did not build"
    sed -n '1,40p' "$Work/build.log"
    exit 1
fi

"$Work/gate"
Status=$?
if [ $Status -eq 0 ]; then echo "[Showcase] GREEN — the default level can be lit"
else                       echo "[Showcase] RED — the default level is missing something the renderer needs"; fi
exit $Status
