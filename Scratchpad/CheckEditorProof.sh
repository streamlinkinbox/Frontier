#!/usr/bin/env bash
# Headless visual proof for the development editor — compiles the patched vendor plus Engine/Editor, drives ten
#    ticks through the engine's tick order, rasterises the last tick with a dependency-free CPU rasteriser, and
#    gates the PNG: three occupied columns, a trapezoid slant on both tab edges, titled strips, the seated theme
#    tints, and the four faces the theme seats.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
Fail=0
mkdir -p Diagnostics

echo "[EditorProof] seating the vendor patches, as every build does"
if ! python3 Scripts/ApplyImGuiPatches.py >/tmp/EditorProof.patches 2>&1; then
    echo "  PATCH SEATING FAILED"; sed 's/^/    /' /tmp/EditorProof.patches | head -25; exit 1
fi
if ! python3 Scripts/ApplyImGuiPatches.py --verify >/tmp/EditorProof.verify 2>&1; then
    echo "  PATCH VERIFY FAILED"; sed 's/^/    /' /tmp/EditorProof.verify | head -25; exit 1
fi

echo "[EditorProof] compiling the patched vendor + Engine/Editor (headless: no Vulkan, no GLFW)"
Binary="$(mktemp -u /tmp/EditorProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_DEVELOPMENT \
     -I ExternalPackages/imgui -I Engine/Editor -I Scratchpad \
     Scratchpad/EditorProof.cpp \
     Engine/Editor/EditorHost.cpp \
     Engine/Editor/EditorKit.cpp \
     Engine/Editor/OutlinerPanel.cpp \
     Engine/Editor/ViewportPanel.cpp \
     Engine/Editor/InspectorPanel.cpp \
     ExternalPackages/imgui/imgui.cpp \
     ExternalPackages/imgui/imgui_draw.cpp \
     ExternalPackages/imgui/imgui_tables.cpp \
     ExternalPackages/imgui/imgui_widgets.cpp \
     -o "$Binary" 2>/tmp/EditorProof.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/EditorProof.build | head -25; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

echo
echo "[EditorProof] engine ⇄ project seam"
# The engine must never learn game semantics. A single include of Projects/ here would make the editor
# unusable by any other project and break CLAUDE.md's seam rule.
if grep -rn '#include.*Projects/' Engine/Editor/ >/dev/null 2>&1; then
    echo "  the editor includes from Projects/ — the engine must not know game semantics"; Fail=1
fi

echo
echo "[EditorProof] typeface archives the theme seats"
for Face in EngineContent/FontArchives/FiraSans/FiraSans-Regular.ttf \
            EngineContent/FontArchives/JetBrainsMono/JetBrainsMono-Regular.ttf; do
    if [[ ! -s "$Face" ]]; then echo "  MISSING $Face"; Fail=1; fi
done

# Banned vocabulary (CLAUDE.md §2). ImGui's own API names its dock columns 'nodes', its draw-store members
#    'buffers', and its scroll panes 'children'; those lines are the vendor's vocabulary, not ours, and are
#    excluded from the scan.
Bad="$(grep -nE '\b(Node|Tree|Item|Entity|Element|Object|Frame|Manager|Controller|Handle|State|Value|Flag|Data|Buffer|Cache|Region|Array|Map|Model|Source|Parent|Child|Mesh|Grid|Filter|Stage|Pass|Ordinal)\b' \
    Engine/Editor/EditorRecord.h Engine/Editor/EditorKit.h Engine/Editor/EditorKit.cpp \
    Engine/Editor/EditorHost.h Engine/Editor/EditorHost.cpp \
    Engine/Editor/OutlinerPanel.h Engine/Editor/OutlinerPanel.cpp \
    Engine/Editor/ViewportPanel.h Engine/Editor/ViewportPanel.cpp \
    Engine/Editor/InspectorPanel.h Engine/Editor/InspectorPanel.cpp \
    Scratchpad/EditorProof.cpp | grep -vE 'Im[A-Z]' || true)"
if [[ -n "$Bad" ]]; then
    echo "  forbidden words in the editor's own vocabulary:"; echo "$Bad" | sed 's/^/    /'; Fail=1
fi

# No heap traffic while drawing: the panels must not allocate.
if grep -nE '(push_back|emplace_back|resize|reserve|new )' Engine/Editor/*.cpp | grep -q .; then
    echo "  a panel appears to allocate — the tick must not touch the heap"; Fail=1
fi

echo
if [[ ! -s Diagnostics/EditorProof_Tabs.png ]]; then echo "  MISSING Diagnostics/EditorProof_Tabs.png"; Fail=1; else echo "  wrote Diagnostics/EditorProof_Tabs.png"; fi

if (( Fail )); then echo "  >>> EDITOR PROOF FAILED"; else echo "  >>> the editor agrees with its caption"; fi
exit "$Fail"
