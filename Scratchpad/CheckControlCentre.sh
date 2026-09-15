#!/usr/bin/env bash
# Headless interaction proof for the Control Centre — compiles Engine/DisplayPresentation, drives the shade
#    through the dashboard, the settings hub, and every settings page the way the engine does (notch drags,
#    tile taps, the render-scale pill, hub rows, tabs, sliders, dropdowns, switches, dialogues, footer pills),
#    and gates the run: each scripted drag must move its draft value and each section must land on its page.
#    Snapshots land in Diagnostics/ControlCentre_*.png for eyeball review; the gates are what fail the build.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
Fail=0
mkdir -p Diagnostics

echo "[ControlCentre] seating the vendor patches, as every build does"
if ! python3 Scripts/ApplyImGuiPatches.py >/tmp/ControlCentre.patches 2>&1; then
    echo "  PATCH SEATING FAILED"; sed 's/^/    /' /tmp/ControlCentre.patches | head -25; exit 1
fi
if ! python3 Scripts/ApplyImGuiPatches.py --verify >/tmp/ControlCentre.verify 2>&1; then
    echo "  PATCH VERIFY FAILED"; sed 's/^/    /' /tmp/ControlCentre.verify | head -25; exit 1
fi

[ -f "ExternalPackages/tomlpp/include/toml++/toml.hpp" ] || git clone --depth 1 -q https://github.com/marzer/tomlplusplus.git ExternalPackages/tomlpp

echo "[ControlCentre] compiling Engine/DisplayPresentation (headless: no Vulkan, no GLFW)"
Binary="$(mktemp -u /tmp/ControlCentreProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_DEVELOPMENT \
     -I ExternalPackages/imgui -I Engine/DisplayPresentation -I ExternalPackages/tomlpp/include -I ExternalPackages/stb -I Scratchpad \
     Scratchpad/GenerateControlCentreProof.cpp \
     Engine/DisplayPresentation/ControlCentreHost.cpp \
     Engine/DisplayPresentation/PixelSpace.cpp \
     Engine/DisplayPresentation/MotionIntegrator.cpp \
     Engine/DisplayPresentation/ThemeStructure.cpp \
     Engine/DisplayPresentation/ControlKit.cpp \
     Engine/DisplayPresentation/AppearanceInspector.cpp \
     Engine/DisplayPresentation/ConfigurationInspector.cpp \
     Engine/DisplayPresentation/DialogueHost.cpp \
     Engine/DisplayPresentation/FidelityClassifier.cpp \
     Engine/DisplayPresentation/VectorCodec.cpp \
     Engine/DisplayPresentation/NotificationQueue.cpp \
     Engine/DisplayPresentation/TelemetryMetrics.cpp \
     Engine/DisplayPresentation/TypefaceRegistry.cpp \
     Engine/DisplayPresentation/GlyphSpace.cpp \
     Engine/DisplayPresentation/FontCodec.cpp \
     Engine/DisplayPresentation/ConfigurationRegistry.cpp \
     Engine/DeviceExchange/InputExchange.cpp \
     ExternalPackages/imgui/imgui.cpp \
     ExternalPackages/imgui/imgui_draw.cpp \
     ExternalPackages/imgui/imgui_tables.cpp \
     ExternalPackages/imgui/imgui_widgets.cpp \
     -o "$Binary" 2>/tmp/ControlCentre.build; then
    echo "  COMPILE FAILED"; grep -iE "error" /tmp/ControlCentre.build | head -25; exit 1
fi

echo "[ControlCentre] driving the shade (dashboard, hub, pages, dialogues)"
"$Binary" > /tmp/ControlCentre.run 2>&1 || Fail=1
rm -f "$Binary"
grep -E "FAIL|FAILURE" /tmp/ControlCentre.run && Fail=1
grep -E "\[24\]|\[32\]|\[41\]|\[52\]|\[58\]|\[65\]" /tmp/ControlCentre.run | sed 's/^/  /'

echo
if [ "$Fail" -ne 0 ]; then
    echo "[ControlCentre] PROOF FAILED — see /tmp/ControlCentre.run and Diagnostics/ControlCentre_*.png"
    exit 1
fi
echo "[ControlCentre] the shade answers every drag on the right page"
