#!/usr/bin/env bash
# Gate: the world-space panel drives the audio transport — clicking the bar changes what a listener hears.
# Uses the null driver, so no sound card is required and CI exercises the same relay and integrator.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
VulkanInclude="${VULKAN_INCLUDE:-}"
if [ -z "$VulkanInclude" ]; then
    for C in /usr/include /tmp/vkh/include "${VULKAN_SDK:-/nonexistent}/include"; do
        [ -f "$C/vulkan/vulkan.h" ] && VulkanInclude="$C" && break
    done
fi
[ -z "$VulkanInclude" ] && { git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vkh >/dev/null 2>&1; VulkanInclude=/tmp/vkh/include; }
g++ -std=c++20 -O2 -pthread -I Engine -I . -I "$VulkanInclude" \
    -I Projects/Project-Dyno/Source -I ExternalPackages/miniaudio \
    Scratchpad/InterfaceAudioTest.cpp \
    Projects/Project-Zero/Source/InterfaceAudioSequence.cpp \
    Projects/Project-Zero/Source/InterfaceTrialSequence.cpp \
    Projects/Project-Dyno/Source/CrankClickIntegrator.cpp Projects/Project-Dyno/Source/DynoSequence.cpp \
    Engine/PlatformInterchange/AudioExchange.cpp Engine/PlatformInterchange/MiniaudioTranslation.cpp \
    Engine/PlatformInterchange/WaveCodec.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/SpatialInterface/InterfacePointerProjection.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp \
    -o /tmp/InterfaceAudioTest -ldl || { echo "[InterfaceAudio] COMPILE FAILED"; exit 1; }
/tmp/InterfaceAudioTest
S=$?
[ $S -eq 0 ] && echo "[InterfaceAudio] OK"
exit $S
