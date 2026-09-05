#!/usr/bin/env bash
# P1 gate: stroke-font text renders, lays out correctly, and still costs exactly one draw.
# Compiles the SAME glyph code the fragment shader runs (GlslShim.h maps Slang to C++), so the proof and the
#    shader cannot diverge. Also writes Diagnostics/SpatialInterface_P1_Text.png for inspection by eye — pixel
#    counts prove ink exists, not that it spells anything.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }

# GLSL swizzles are member functions in the shim, so .xy becomes .xy().
sed -E 's/\.(xyz|xy|yz|xz|zw)\b([^(])/.\1()\2/g' Engine/Shaders/InterfaceSignedDistance.slang \
    > /tmp/InterfaceSignedDistance.port.inc

mkdir -p Diagnostics
g++ -std=c++20 -O2 -I Scratchpad -I Engine -I . -I "$VulkanInclude" \
    Scratchpad/TextProjectionTest.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/SpatialInterface/InterfacePointerProjection.cpp Engine/SpatialInterface/InterfaceTextProjection.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp \
    -o /tmp/TextProjectionTest || { echo "[TextProjection] COMPILE FAILED"; exit 1; }

/tmp/TextProjectionTest || exit 1

# The C++ harness is permissive where GLSL is not: (void)x casts and unused locals compile fine as C++ and are
#    rejected by glslang. Shipping a glyph table that only builds in the proof would be worse than no proof, so
#    the real shader compile is part of THIS gate rather than a separate one.
if bash Scratchpad/CompileInterfaceShaders.sh >/tmp/TextShaderCompile.log 2>&1; then
    echo "[TextProjection] shader compiles to SPIR-V"
else
    echo "[TextProjection] SHADER COMPILE FAILED:"
    grep -E "ERROR" /tmp/TextShaderCompile.log | head -5
    exit 1
fi

echo "[TextProjection] OK"
