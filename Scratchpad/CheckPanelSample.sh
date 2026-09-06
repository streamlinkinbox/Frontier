#!/usr/bin/env bash
# High-tier gate: sampling the panel across its face reproduces its LAYOUT, agrees with the Low-tier average, and
#    compiles as real GLSL — the C++ shim alone would happily accept code glslang rejects.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }

sed -E 's/\.(xyz|xy|yz|xz|zw)\b([^(])/.\1()\2/g' Engine/Shaders/InterfaceSignedDistance.slang \
    > /tmp/InterfaceSignedDistance.port.inc
mkdir -p Diagnostics

Includes="-I Scratchpad -I Engine -I . -I $VulkanInclude -I ExternalPackages/cgltf -I ExternalPackages/stb -I ExternalPackages/imgui"

g++ -std=c++20 -O2 $Includes Scratchpad/PanelSampleTest.cpp Scratchpad/PixelSpaceLinkStub.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/SpatialInterface/InterfacePointerProjection.cpp Engine/SpatialInterface/InterfaceTextProjection.cpp \
    Engine/SpatialInterface/InterfaceScreenSequence.cpp Engine/SpatialInterface/InterfaceVectorCodec.cpp \
    Engine/SpatialInterface/InterfaceLightProjection.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp Engine/DisplayPresentation/GlyphSpace.cpp \
    Engine/GeometricRaster/SceneStructure.cpp Engine/GeometricRaster/GeometryStructure.cpp \
    Engine/ContentInterchange/MaterialIndex.cpp Engine/DeviceExchange/OrientationClassifier.cpp \
    -o /tmp/PanelSampleTest || { echo "[PanelSample] COMPILE FAILED"; exit 1; }
/tmp/PanelSampleTest || exit 1

g++ -std=c++20 -O2 $Includes Scratchpad/TrialPanelSampleTest.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp Projects/Project-Zero/Source/InterfaceTrialSequence.cpp \
    -o /tmp/TrialPanelSampleTest || { echo "[PanelSample] trial harness COMPILE FAILED"; exit 1; }
/tmp/TrialPanelSampleTest || exit 1

# The C++ shim is permissive where GLSL is not. Compile the header the way the kernel will actually see it.
Stage=$(mktemp -d); trap 'rm -rf "$Stage"' EXIT
mkdir -p "$Stage/Shaders"; cp Engine/Shaders/*.slang "$Stage/Shaders/"
cat > "$Stage/Shaders/PanelProbe.comp" <<'PROBE'
#version 460
#extension GL_GOOGLE_include_directive : require
layout(local_size_x = 8, local_size_y = 8) in;
#include "Shaders/InterfaceRecords.slang"
#include "Shaders/InterfaceSignedDistance.slang"
layout(std430, binding = 0) readonly buffer PanelExtent { InterfaceInstanceFigure InterfacePanelFigures[]; };
#include "Shaders/InterfacePanelSample.slang"
layout(std430, binding = 1) writeonly buffer Out { vec4 Result[]; };
void main()
{
    PanelSample S = SampleInterfacePanel(vec2(0.5), 0.25, 0.16, 14u, 64u, 0.002);
    Result[0] = vec4(S.Emission, S.Coverage);
}
PROBE
Glslang="${1:-$(command -v glslang || command -v glslangValidator || echo /tmp/gl/build/StandAlone/glslang)}"
if [ -x "$Glslang" ]; then
    "$Glslang" -V --target-env vulkan1.2 -S comp -I"$Stage" "$Stage/Shaders/PanelProbe.comp" \
        -o "$Stage/probe.spv" >/dev/null || { echo "[PanelSample] SHADER COMPILE FAILED"; exit 1; }
    echo "[PanelSample] sampler compiles to SPIR-V"
else
    echo "[PanelSample] glslang absent - SPIR-V check skipped"
fi

echo "[PanelSample] OK"
