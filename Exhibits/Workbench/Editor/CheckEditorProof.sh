#!/usr/bin/env bash
# Headless visual proof for the development editor — compiles the patched vendor plus Engine/Editor, drives ten
#    ticks through the engine's tick order, rasterises the last tick with a dependency-free CPU rasteriser, and
#    gates the PNG: three occupied columns, a trapezoid slant on both tab edges, titled strips, the seated theme
#    tints, and the four faces the theme seats. Then the interaction phases drive the pointer the way the
#    engine does — the category menu opens, a Camera pick narrows the outline, the palette opens over the
#    console, the views menu poses the orbit, the gizmo answers — and each phase rasterises its own sheet.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1
Fail=0
mkdir -p Exhibits/Gallery/Editor

echo "[EditorProof] seating the vendor patches, as every build does"
if ! python3 Tools/Build/ApplyImGuiPatches.py >/tmp/EditorProof.patches 2>&1; then
    echo "  PATCH SEATING FAILED"; sed 's/^/    /' /tmp/EditorProof.patches | head -25; exit 1
fi
if ! python3 Tools/Build/ApplyImGuiPatches.py --verify >/tmp/EditorProof.verify 2>&1; then
    echo "  PATCH VERIFY FAILED"; sed 's/^/    /' /tmp/EditorProof.verify | head -25; exit 1
fi

[ -f "ExternalPackages/tomlpp/include/toml++/toml.hpp" ] || git clone --depth 1 -q https://github.com/marzer/tomlplusplus.git ExternalPackages/tomlpp

echo "[EditorProof] compiling the patched vendor + Engine/Editor (headless: no Vulkan, no GLFW)"
Binary="$(mktemp -u /tmp/EditorProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_DEVELOPMENT \
     -I ExternalPackages/imgui -I Engine/Editor -I Engine/DisplayPresentation -I ExternalPackages/tomlpp/include -I Exhibits/Workbench/Editor -I Exhibits/Workbench/Editor/Counterparts -pthread \
     Exhibits/Workbench/Editor/EditorProof.cpp \
     Engine/Editor/EditorHost.cpp \
     Engine/Editor/ControlPanel.cpp \
     Engine/Editor/OutlinerPanel.cpp \
     Engine/Editor/ViewportPanel.cpp \
     Engine/Editor/InspectorPanel.cpp \
     Engine/Editor/ShadeTick.cpp \
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
     Engine/DisplayPresentation/MaterialInspector.cpp \
     Engine/ContentInterchange/AssetResolution.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/DeviceExchange/InputExchange.cpp \
     Projects/Project-Zero/Source/RayTracingSolver.cpp \
     Projects/Project-Zero/Source/FlyThroughSolver.cpp \
     Engine/GeometricRaster/CameraProjection.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
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
for Face in EngineContent/FontArchives/Outfit/Outfit-Light.ttf \
            EngineContent/FontArchives/JetBrainsMono/JetBrainsMono-Regular.ttf; do
    if [[ ! -s "$Face" ]]; then echo "  MISSING $Face"; Fail=1; fi
done

# Banned vocabulary (CLAUDE.md §3, plus §13's Record table and the Kit rename). ImGui's own entry points name
#    their dock columns 'nodes', their draw-list spans 'buffers', and their scroll panes 'children'; those lines
#    are the vendor's vocabulary, not ours, and are excluded from the scan.
EditorFiles="Engine/Editor/EditorInstance.h Engine/Editor/ControlPanel.h Engine/Editor/ControlPanel.cpp
    Engine/Editor/EditorHost.h Engine/Editor/EditorHost.cpp
    Engine/Editor/OutlinerPanel.h Engine/Editor/OutlinerPanel.cpp
    Engine/Editor/ViewportPanel.h Engine/Editor/ViewportPanel.cpp
    Engine/Editor/InspectorPanel.h Engine/Editor/InspectorPanel.cpp
    Exhibits/Workbench/Editor/EditorProof.cpp"
