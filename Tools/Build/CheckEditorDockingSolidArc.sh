#!/usr/bin/env bash
# Static proof for the editor docking contract the runtime build needs:
#   [Outliner] [Viewport] [Inspector]
# and for SolidArc using the same outliner panel with its own docked viewport window.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

Fail=0
Pass() { echo "  PASS  $1"; }
FailOne() { echo "  FAIL  $1"; Fail=1; }

EditorHost="Engine/Editor/EditorHost.cpp"
SolidHost="Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.cpp"
Adapter="Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcOutlinerAdapter.cpp"

echo "[EditorDocking] Project-Zero editor layout"
if grep -q 'FrontierEditorDockSpace' "$EditorHost"; then
    Pass "uses the new editor dockspace id, so stale two-column .ini data cannot keep the old layout"
else
    FailOne "FrontierEditorDockSpace is missing"
fi
if grep -q 'ImGuiDir_Left' "$EditorHost" && grep -q 'ImGuiDir_Right' "$EditorHost"; then
    Pass "splits left and right, producing outliner / viewport / inspector columns"
else
    FailOne "left/right dock splits are missing"
fi
if grep -q 'DockBuilderDockWindow("Outliner", Left)' "$EditorHost" \
   && grep -q 'DockBuilderDockWindow("Viewport", Centre)' "$EditorHost" \
   && grep -q 'DockBuilderDockWindow("Inspector", Right)' "$EditorHost"; then
    Pass "docks Outliner left, Viewport centre, Inspector right"
else
    FailOne "dock window seating is not [Outliner][Viewport][Inspector]"
fi
if grep -q 'DockBuilderDockWindow("Inspector", Left)' "$EditorHost"; then
    FailOne "Inspector is still stacked over the Outliner"
else
    Pass "Inspector is no longer stacked over the Outliner"
fi
if grep -q 'PassthruCentralNode' "$EditorHost" || grep -q 'ImGuiWindowFlags_NoBackground' "$EditorHost"; then
    FailOne "viewport can still show as a passthrough/fullscreen backing plate"
else
    Pass "viewport is an opaque docked window, not a fullscreen backing plate"
fi
if grep -q 'AssignSceneBackdrop(false)' Projects/Project-Zero/Source/GameExecution.cpp \
   && grep -q 'SceneBackdrop' Engine/DeviceExchange/SwapchainExchange.h \
   && grep -q 'if (SceneBackdrop)' Engine/DeviceExchange/SwapchainExchange.cpp; then
    Pass "development editor disables the fullscreen scene backdrop, so the render appears only inside the viewport panel"
else
    FailOne "development editor can still blit the scene as a fullscreen backing plate behind docked panels"
fi
if grep -q 'ImGui::Begin(WindowTitle_' Engine/Editor/OutlinerPanel.cpp \
   && grep -q 'ImGui::Begin(WindowTitle_' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'ImGui::Begin(WindowTitle_' Engine/Editor/InspectorPanel.cpp; then
    Pass "OutlinerPanel, ViewportPanel and InspectorPanel can be retitled for SolidArc without duplicating panel code"
else
    FailOne "shared panels do not use the assigned window title"
fi
if grep -q 'Outliner_.AssignFilterCatalog(GameFilters' "$EditorHost" \
   && grep -q '"Lights"' "$EditorHost" \
   && grep -q '"Sky"' "$EditorHost" \
   && grep -q '"Bodies"' "$EditorHost" \
   && grep -q '"Geometry"' "$EditorHost" \
   && grep -q '"Camera"' "$EditorHost"; then
    Pass "Project-Zero owns an explicit game filter catalogue"
else
    FailOne "Project-Zero game filter catalogue is missing"
fi

echo
echo "[EditorDocking] SolidArc editor shell"
if [ -f "$SolidHost" ] && [ -f "$Adapter" ]; then
    Pass "SolidArc editor host and outliner adapter exist"
else
    FailOne "SolidArc editor host or outliner adapter is missing"
fi
if grep -q 'OutlinerPanel.h' Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.h \
   && grep -q 'ViewportPanel.h' Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.h \
   && grep -q 'InspectorPanel.h' Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.h; then
    Pass "SolidArc reuses the engine OutlinerPanel, ViewportPanel and InspectorPanel"
else
    FailOne "SolidArc is not wired to the shared editor panels"
fi
if grep -q 'AssignWindowTitle("SolidArc Outliner")' "$SolidHost" \
   && grep -q 'AssignWindowTitle("SolidArc Viewport")' "$SolidHost" \
   && grep -q 'AssignWindowTitle("SolidArc Inspector")' "$SolidHost"; then
    Pass "SolidArc seats distinct outliner, viewport and inspector window titles"
else
    FailOne "SolidArc window titles are not assigned"
fi
if grep -q 'DockBuilderDockWindow("SolidArc Outliner", Left)' "$SolidHost" \
   && grep -q 'DockBuilderDockWindow("SolidArc Viewport", Centre)' "$SolidHost" \
   && grep -q 'DockBuilderDockWindow("SolidArc Inspector", Right)' "$SolidHost"; then
    Pass "SolidArc docks outliner left, viewport centre and inspector right"
else
    FailOne "SolidArc docking layout is missing or not dedicated"
fi
if grep -q 'PassthruCentralNode' "$SolidHost" || grep -q 'ImGuiWindowFlags_NoBackground' "$SolidHost"; then
    FailOne "SolidArc viewport can still be a passthrough/fullscreen backing plate"
else
    Pass "SolidArc viewport is a docked window, not a fullscreen backing plate"
