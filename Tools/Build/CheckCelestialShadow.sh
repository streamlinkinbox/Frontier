#!/usr/bin/env bash
#============================================================================================================================================
#                                                        CHECKCELESTIALSHADOW.SH
#============================================================================================================================================
# Builds and runs the sun-shadow gate: with GI off, the shadow stage must place a rasterisation tap for the sun.
#
#    The gate links the SHIPPED VisibilityExchange translation unit, so it is testing the real PlaceShadowTaps and not
#    a copy that could drift from it. That TU includes <vulkan/vulkan.h>; no Vulkan loader is needed at link time
#    because nothing the gate calls issues a Vulkan command, but the HEADERS must be reachable. They are looked for in
#    the same three places the rest of the workbench uses.
#
#    usage: bash Tools/Build/CheckCelestialShadow.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

VULKAN_ROOT=""
for Candidate in ExternalPackages/Vulkan-Headers "${MATERIAL_SCENES_EXT:-}/Vulkan-Headers" "$HOME/.cache/m7/Vulkan-Headers"; do
    if [ -f "$Candidate/include/vulkan/vulkan.h" ]; then VULKAN_ROOT="$Candidate"; break; fi
done
if [ -z "$VULKAN_ROOT" ]; then
    echo "[celestial-shadow] Vulkan headers not found — tried ExternalPackages/, \$MATERIAL_SCENES_EXT and ~/.cache/m7." >&2
    echo "[celestial-shadow]   git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers ~/.cache/m7/Vulkan-Headers" >&2
    exit 2
fi
echo "[celestial-shadow] Vulkan headers: $VULKAN_ROOT"

Stage="$(mktemp -d)"
trap 'rm -rf "$Stage"' EXIT

# The gate drives PlaceShadowTaps only. Every other entry point on the exchange is a Vulkan command the gate never
#    issues, but the shipped TU still REFERENCES those symbols, so the link needs them to resolve. Ignoring them with
#    --unresolved-symbols produces a binary the dynamic loader refuses ("unexpected PLT reloc type"), so instead the
#    undefined vk* symbols are discovered from the object file and emitted as no-op stubs. A stub that is ever called
#    would mean the gate had strayed off the pure path it claims to test, so each one aborts rather than returning.
g++ -std=c++20 -O1 -g -c -I. -I"$VULKAN_ROOT/include" -o "$Stage/VisibilityExchange.o" Engine/DeviceExchange/VisibilityExchange.cpp

{
    echo '#include <cstdio>'
    echo '#include <cstdlib>'
    echo 'extern "C" {'
    nm -u "$Stage/VisibilityExchange.o" | awk '{print $2}' | grep -E '^vk' | sort -u | while read -r Symbol; do
        echo "void ${Symbol}(void) { std::fprintf(stderr, \"[celestial-shadow] ${Symbol} was called - the gate left the pure path\\n\"); std::abort(); }"
    done
    echo '}'
} > "$Stage/VulkanStubs.cpp"

echo "[celestial-shadow] stubbed $(grep -c '^void vk' "$Stage/VulkanStubs.cpp") Vulkan entry points (none may be called)"

g++ -std=c++20 -O1 -g -fno-omit-frame-pointer \
    -I. -I"$VULKAN_ROOT/include" \
    -o "$Stage/CelestialShadowGate" \
    Tools/Build/Gates/CelestialShadowGate.cpp \
    "$Stage/VisibilityExchange.o" \
    Engine/DeviceExchange/OrientationClassifier.cpp \
    "$Stage/VulkanStubs.cpp"

"$Stage/CelestialShadowGate"
