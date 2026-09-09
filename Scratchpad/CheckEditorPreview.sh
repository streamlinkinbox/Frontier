#!/usr/bin/env bash
# Headless editor preview — the development editor over the LIVE Cornell level, with the viewport traced on the
#    CPU through the renderer's own traversal. One sheet out (Diagnostics/EditorPreview.png). No Vulkan, no GLFW.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
mkdir -p Diagnostics

echo "[EditorPreview] seating the vendor patches, as every build does"
if ! python3 Scripts/ApplyImGuiPatches.py >/tmp/EditorPreview.patches 2>&1; then
    echo "  PATCH SEATING FAILED"; sed 's/^/    /' /tmp/EditorPreview.patches | head -25; exit 1
fi

Cg="${CGLTF:-/tmp/cg}"; Ufbx="${UFBX:-/tmp/ufbxsrc}"; Stb="${STB:-/tmp/stbsrc}"; Bvh="${TINYBVH:-/tmp/tinybvh}"
Vkh="${VKH:-/tmp/vkh/include}"
[ -f "$Cg/cgltf.h"   ] || git clone --depth 1 -q https://github.com/jkuhlmann/cgltf.git "$Cg"
[ -f "$Ufbx/ufbx.h"  ] || git clone --depth 1 -q https://github.com/ufbx/ufbx.git "$Ufbx"
[ -f "$Stb/stb_image.h" ] || git clone --depth 1 -q https://github.com/nothings/stb.git "$Stb"
[ -f "$Bvh/tiny_bvh.h" ] || git clone --depth 1 -q https://github.com/jbikker/tinybvh.git "$Bvh"
[ -f "$Vkh/vulkan/vulkan.h" ] || git clone --depth 1 -q https://github.com/KhronosGroup/Vulkan-Headers.git "$(dirname "$Vkh")"

echo "[EditorPreview] compiling the preview (headless: no Vulkan, no GLFW)"
Binary="$(mktemp -u /tmp/EditorPreview.XXXXXX)"
if ! g++ -std=c++20 -O2 -msse4.2 -mavx2 -DFRONTIER_DEVELOPMENT \
     -I ExternalPackages/imgui -I Engine/Editor -I Scratchpad -I . -I Engine \
     -I Projects/Project-Zero/Source -I "$Cg" -I "$Ufbx" -I "$Stb" -I "$Bvh" -I "$Vkh" \
     Scratchpad/EditorPreview.cpp \
     Engine/Editor/EditorHost.cpp \
     Engine/Editor/ControlPanel.cpp \
     Engine/Editor/OutlinerPanel.cpp \
     Engine/Editor/ViewportPanel.cpp \
     Engine/Editor/InspectorPanel.cpp \
     Engine/Editor/ControlCentrePanel.cpp \
     Engine/GeometricRaster/VisibilityRaster.cpp \
     ExternalPackages/imgui/imgui.cpp \
     ExternalPackages/imgui/imgui_draw.cpp \
     ExternalPackages/imgui/imgui_tables.cpp \
     ExternalPackages/imgui/imgui_widgets.cpp \
     Projects/Project-Zero/Source/EditorFeedSequence.cpp \
     Projects/Project-Zero/Source/FlyThroughSolver.cpp \
     Engine/GeometricRaster/CameraProjection.cpp \
     Engine/DeviceExchange/InputExchange.cpp \
     Engine/GeometricRaster/CelestialSolver.cpp \
     Engine/GeometricRaster/TraversalIndex.cpp \
     Engine/ContentInterchange/SceneCodec.cpp \
     Engine/ContentInterchange/MaterialCodec.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     Engine/GeometricRaster/SceneStructure.cpp \
     Engine/GeometricRaster/GeometryStructure.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     -o "$Binary" 2>/tmp/EditorPreview.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/EditorPreview.build | head -25; exit 1
fi
"$Binary" || exit 1
rm -f "$Binary"
echo "[EditorPreview] quarantining the visibility raster (no ray query may appear)"
if grep -nE 'TraceClosest|TraversalIndex|BuildBottomLevel|TraceRay|Intersect' Engine/GeometricRaster/VisibilityRaster.cpp Engine/GeometricRaster/VisibilityRaster.h; then
    echo "  >>> A RAY QUERY LEAKED INTO THE NO-RAY PATH"; exit 1
fi
echo "[EditorPreview] preview OK"