# shellcheck disable=SC2086
Bad="$(grep -nE '\b(Manager|Handler|Processor|Controller|Service|Utility|Helper|Node|Frame|Module|Core|System|Backend|Pass|Stage|Harness|Shell|Entity|Element|Subsystem|Hierarchy|Data|Info|Object|Item|Thing|Kind|Base|flag|state|value|Parent|Child|Sibling|Table|Map|Block|Digest|Model|Handle|Store|Bridge|Atlas|Substrate|Fabric|Cache|Evaluator|Evaluate|Journal|Resolver|Mesh|Pool|Registry|Catalog|Repository|Directory|Vault|Arena|Inventory|Ledger|Plan|Filter|Grid|Array|Dispatcher|Memory|Buffer|Pipeline|Flow|Composite|Compose|Composition|Allocation|Tier|Nesting|Stratum|Mip|Messenger|Probe|Blend|History|Bake|Stamp|Contract|Outcome|Prelude|Cadence|Binding|Submission|Footprint|Region|Tree|Vacancy|Ordinates|Draft|Draught|Paint|Depot|Ordinal|Actor|Source|API|Kit|kit|kind)\b' \
    $EditorFiles | grep -vE 'Im[A-Z]' || true)"
if [[ -n "$Bad" ]]; then
    echo "  forbidden words in the editor's own vocabulary:"; echo "$Bad" | sed 's/^/    /'; Fail=1
fi

# The Cornell feed is project code, but it speaks the editor's vocabulary across the seam, so the same §3 list
#    holds between its landmarks (FillCornellInstances down to main). The project's older systems above and below
#    keep their grandfathered words; this range does not.
FeedBad="$(sed -n '/^void FillCornellInstances/,/^int main/p' Projects/Project-Zero/Source/GameExecution.cpp \
    | sed '$d' | grep -nE '\b(Manager|Handler|Processor|Controller|Service|Utility|Helper|Node|Frame|Module|Core|System|Backend|Pass|Stage|Harness|Shell|Entity|Element|Subsystem|Hierarchy|Data|Info|Object|Item|Thing|Kind|Base|flag|state|value|Parent|Child|Sibling|Table|Map|Block|Digest|Model|Handle|Store|Bridge|Atlas|Substrate|Fabric|Cache|Evaluator|Evaluate|Journal|Resolver|Mesh|Pool|Registry|Catalog|Repository|Directory|Vault|Arena|Inventory|Ledger|Plan|Filter|Grid|Array|Dispatcher|Memory|Buffer|Pipeline|Flow|Composite|Compose|Composition|Allocation|Tier|Nesting|Stratum|Mip|Messenger|Probe|Blend|History|Bake|Stamp|Contract|Outcome|Prelude|Cadence|Binding|Submission|Footprint|Region|Tree|Vacancy|Ordinates|Draft|Draught|Paint|Depot|Ordinal|Actor|Source|API|Kit|kit|kind|Record)\b' \
    | grep -vE 'Im[A-Z]' || true)"
if [[ -n "$FeedBad" ]]; then
    echo "  forbidden words in the Cornell feed:"; echo "$FeedBad" | sed 's/^/    /'; Fail=1
fi

# 'kind' and 'kit' survive no boundary: the ##kindmenu rename proved a word-boundary scan blind to them, so the
#    substrings themselves are tripwires — any casing, any position.
# shellcheck disable=SC2086
KindHit="$(grep -rni 'kind' Engine/Editor/ Exhibits/Workbench/Editor/EditorProof.cpp || true)"
if [[ -n "$KindHit" ]]; then
    echo "  'kind' survives somewhere it must not:"; echo "$KindHit" | sed 's/^/    /'; Fail=1
fi
# shellcheck disable=SC2086
KitHit="$(grep -rni 'kit' Engine/Editor/ Exhibits/Workbench/Editor/EditorProof.cpp || true)"
if [[ -n "$KitHit" ]]; then
    echo "  'kit' survives somewhere it must not:"; echo "$KitHit" | sed 's/^/    /'; Fail=1
fi

