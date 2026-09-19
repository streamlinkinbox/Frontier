//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/HotkeyChart.cpp — Default chart
//============================================================================================================================================

#include "HotkeyChart.h"
#include <algorithm>

namespace Frontier
{

void HotkeyChart::Bind(Key KeyCode, uint8_t Modifiers, std::string Verb, std::string Description) noexcept
{
    Unbind(KeyCode, Modifiers);
    Entries.push_back({ KeyCode, Modifiers, std::move(Verb), std::move(Description) });
}

bool HotkeyChart::Unbind(Key KeyCode, uint8_t Modifiers) noexcept
{
    auto It = std::remove_if(Entries.begin(), Entries.end(), [&](const HotkeyEntry& B) { return B.KeyCode == KeyCode && B.Modifiers == Modifiers; });
    bool Removed = It != Entries.end();
    Entries.erase(It, Entries.end());
    return Removed;
}

const HotkeyEntry* HotkeyChart::Find(Key KeyCode, uint8_t Modifiers) const noexcept
{
    for (const HotkeyEntry& B : Entries) if (B.KeyCode == KeyCode && B.Modifiers == Modifiers) return &B;
    return nullptr;
}

HotkeyChart HotkeyChart::Defaults() noexcept
{
    HotkeyChart C;
    auto B = [&](const char* Chord, const char* Verb, const char* Description)
    {
        Key K; uint8_t M;
        if (ParseKeyChord(Chord, K, M)) C.Bind(K, M, Verb, Description);
    };
    //---------------------------------------------- transform (Plasticity = Blender) ----------------------------------------------
    B("g",           "tool move",          "Move (grab) selection — X/Y/Z lock axis, Shift+X/Y/Z lock plane, type distance");
    B("r",           "tool rotate",        "Rotate selection — axis lock, type degrees");
    B("s",           "tool scale",         "Scale selection — axis lock, type factor");
    B("alt+x",       "mirror",             "Mirror selection");
    B("shift+d",     "duplicate",          "Duplicate then move");
    B("ctrl+d",      "tool place",         "Place duplicate at point");
    //---------------------------------------------- sketch tools ----------------------------------------------
    B("shift+a",     "tool line",          "Line (Plasticity Shift+A)");
    B("alt+shift+a", "menu add",           "Add menu (Blender Shift+A equivalent listing)");
    B("shift+b",     "tool box",           "Box");
    B("shift+c",     "tool cylinder",      "Cylinder");
    B("shift+s",     "tool sphere",        "Sphere");
    B("ctrl+shift+c","tool circle",        "Centre circle");
    B("ctrl+shift+r","tool rect",          "Corner rectangle");
    B("ctrl+shift+a","tool arc",           "Centre arc");
    B("ctrl+shift+p","tool polygon",       "Polygon");
    B("ctrl+shift+s","tool spline",        "Spline (interpolating)");
    B("ctrl+shift+l","tool slot",          "Slot");
    B("ctrl+shift+e","tool ellipse",       "Ellipse");
    //---------------------------------------------- solid ops ----------------------------------------------
    B("e",           "tool extrude",       "Extrude selected curve/face");
    B("q",           "boolean union selected",     "Boolean union of the selected bodies / profiles (Plasticity Q)");
    B("shift+q",     "boolean subtract selected",  "Boolean subtract: last selected body / profile from the rest");
    B("ctrl+q",      "boolean intersect selected", "Boolean intersect of the selected bodies / profiles");
    B("c",           "tool cut",           "Cut");
    B("l",           "loft selected",      "Loft the selection in order (areas, curves, body edges); closed → solid");
    B("shift+p",     "sweep selected",     "Sweep: first selected = profile, second = path");
    B("p",           "pipe selected 0.25", "Pipe along the selected curve / edge (radius via `pipe`)");
    B("shift+l",     "fillpatch selected", "Patch: Coons / N-sided fill over the selected boundaries");
    B("b",           "fillet selected 0.25",  "Fillet the selected curves' corners (Plasticity B; radius via `fillet`)");
    B("shift+b",     "chamfer selected 0.25", "Chamfer the selected curves' corners");
    B("o",           "offset selected 0.25",  "Offset the selected curves (Plasticity O)");
    B("t",           "tool trim",             "Trim");
    B("j",           "join selected",      "Join curves");
    B("alt+j",       "explode selected",   "Explode joined curve");
    B("shift+r",     "repeat",             "Repeat last command");
    B("f9",          "adjust",             "Adjust last operation (Blender F9)");
    //---------------------------------------------- selection ----------------------------------------------
    B("1",           "selectmode control", "Select control points");
    B("2",           "selectmode edge",    "Select edges / curves");
    B("3",           "selectmode face",    "Select faces");
    B("4",           "selectmode solid",   "Select solids");
    B("tab",         "selectmode cycle",   "Cycle selection mode (Blender Tab)");
    B("a",           "select all",         "Select all");
    B("alt+a",       "select none",        "Select none");
    B("ctrl+i",      "select invert",      "Invert selection");
    B("h",           "hide selected",      "Hide selected");
    B("shift+h",     "hide unselected",    "Hide unselected");
    B("alt+h",       "unhide all",         "Unhide all");
    B("x",           "delete selected",    "Delete selected");
    B("delete",      "delete selected",    "Delete selected");
    B("ctrl+z",      "undo",               "Undo");
    B("ctrl+shift+z","redo",               "Redo");
    B("ctrl+y",      "redo",               "Redo");
    B("f",           "menu search",        "Command search (Plasticity F / Blender F3)");
    B("f3",          "menu search",        "Command search");
    B("shift+f",     "menu suggested",     "Suggested commands for selection");
    //---------------------------------------------- view ----------------------------------------------
    B("numpad1",     "view front",         "Front (−Y)");
    B("ctrl+numpad1","view back",          "Back (+Y)");
    B("numpad3",     "view right",         "Right (+X)");
    B("ctrl+numpad3","view left",          "Left (−X)");
    B("numpad7",     "view top",           "Top (+Z)");
    B("ctrl+numpad7","view bottom",        "Bottom (−Z)");
    B("numpad5",     "view toggle",        "Perspective / orthographic");
    B("numpad0",     "view iso",           "Isometric");
    B("numpad4",     "view orbit -15 0",   "Orbit left 15°");
    B("numpad6",     "view orbit 15 0",    "Orbit right 15°");
    B("numpad8",     "view orbit 0 15",    "Orbit up 15°");
    B("numpad2",     "view orbit 0 -15",   "Orbit down 15°");
    B("numpad+",     "view dolly 1",       "Dolly in");
    B("numpad-",     "view dolly -1",      "Dolly out");
    B("numpad.",     "view fit selected","Fit  (Blender Numpad .)");
    B("home",        "view fit",         "Fit ");
    B("space",       "view fit selected","Fit  (Plasticity Space)");
    B("alt+z",       "show xray toggle",   "X-ray");
    B("alt+shift+z", "show overlays toggle","Overlays");
    B("ctrl+space",  "workplane pick",     "Construction plane from selection");
    B("shift+space", "workplane xy",       "Reset construction plane");
    B("ctrl+=",      "tool measure",       "Measure");
    //---------------------------------------------- modal (documented here; consumed by ToolSession) ----------------------------------------------
    // Enter / Left-click confirm · Esc / Right-click cancel · X Y Z axis lock · Shift+X/Y/Z plane lock · digits numeric entry
    // · Tab next field · Ctrl snap-toggle while held · Shift precision · Alt quantise angle
    return C;
}

} // namespace Frontier