fi
if grep -q 'Engine/Editor/EditorInstance.h' Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcOutlinerAdapter.h \
   && grep -q 'BuildSolidArcOutliner' "$Adapter" \
   && grep -q 'ApplySolidArcOutlinerVisibility' "$Adapter"; then
    Pass "SolidArc document rows use the shared EditorInstance outliner feed and write visibility back"
else
    FailOne "SolidArc outliner adapter does not use the shared feed/writeback"
fi
if grep -q '"Sketches"' "$Adapter" \
   && grep -q '"Bodies"' "$Adapter" \
   && grep -q '"Surfaces"' "$Adapter" \
   && grep -q '"Construction"' "$Adapter" \
   && grep -q '"Dimensions"' "$Adapter"; then
    Pass "SolidArc outliner exposes CAD folders from the HTML panel instead of game-engine categories"
else
    FailOne "SolidArc outliner CAD folder labels are missing"
fi
if grep -q 'AssignFilterCatalog' "$SolidHost" \
   && grep -q '"Lines"' "$SolidHost" \
   && grep -q '"Profiles"' "$SolidHost" \
   && grep -q '"Bodies"' "$SolidHost" \
   && grep -q '"Surfaces"' "$SolidHost" \
   && grep -q '"Construction"' "$SolidHost" \
   && grep -q '"Dimensions"' "$SolidHost"; then
    Pass "SolidArc uses a CAD-specific filter catalogue instead of the game editor's Lights/Sky/Geometry/Camera vocabulary"
else
    FailOne "SolidArc CAD filter labels are missing"
fi
if grep -q 'kSketchTint' "$Adapter" \
   && grep -q 'kDimensionTint' "$Adapter" \
   && grep -q 'kBodyTint' "$Adapter" \
   && grep -q 'kSurfaceTint' "$Adapter" \
   && grep -q 'kConstructionTint' "$Adapter" \
   && grep -q 'SolidArcOutlinerFilter::Lines' "$Adapter" \
   && grep -q 'SolidArcOutlinerFilter::Profiles' "$Adapter" \
   && grep -q '#4fd8e0' "$Adapter" \
   && grep -q '#ffb454' "$Adapter" \
   && grep -q '#4da3ff' "$Adapter" \
   && grep -q '#b48cff' "$Adapter" \
   && grep -q '#e5d33a' "$Adapter"; then
    Pass "SolidArc outliner seats the web category colour coding for lines, profiles, bodies, surfaces, construction and dimensions"
else
    FailOne "SolidArc outliner category colour coding is missing"
fi
if grep -q 'BuildSolidArcInspectorSheet' "$Adapter" \
   && grep -q '"CAD geometry"' "$Adapter" \
   && grep -q '"Bounds"' "$Adapter" \
   && grep -q '"Parameters"' "$Adapter"; then
    Pass "SolidArc inspector sheet exposes CAD identity, geometry, bounds and parameter groups"
else
    FailOne "SolidArc CAD inspector sheet is missing"
fi
if grep -q 'AssignChrome(ViewportPanelChrome::SolidArcCad)' "$SolidHost" \
   && grep -q 'RecordSolidArcBar' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Construct' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Body 1' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Face 2' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Edge 3' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Vertex 4' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Matcap' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Move G' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Rotate ⇧R' Engine/Editor/ViewportPanel.cpp \
   && grep -q 'Scale S' Engine/Editor/ViewportPanel.cpp; then
    Pass "SolidArc viewport top toolbar matches the live HTML controls: Construct, sub-selection, Matcap and transform gizmos"
else
    FailOne "SolidArc viewport toolbar controls are missing"
fi

ImguiRoot="${IMGUI_INCLUDE_DIR:-}"
if [ -z "$ImguiRoot" ] && [ -f ExternalPackages/imgui/imgui.h ]; then ImguiRoot="ExternalPackages/imgui"; fi
if [ -z "$ImguiRoot" ] && [ -f /tmp/imgui-docking/imgui.h ]; then ImguiRoot="/tmp/imgui-docking"; fi
if [ -n "$ImguiRoot" ]; then
    if ! grep -q 'TabSlant' "$ImguiRoot/imgui.h"; then
        echo "  SKIP  optional ImGui syntax compile — $ImguiRoot is not patched with Frontier tab geometry"
    else
        Compiler="${CXX:-g++}"
        if "$Compiler" -std=c++20 -Wall -Wextra -Wpedantic -Wno-unused-function -DFRONTIER_DEVELOPMENT \
            -I"$ImguiRoot" -I. -IEditor/AuthoringTools/Modelling/SolidArc -IEngine/Editor \
            -fsyntax-only \
            Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.cpp \
            Engine/Editor/ControlPanel.cpp Engine/Editor/OutlinerPanel.cpp Engine/Editor/ViewportPanel.cpp Engine/Editor/InspectorPanel.cpp \
            >/tmp/EditorDockingSolidArc.syntax 2>&1; then
            Pass "SolidArc optional ImGui editor shell syntax-compiles against $ImguiRoot"
        else
            FailOne "SolidArc optional ImGui editor shell does not syntax-compile against $ImguiRoot"
            sed 's/^/        /' /tmp/EditorDockingSolidArc.syntax | head -30
        fi
    fi
else
    echo "  SKIP  optional ImGui syntax compile — set IMGUI_INCLUDE_DIR or populate ExternalPackages/imgui"
fi

echo
if [ "$Fail" -eq 0 ]; then
    echo "[EditorDocking] GREEN — game editor and SolidArc use dedicated docked viewport windows, and SolidArc uses shared outliner/inspector panels with CAD chrome"
    exit 0
fi
echo "[EditorDocking] RED — docking proof failed"
exit 1