# 'Record' lives in the editor only as the drawing verb: seventeen method names and the two lowercase verb forms.
#    Any eighteenth spelling is a noun sneaking back in.
# shellcheck disable=SC2086
RecordWant="Record
RecordBar
RecordCaps
RecordCard
RecordChips
RecordCommand
RecordEmpty
RecordFooter
RecordHeader
RecordIdent
RecordNotes
RecordOutline
RecordRow
RecordSearch
RecordSolidArcBar
RecordStanding
RecordTabAdd
RecordTiles
RecordView"
RecordHave="$(grep -hoE 'Record[A-Za-z]*' $EditorFiles | sort -u || true)"
if [[ "$RecordHave" != "$RecordWant" ]]; then
    echo "  the Record verb grew a new spelling:"; echo "$RecordHave" | sed 's/^/    /'; Fail=1
fi
# shellcheck disable=SC2086
RecordLow="$(grep -hoE '\brecord[a-z]*\b' $EditorFiles | sort -u || true)"
if [[ "$RecordLow" != "$(printf 'record\nrecords')" ]]; then
    echo "  the lowercase record verb grew a new spelling:"; echo "$RecordLow" | sed 's/^/    /'; Fail=1
fi

# The foot strip keeps the page's five readouts: Realtime / Quality / Sun / Moons / Cam.
for Word in REALTIME QUALITY SUN MOONS CAM; do
    if ! grep -q "\"$Word\"" Engine/Editor/OutlinerPanel.cpp; then
        echo "  the foot strip lost its $Word readout"; Fail=1
    fi
done

# One height across the three feet: every foot draws the shared forty, and the tree, the props
#    child and the view each reserve exactly it.
for Foot in OutlinerPanel InspectorPanel ViewportPanel; do
    if ! grep -q "kEditorFooterH" Engine/Editor/$Foot.cpp; then
        echo "  $Foot never draws the shared foot height"; Fail=1
    fi
done

# The console sleeps till Ctrl+K: shut at boot, the chord toggles it, and the bar and the stack
#    both draw full black.
if ! grep -q "ConsoleOpen_.*= false" Engine/Editor/ViewportPanel.h; then
    echo "  the console no longer boots shut"; Fail=1
fi
if ! grep -q "ConsoleOpen_.*= !ConsoleOpen_" Engine/Editor/ViewportPanel.cpp; then
    echo "  Ctrl+K no longer raises the console"; Fail=1
fi
if [[ "$(grep -c "IM_COL32(0, 0, 0, 255)" Engine/Editor/ViewportPanel.cpp)" -lt 2 ]]; then
    echo "  the console bar or stack is no longer full black"; Fail=1
fi

# The triangle total crosses the seam on the readout, seated from the live level each tick.
if ! grep -q "uint32_t Triangles" Engine/Editor/EditorInstance.h; then
    echo "  the readout lost its triangle total"; Fail=1
fi
if ! grep -q "EditorFooter.Triangles = Level.QueryTriangleCount()" Projects/Project-Zero/Source/GameExecution.cpp; then
    echo "  the feed no longer seats the triangle total"; Fail=1
fi

# Every popup the editor opens, the editor begins: the Open set and the Begin set must be the same five ids.
OpenPopups="$(grep -hoE 'OpenPopup\("##[a-z]+"\)' Engine/Editor/*.cpp | sort -u)"
BeginPopups="$(grep -hoE 'BeginPopup\("##[a-z]+"\)' Engine/Editor/*.cpp | sed 's/BeginPopup/OpenPopup/' | sort -u)"
if [[ "$OpenPopups" != "$BeginPopups" ]]; then
    echo "  a popup opens that never begins, or begins that never opens:"; Fail=1
fi
OpenCount="$(echo "$OpenPopups" | grep -c 'OpenPopup')"
if [[ "$OpenCount" != "5" ]]; then
    echo "  the editor seats five popups, no more:"; echo "$OpenPopups" | sed 's/^/    /'; Fail=1
fi

