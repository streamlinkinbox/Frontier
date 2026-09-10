#!/usr/bin/env bash
# Headless visual proof for the development editor — compiles the patched vendor plus Engine/Editor, drives ten
#    ticks through the engine's tick order, rasterises the last tick with a dependency-free CPU rasteriser, and
#    gates the PNG: three occupied columns, a trapezoid slant on both tab edges, titled strips, the seated theme
#    tints, and the four faces the theme seats. Then the interaction phases drive the pointer the way the
#    engine does — the category menu opens, a Camera pick narrows the outline, the palette opens over the
#    console, the views menu poses the orbit, the gizmo answers — and each phase rasterises its own sheet.
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

[ -f "ExternalPackages/tomlpp/include/toml++/toml.hpp" ] || git clone --depth 1 -q https://github.com/marzer/tomlplusplus.git ExternalPackages/tomlpp

echo "[EditorProof] compiling the patched vendor + Engine/Editor (headless: no Vulkan, no GLFW)"
Binary="$(mktemp -u /tmp/EditorProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_DEVELOPMENT \
     -I ExternalPackages/imgui -I Engine/Editor -I Engine/DisplayPresentation -I ExternalPackages/tomlpp/include -I Scratchpad \
     Scratchpad/EditorProof.cpp \
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
     Engine/DeviceExchange/InputExchange.cpp \
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

# Banned vocabulary (CLAUDE.md §3, plus §13's Record table and the Kit rename). ImGui's own entry points name
#    their dock columns 'nodes', their draw-list spans 'buffers', and their scroll panes 'children'; those lines
#    are the vendor's vocabulary, not ours, and are excluded from the scan.
EditorFiles="Engine/Editor/EditorInstance.h Engine/Editor/ControlPanel.h Engine/Editor/ControlPanel.cpp
    Engine/Editor/EditorHost.h Engine/Editor/EditorHost.cpp
    Engine/Editor/OutlinerPanel.h Engine/Editor/OutlinerPanel.cpp
    Engine/Editor/ViewportPanel.h Engine/Editor/ViewportPanel.cpp
    Engine/Editor/InspectorPanel.h Engine/Editor/InspectorPanel.cpp
    Scratchpad/EditorProof.cpp"
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
KindHit="$(grep -rni 'kind' Engine/Editor/ Scratchpad/EditorProof.cpp || true)"
if [[ -n "$KindHit" ]]; then
    echo "  'kind' survives somewhere it must not:"; echo "$KindHit" | sed 's/^/    /'; Fail=1
fi
# shellcheck disable=SC2086
KitHit="$(grep -rni 'kit' Engine/Editor/ Scratchpad/EditorProof.cpp || true)"
if [[ -n "$KitHit" ]]; then
    echo "  'kit' survives somewhere it must not:"; echo "$KitHit" | sed 's/^/    /'; Fail=1
fi

# 'Record' lives in the editor only as the drawing verb: sixteen method names and the two lowercase verb forms.
#    Any seventeenth spelling is a noun sneaking back in.
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
RecordStanding
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

# The footer caption keeps its two figures: a selected count on the left, hits-of-total on the right.
SelectedCount="$(grep -c '%u selected' Engine/Editor/OutlinerPanel.cpp)"
TotalCount="$(grep -c '%u of %u' Engine/Editor/OutlinerPanel.cpp)"
if [[ "$SelectedCount" != "1" || "$TotalCount" != "1" ]]; then
    echo "  the footer caption lost its figures (selected $SelectedCount, of-total $TotalCount)"; Fail=1
fi

# Every popup the editor opens, the editor begins: the Open set and the Begin set must be the same four ids.
OpenPopups="$(grep -hoE 'OpenPopup\("##[a-z]+"\)' Engine/Editor/*.cpp | sort -u)"
BeginPopups="$(grep -hoE 'BeginPopup\("##[a-z]+"\)' Engine/Editor/*.cpp | sed 's/BeginPopup/OpenPopup/' | sort -u)"
if [[ "$OpenPopups" != "$BeginPopups" ]]; then
    echo "  a popup opens that never begins, or begins that never opens:"; Fail=1
fi
OpenCount="$(echo "$OpenPopups" | grep -c 'OpenPopup')"
if [[ "$OpenCount" != "4" ]]; then
    echo "  the editor seats four popups, no more:"; echo "$OpenPopups" | sed 's/^/    /'; Fail=1
fi

# No heap traffic while drawing: the panels must not allocate.
if grep -nE '(push_back|emplace_back|resize|reserve)[[:space:]]*\(|new[[:space:]]+[A-Za-z_\*]' Engine/Editor/*.cpp | grep -q .; then
    echo "  a panel appears to allocate — the tick must not touch the heap"; Fail=1
fi

echo
for Sheet in Tabs Menu Filtered Palette Views; do
    if [[ ! -s Diagnostics/EditorProof_$Sheet.png ]]; then
        echo "  MISSING Diagnostics/EditorProof_$Sheet.png"; Fail=1
    else
        echo "  wrote Diagnostics/EditorProof_$Sheet.png"
    fi
done

echo
echo "[EditorProof] the knobs sit on their fractions"
KnobCheck="$(mktemp -u /tmp/EditorKnobCheck.XXXXXX)"
# The knob check READS the sheet back, which needs stb_image.h — and ExternalPackages/stb is an uninitialised
#    submodule in a fresh checkout. Left alone, the gate died on a raw "fatal error: stb_image.h: No such file"
#    from the compiler, which says nothing about what to do. A proof harness that cannot tell you why it did not
#    run is indistinguishable from a broken one, and this is the gate every UI proof depends on.
if [ ! -f ExternalPackages/stb/stb_image.h ]; then
    echo "  KNOB CHECK SKIPPED - ExternalPackages/stb is not populated"
    echo "    the sheets above were still written and gated; only the pixel read-back is missing"
    echo "    run: git submodule update --init ExternalPackages/stb"
elif ! g++ -O2 -I ExternalPackages/stb -o "$KnobCheck" Scratchpad/EditorKnobCheck.cpp \
    2>/tmp/EditorKnobCheck.build; then
    echo "  KNOB CHECK COMPILE FAILED"; sed 's/^/    /' /tmp/EditorKnobCheck.build | head -10; Fail=1
else
    KnobOut="$("$KnobCheck" Diagnostics/EditorProof_Tabs.png 2>&1)" || Fail=1
    echo "$KnobOut" | sed 's/^/  /'
fi
rm -f "$KnobCheck"

if (( Fail )); then echo "  >>> EDITOR PROOF FAILED"; else echo "  >>> the editor agrees with its caption"; fi
exit "$Fail"