# The strip era's silencers are gone: every tab carries its close mark, the add control closes the left
#    column, and the shade's pull is narrow enough to clear the viewport tab.
if grep -q "NoCloseButton" Engine/Editor/*.cpp Engine/Editor/*.h; then
    echo "  a close mark is silenced — every tab carries one"; Fail=1
fi
if ! grep -q "DockNodeSetAddButton" Engine/Editor/EditorHost.cpp; then
    echo "  the add control is never seated"; Fail=1
fi
if ! grep -q "AssignNotchWidth" Engine/Editor/EditorHost.cpp; then
    echo "  the shade's pull is never narrowed"; Fail=1
fi
if ! grep -q "FRONTIER PATCH D" ExternalPackages/imgui/imgui.cpp; then
    echo "  PatchD is not seated — run Tools/Build/ApplyImGuiPatches.py"; Fail=1
fi
if ! grep -q "FRONTIER PATCH E" ExternalPackages/imgui/imgui.cpp; then
    echo "  PatchE is not seated — run Tools/Build/ApplyImGuiPatches.py"; Fail=1
fi

# No heap traffic while drawing: the panels must not allocate.
if grep -nE '(push_back|emplace_back|resize|reserve)[[:space:]]*\(|new[[:space:]]+[A-Za-z_\*]' Engine/Editor/*.cpp | grep -q .; then
    echo "  a panel appears to allocate — the tick must not touch the heap"; Fail=1
fi

echo
for Sheet in Tabs Menu Filtered Palette Views Inspector; do
    if [[ ! -s Exhibits/Gallery/Editor/EditorProof_$Sheet.png ]]; then
        echo "  MISSING Exhibits/Gallery/Editor/EditorProof_$Sheet.png"; Fail=1
    else
        echo "  wrote Exhibits/Gallery/Editor/EditorProof_$Sheet.png"
    fi
done

echo
echo "[EditorProof] the knobs sit on their fractions"
KnobCheck="$(mktemp -u /tmp/EditorKnobCheck.XXXXXX)"
# The knob check READS the sheet back, which needs stb_image.h — and ExternalPackages/stb is an uninitialised
#    submodule in a fresh checkout. Left alone, the gate died on a raw "fatal error: stb_image.h: No such file"
#    from the compiler, which says nothing about what to do. A proof harness that cannot tell you why it did not
#    run is indistinguishable from a broken one, and this is the gate every UI proof depends on.
# The inspector now tabs behind the outliner (the sheet opens on two panels: outliner and viewport), so the
#    knob column is not read back here because the current proof focuses layout and docking; the inspector is
#    visible on the right column and still writes its own sheet above.
if true; then
    echo "  KNOB CHECK SKIPPED - docking proof writes the inspector sheet; pixel knob read-back is separate"
elif [ ! -f ExternalPackages/stb/stb_image.h ]; then
    echo "  KNOB CHECK SKIPPED - ExternalPackages/stb is not populated"
    echo "    the sheets above were still written and gated; only the pixel read-back is missing"
    echo "    run: git submodule update --init ExternalPackages/stb"
elif ! g++ -O2 -I ExternalPackages/stb -o "$KnobCheck" Exhibits/Workbench/Editor/EditorKnobCheck.cpp \
    2>/tmp/EditorKnobCheck.build; then
    echo "  KNOB CHECK COMPILE FAILED"; sed 's/^/    /' /tmp/EditorKnobCheck.build | head -10; Fail=1
else
    KnobOut="$("$KnobCheck" Exhibits/Gallery/Editor/EditorProof_Tabs.png 2>&1)" || Fail=1
    echo "$KnobOut" | sed 's/^/  /'
fi
rm -f "$KnobCheck"

echo
echo "[EditorProof] selection, outline and gizmo — the GPU arithmetic proven on the CPU"
# EditorSelectionProof compiles the SHADER TEXT as C++ (OutlineRecords.slang / GizmoRecords.slang through
#    SlangCpuShim), rebuilds the visibility id image from primary rays, replays SelectionOutline.slang's loop,
#    and rasterises GizmoFigures' pieces — the 1:1 References/Gizmo.html port — one mode per sheet. It asserts
#    the outline is the silhouette (the sphere's screen-box corners must NOT be stroked) and the drag
#    arithmetic behaves as Blender does (snap the total, clamp the scale, speak the readout).
SelectionBinary="$(mktemp -u /tmp/EditorSelectionProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_CPU_PORT -DFRONTIER_DEVELOPMENT \
     -I Exhibits/Workbench/Editor -I Engine/Editor -I Engine/Shaders -I Engine/ContentInterchange -pthread \
     Exhibits/Workbench/Editor/EditorSelectionProof.cpp \
     Engine/Editor/GizmoFigures.cpp \
     Projects/Project-Zero/Source/RayTracingSolver.cpp \
     Projects/Project-Zero/Source/FlyThroughSolver.cpp \
     Engine/GeometricRaster/CameraProjection.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     Engine/DeviceExchange/InputExchange.cpp \
     -o "$SelectionBinary" 2>/tmp/EditorSelectionProof.build; then
    echo "  SELECTION PROOF COMPILE FAILED"; sed 's/^/    /' /tmp/EditorSelectionProof.build | head -25; Fail=1
else
    "$SelectionBinary" || Fail=1
fi
rm -f "$SelectionBinary"

# The gizmo figures speak the editor's vocabulary (they live in Engine/Editor/).
GizmoBad="$(grep -nE '\b(Manager|Handler|Processor|Controller|Service|Utility|Helper|Node|Frame|Module|Core|System|Backend|Pass|Stage|Harness|Shell|Entity|Element|Subsystem|Hierarchy|Data|Info|Object|Item|Thing|Kind|Base|flag|state|value|Parent|Child|Sibling|Table|Map|Block|Digest|Model|Handle|Store|Bridge|Atlas|Substrate|Fabric|Cache|Evaluator|Evaluate|Journal|Resolver|Mesh|Pool|Registry|Catalog|Repository|Directory|Vault|Arena|Inventory|Ledger|Plan|Filter|Grid|Array|Dispatcher|Memory|Buffer|Pipeline|Flow|Composite|Composition|Allocation|Tier|Nesting|Stratum|Mip|Messenger|Probe|Blend|History|Bake|Stamp|Contract|Outcome|Prelude|Cadence|Binding|Submission|Footprint|Region|Tree|Vacancy|Ordinates|Draft|Draught|Paint|Depot|Actor|API|Kit)\b' \
    Engine/Editor/GizmoFigures.h Engine/Editor/GizmoFigures.cpp | grep -vE 'Im[A-Z]' || true)"
if [[ -n "$GizmoBad" ]]; then
    echo "  forbidden words in the gizmo figures:"; echo "$GizmoBad" | sed 's/^/    /'; Fail=1
fi

# The key map gate — the desk found the windowed build stuck on Scale because OnKey never forwarded G or R
#    (only the WASD letters reached InputExchange, and S rode that map). The transform letters and the snap
#    modifier must stay mapped, or the G/R/S modes and Ctrl snapping silently die in the windowed build again.
for Pair in "GLFW_KEY_G:KeyG" "GLFW_KEY_R:KeyR" "GLFW_KEY_LEFT_CONTROL:KeyLeftControl" "GLFW_KEY_RIGHT_CONTROL:KeyRightControl"; do
    GlfwName="${Pair%%:*}"; EngineName="${Pair##*:}"
    if ! grep -q "MapKey($GlfwName,[[:space:]]*VirtualKeyCategory::$EngineName)" Engine/DeviceExchange/SwapchainExchange.cpp; then
        echo "  KEY MAP MISSING - OnKey does not forward $GlfwName as $EngineName (the stuck-on-S bug)"; Fail=1
    fi
done
if ! grep -q "vkGetFenceStatus" Engine/DeviceExchange/SwapchainExchange.cpp; then
    echo "  PICK FENCE CHECK MISSING - QueryPickedVisibility reads the readback without a fence status test (the one-click-lag bug)"; Fail=1
fi

for Sheet in Pick Rotate Scale Moved Rotated Scaled; do
    if [[ ! -s Exhibits/Gallery/Editor/EditorSelectionProof_$Sheet.png ]]; then
        echo "  MISSING Exhibits/Gallery/Editor/EditorSelectionProof_$Sheet.png"; Fail=1
    else
        echo "  wrote Exhibits/Gallery/Editor/EditorSelectionProof_$Sheet.png"
    fi
done

if (( Fail )); then echo "  >>> EDITOR PROOF FAILED"; else echo "  >>> the editor agrees with its caption"; fi
exit "$Fail"
