//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Console/ConsoleInteraction.cpp — Phase 3 commands: modal tools, synthetic pointer/keys, snapping, hotkeys, HUD
//============================================================================================================================================
// The console is the input device: `pointer x y` moves it, `click` presses, `key g` / `key shift+x` types, `type 3,4`
//    enters numbers — exactly the events a window would send. Everything a tool shows on screen (rubber band, snap
//    glyph, prompt line) is also printed, so a script transcript is a full account of the interaction.

#include "ConsoleHost.h"
#include "Presentation/ScenePresentation.h"
#include <cstring>

namespace Frontier
{

ToolSession::Context ConsoleHost::ToolContext() const noexcept
{
    ToolSession::Context C;
    C.Camera = &View; C.Scene = &Scene; C.Snap = &Snap; C.Plane = Plane;
    C.Width = Surface->Width(); C.Height = Surface->Height();
    Box3 B = Scene.Bounds(true);
    C.SelectionPivot = B.Empty() ? Plane.Origin : SelectionPivot();
    return C;
}

void ConsoleHost::OnToolResult(const ToolResult& Result) noexcept
{
    if (!Result.Completed) { Row("tool: %s", Result.Summary.c_str()); if (Result.Summary.rfind("refused", 0) == 0) ToolReportedRefusal = true; return; }
    for (const NurbsCurve& C : Result.Curves)
    {
        const char* Stem = C.Classification == CurveClassification::Freeform ? (Tool.CurrentTool() == ToolChoice::ControlCurve ? "ControlCurve" : "Spline") : Describe(C.Classification);
        SceneFigure& Figure = Scene.AddCurve(Stem, C);
        DescribeFigure(Figure);
    }
    if (Result.Curves.empty())                                                         // transform
    {
        int N = 0, Poles = 0;
        for (SceneFigure& I : Scene.Figures())
        {
            if (!I.Selected) continue;
            if (Mode == SelectMode::Control && !I.SelectedPoles.empty())                  // edit mode: only the chosen poles move
            {
                for (int P : I.SelectedPoles) { I.MovePole(P, Result.Transform.TransformPoint(I.PolePosition(P))); ++Poles; }
            }
            else I.Transform(Result.Transform);
            ++N; DescribeFigure(I);
        }
        if (Poles) Row("%s → %d pole(s) on %d figure(s)", Result.Summary.c_str(), Poles, N);
        else Row("%s → %d figure(s)", Result.Summary.c_str(), N);
    }
    else Row("tool: %s", Result.Summary.c_str());
}

bool ConsoleHost::Dispatch(const InputEvent& E) noexcept
{
    if (Tool.Active() && Tool.Receive(E))
    {
        if (Tool.Active())
        {
            const ToolPreview& P = Tool.Preview();
            std::string Line = std::string("[") + ToolName(Tool.CurrentTool()) + "] " + Tool.CurrentPrompt().Label;
            for (const std::string& R : P.Readout) Line += "  · " + R;
            Row("%s", Line.c_str());
        }
        return true;
    }
    if (E.Action == InputAction::KeyPress)
    {
        if (const HotkeyEntry* B = Hotkeys.Find(E.KeyCode, E.Modifiers))
        {
            Row("%s → %s", DescribeKeyChord(E.KeyCode, E.Modifiers).c_str(), B->Verb.c_str());
            return Execute(B->Verb);
        }
        return Refuse("%s is not bound", DescribeKeyChord(E.KeyCode, E.Modifiers).c_str());
    }
    const bool GizmoLive = GizmoShown && (Scene.SelectedCount() + Scene.SelectedPoleCount() + Scene.SelectedFaceCount() + Scene.SelectedEdgeCount() > 0);
    if (E.Action == InputAction::PointerMove && GizmoLive)
    {
        if (GizmoRig.Dragging())
        {
            GizmoRig.UpdateDrag(E.PixelX, E.PixelY, E.Ctrl(), View, Surface->Width(), Surface->Height());
            ApplyGizmoDelta(GizmoRig.Drag().Delta);
            Row("gizmo %s%s", GizmoRig.Drag().Readout.c_str(), E.Ctrl() ? "  [snap]" : "");
            return true;
        }
        RefreshGizmoPivot();
        GizmoRig.AimAt(View);
        GizmoGrip H = GizmoRig.Locate(E.PixelX, E.PixelY, View, Surface->Width(), Surface->Height());
        if (H != GizmoRig.Hovered()) { GizmoRig.MarkHovered(H); Row("gizmo hover %s", GizmoGripName(H)); }
        if (H != GizmoGrip::None) return true;
    }
    if (E.Action == InputAction::PointerMove && !Tool.Active()) { HoverAtPixel(E.PixelX, E.PixelY); return false; }
    if (E.Action == InputAction::PointerPress && E.Button == PointerButton::Left && GizmoLive)
    {
        RefreshGizmoPivot();
        if (GizmoRig.BeginDrag(E.PixelX, E.PixelY, View, Surface->Width(), Surface->Height()))
        {
            GizmoOriginals.clear();
            for (const SceneFigure& I : Scene.Figures()) if (I.Selected || !I.SelectedPoles.empty() || !I.SelectedFaces.empty() || !I.SelectedEdges.empty()) GizmoOriginals.emplace_back(I.Identity, I);
            Row("gizmo grab %s", GizmoGripName(GizmoRig.Drag().Grip));
            return true;
        }
    }
    if (E.Action == InputAction::PointerRelease && GizmoRig.Dragging())
    {
        GizmoDrag D = GizmoRig.EndDrag();
        ApplyGizmoDelta(D.Delta);
        GizmoOriginals.clear();
        RefreshGizmoPivot();
        Row("gizmo release %s", D.Readout.c_str());
        Undo.Settle(Scene);                                                           // closes the entry opened at the grab
        return true;
    }
    if (E.Action == InputAction::PointerPress && E.Button == PointerButton::Left)
    {
        SelectAtPixel(E.PixelX, E.PixelY, E.Shift());
        return true;
    }
    if (E.Action == InputAction::Wheel) { View.Dolly(E.WheelSteps); return true; }
    return false;
}

void ConsoleHost::DrawToolPreview() noexcept
{
    if (!Tool.Active()) return;
    const ToolPreview& P = Tool.Preview();
    DrawRecord Band = ScenePresentation::Tinted(1.0f, 0.85f, 0.35f); Band.LineWidth = 2.0f;
    for (const NurbsCurve& C : P.Curves) Surface->DrawSegments(ScenePresentation::CurveSegments(C), Band);
    if (!P.Points.empty())
    {
        PointStream Pts; for (Vec3 Q : P.Points) Pts.Append(Q, PointGlyph::Square);
        DrawRecord D = ScenePresentation::Tinted(1.0f, 0.85f, 0.35f); D.PointSize = 7.0f;
        Surface->DrawPoints(Pts, D);
    }
    // Axis-lock guide line through the anchor.
    if (P.Lock != AxisLock::None && !P.Points.empty())
    {
        Vec3 A = P.Points.back();
        Vec3 Dir = P.Lock == AxisLock::X ? Plane.AxisX : P.Lock == AxisLock::Y ? Plane.AxisY : Plane.Normal();
        float Col[3] = { P.Lock == AxisLock::X ? 0.95f : 0.3f, P.Lock == AxisLock::Y ? 0.9f : 0.3f, P.Lock == AxisLock::Z ? 0.95f : 0.3f };
        SegmentStream S; S.Append(A - Dir * 1000.0, A + Dir * 1000.0);
        DrawRecord G = ScenePresentation::Tinted(Col[0], Col[1], Col[2], 0.8f); G.LineWidth = 1.0f; G.Dashed = true;
        Surface->DrawSegments(S, G);
    }
    // Snap glyph: shape says which snap.
    if (P.Snap.Classification != SnapClassification::None)
    {
        PointGlyph Glyph = PointGlyph::Cross;
        switch (P.Snap.Classification)
        {
            case SnapClassification::Endpoint: case SnapClassification::ControlPoint: Glyph = PointGlyph::Square; break;
            case SnapClassification::Midpoint: case SnapClassification::Quadrant:     Glyph = PointGlyph::Diamond; break;
            case SnapClassification::Centre: case SnapClassification::Origin:         Glyph = PointGlyph::Ring; break;
            case SnapClassification::Intersection:                          Glyph = PointGlyph::Cross; break;
            case SnapClassification::OnCurve: case SnapClassification::Perpendicular: case SnapClassification::Tangent: Glyph = PointGlyph::Disc; break;
            default:                                              Glyph = PointGlyph::Cross; break;
        }
        PointStream S; S.Append(P.Cursor, Glyph);
        bool Strong = P.Snap.Classification != SnapClassification::Free && P.Snap.Classification != SnapClassification::Lattice;
        DrawRecord D = Strong ? ScenePresentation::Tinted(0.35f, 0.90f, 0.95f) : ScenePresentation::Tinted(0.8f, 0.8f, 0.85f, 0.7f);
        D.PointSize = Strong ? 13.0f : 9.0f;
        Surface->DrawPoints(S, D);
    }
}

void ConsoleHost::RegisterInteraction() noexcept
{
    auto Add = [&](const char* Verb, const char* Help, Command Fn) { Commands[Verb] = std::move(Fn); Usage[Verb] = Help; };
    auto Number = [&](const CommandLine& C, size_t I, double& Out) -> bool { auto N = C.Number(I); if (!N) return false; Out = *N; return true; };
    auto Event = [&](InputAction A) { InputEvent E; E.Action = A; E.PixelX = PointerX; E.PixelY = PointerY; return E; };
    auto Modifiers = [&](const CommandLine& C) { uint8_t M = ModifierNone; if (C.Switch("shift")) M |= ModifierShift; if (C.Switch("ctrl")) M |= ModifierCtrl; if (C.Switch("alt")) M |= ModifierAlt; return M; };

    //---------------------------------------------- tools ----------------------------------------------
    Add("tool", "tool <name> — start a modal tool: line polyline rect centerrect polygon slot circle circle2 circle3 arc arc3 ellipse spline cpcurve move rotate scale  ·  tool cancel", [=, this](const CommandLine& C)
    {
        if (C.Count() < 1) return Refuse("tool: name required");
        if (C.Arguments[0] == "cancel") { Tool.Cancel(); return true; }
        auto K = ParseToolChoice(C.Arguments[0]);
        if (!K) return Refuse("tool: unknown tool '%s'", C.Arguments[0].c_str());
        if ((*K == ToolChoice::Move || *K == ToolChoice::Rotate || *K == ToolChoice::Scale) && Scene.Bounds(true).Empty()) return Refuse("tool %s: nothing selected", C.Arguments[0].c_str());
        ToolReportedRefusal = false;
        Tool.Begin(*K, ToolContext(), [this](const ToolResult& O) { OnToolResult(O); });
        Row("[%s] %s", ToolName(*K), Tool.CurrentPrompt().Label.c_str());
        return true;
    });
    Add("point", "point (x,y[,z]) — supply a point to the running tool (planar points lift onto the workplane)", [=, this](const CommandLine& C)
    {
        if (!Tool.Active()) return Refuse("point: no tool running");
        auto P = C.Point(0); if (!P) return Refuse("point: (x,y[,z]) required");
        bool Planar = C.Arguments[0].find(',') == C.Arguments[0].rfind(',');
        Vec3 W = Planar ? Plane.ToWorld({ P->X, P->Y }) : *P;
        ToolReportedRefusal = false;
        if (!Tool.SupplyPoint(W)) return Refuse("point: rejected (coincident with previous?)");
        if (Tool.Active()) { std::string L; for (const std::string& R : Tool.Preview().Readout) L += "  · " + R; Row("[%s] %s%s", ToolName(Tool.CurrentTool()), Tool.CurrentPrompt().Label.c_str(), L.c_str()); }
        return !ToolReportedRefusal;
    });
    Add("type", "type <text> — numeric entry: 3 · 2,4 · @1,1 (relative) · r2.5 (radius) · a45 (angle) · a30,2 (polar) · n6 (sides) · d2 (degree) · 3*2", [=, this](const CommandLine& C)
    {
        if (!Tool.Active()) return Refuse("type: no tool running");
        if (C.Count() < 1) return Refuse("type: text required");
        ToolReportedRefusal = false;
        if (!Tool.SupplyText(C.Arguments[0])) return Refuse("type: '%s' not understood here", C.Arguments[0].c_str());
        if (Tool.Active()) { std::string L; for (const std::string& R : Tool.Preview().Readout) L += "  · " + R; Row("[%s] %s%s", ToolName(Tool.CurrentTool()), Tool.CurrentPrompt().Label.c_str(), L.c_str()); }
        return !ToolReportedRefusal;
    });
    Add("pointer", "pointer x y [--ctrl] — move the synthetic pointer (pixels, origin top-left); tools snap and preview", [=, this](const CommandLine& C)
    {
        double X, Y; if (!Number(C, 0, X) || !Number(C, 1, Y)) return Refuse("pointer: x y required");
        PointerX = X; PointerY = Y;
        InputEvent E = Event(InputAction::PointerMove); E.Modifiers = Modifiers(C);
        if (!Dispatch(E))
        {
            Vec3 Hit; if (SnapResolution::PlaneHit(X, Y, View, Surface->Width(), Surface->Height(), Plane, Hit)) Row("pointer (%d,%d) → workplane (%.4f %.4f %.4f)", int(X), int(Y), Hit.X, Hit.Y, Hit.Z);
            else Row("pointer (%d,%d) misses the workplane", int(X), int(Y));
        }
        return true;
    });
    Add("click", "click [x y] [--right] [--middle] [--shift] [--ctrl] — press at the pointer (or at x y)", [=, this](const CommandLine& C)
    {
        double X, Y; if (Number(C, 0, X) && Number(C, 1, Y)) { PointerX = X; PointerY = Y; InputEvent M = Event(InputAction::PointerMove); M.Modifiers = Modifiers(C); Dispatch(M); }
        InputEvent E = Event(InputAction::PointerPress);
        E.Button = C.Switch("right") ? PointerButton::Right : C.Switch("middle") ? PointerButton::Middle : PointerButton::Left;
        E.Modifiers = Modifiers(C);
        ToolReportedRefusal = false;
        Dispatch(E);
        return !ToolReportedRefusal;
    });
    Add("release", "release [--ctrl] — release the pointer button (ends a gizmo drag)", [=, this](const CommandLine& C)
    {
        InputEvent E = Event(InputAction::PointerRelease); E.Button = PointerButton::Left; E.Modifiers = Modifiers(C);
        if (!Dispatch(E)) Row("release: nothing held");
        return true;
    });
    Add("key", "key <chord> — press a key: g, shift+x, ctrl+numpad1, enter, esc, tab, up, backspace", [=, this](const CommandLine& C)
    {
        if (C.Count() < 1) return Refuse("key: chord required");
        InputEvent E = Event(InputAction::KeyPress);
        if (!ParseKeyChord(C.Arguments[0], E.KeyCode, E.Modifiers)) return Refuse("key: unknown chord '%s'", C.Arguments[0].c_str());
        ToolReportedRefusal = false;
        return Dispatch(E) && !ToolReportedRefusal;
    });
    Add("wheel", "wheel steps — dolly", [=, this](const CommandLine& C)
    {
        double S; if (!Number(C, 0, S)) return Refuse("wheel: steps required");
        InputEvent E = Event(InputAction::Wheel); E.WheelSteps = S; Dispatch(E);
        Row("distance %.3f", View.Distance);
        return true;
    });
    Add("snap", "snap on|off  ·  snap lattice|geometry|oncurve|axis|intersections on|off  ·  snap radius px  ·  snap step m  ·  snap status", [=, this](const CommandLine& C)
    {
        if (C.Count() >= 1 && C.Arguments[0] == "status") {}
        else if (C.Count() == 1) { if (C.Arguments[0] != "on" && C.Arguments[0] != "off") return Refuse("snap: on|off"); Snap.Enabled = C.Arguments[0] == "on"; }
        else if (C.Count() >= 2)
        {
            const std::string& K = C.Arguments[0]; bool On = C.Arguments[1] == "on"; double V = C.Number(1).value_or(0.0);
            if (K == "lattice") Snap.Lattice = On; else if (K == "geometry") Snap.Geometry = On; else if (K == "oncurve") Snap.OnCurve = On;
            else if (K == "axis") Snap.Axis = On; else if (K == "intersections") Snap.Intersections = On;
            else if (K == "radius") Snap.Radius = V; else if (K == "step") Snap.LatticeStep = V; else if (K == "angle") Snap.AngleStep = ScalarCriteria::Radians(V);
            else return Refuse("snap: unknown setting '%s'", K.c_str());
        }
        Row("snap %s  lattice %s (step %.3g)  geometry %s  on-curve %s  axis %s  intersections %s  radius %.0f px",
            Snap.Enabled ? "on" : "off", Snap.Lattice ? "on" : "off", Snap.LatticeStep, Snap.Geometry ? "on" : "off", Snap.OnCurve ? "on" : "off", Snap.Axis ? "on" : "off", Snap.Intersections ? "on" : "off", Snap.Radius);
        return true;
    });
    Add("inspect", "inspect x y — report what the cursor would snap to at a pixel, without a tool", [=, this](const CommandLine& C)
    {
        double X, Y; if (!Number(C, 0, X) || !Number(C, 1, Y)) return Refuse("inspect: x y required");
        SnapCandidate S = SnapResolution::Resolve(X, Y, View, Surface->Width(), Surface->Height(), Plane, Scene, Snap, nullptr);
        Row("inspect (%d,%d) → %s at (%.4f %.4f %.4f)%s%u  %.1f px", int(X), int(Y), SnapName(S.Classification), S.Position.X, S.Position.Y, S.Position.Z, S.FigureIdentity ? "  #" : "  ", S.FigureIdentity, S.PixelDistance);
        return true;
    });
    Add("bind", "bind <chord> \"command\" — add or replace a hotkey", [=, this](const CommandLine& C)
    {
        if (C.Count() < 2) return Refuse("bind: chord and command required");
        Key K; uint8_t M; if (!ParseKeyChord(C.Arguments[0], K, M)) return Refuse("bind: unknown chord '%s'", C.Arguments[0].c_str());
        Hotkeys.Bind(K, M, C.Arguments[1], "user");
        Row("%s → %s", DescribeKeyChord(K, M).c_str(), C.Arguments[1].c_str());
        return true;
    });
    Add("unbind", "unbind <chord>", [=, this](const CommandLine& C)
    {
        if (C.Count() < 1) return Refuse("unbind: chord required");
        Key K; uint8_t M; if (!ParseKeyChord(C.Arguments[0], K, M)) return Refuse("unbind: unknown chord");
        return Hotkeys.Unbind(K, M) ? true : Refuse("unbind: %s was not bound", DescribeKeyChord(K, M).c_str());
    });
    Add("hotkeys", "hotkeys [sift] — print the hotkey chart", [=, this](const CommandLine& C)
    {
        std::string Needle = C.Count() ? C.Arguments[0] : "";
        int N = 0;
        for (const HotkeyEntry& B : Hotkeys.Bindings())
        {
            if (!Needle.empty() && B.Verb.find(Needle) == std::string::npos && B.Description.find(Needle) == std::string::npos) continue;
            std::printf("  %-16s %-24s %s\n", DescribeKeyChord(B.KeyCode, B.Modifiers).c_str(), B.Verb.c_str(), B.Description.c_str()); ++N;
        }
        Row("%d hotkey(s)", N);
        return true;
    });
    Add("repeat", "repeat — run the previous command again (Shift+R)", [=, this](const CommandLine&)
    {
        if (LastCommand.empty()) return Refuse("repeat: nothing to repeat");
        return Execute(LastCommand);
    });
    Add("hud", "hud — print the modal tool status: tool, prompt, points, lock, snap, readout", [=, this](const CommandLine&)
    {
        if (!Tool.Active()) { Row("no tool running  ·  workplane origin (%.2f %.2f %.2f)  ·  pointer (%d,%d)", Plane.Origin.X, Plane.Origin.Y, Plane.Origin.Z, int(PointerX), int(PointerY)); return true; }
        const ToolPreview& P = Tool.Preview();
        Row("tool %s  prompt %zu/%zu '%s'  lock %s  snap %s", ToolName(Tool.CurrentTool()), Tool.CurrentPromptIndex() + 1, (size_t)0 + Tool.CurrentPromptIndex() + 1, Tool.CurrentPrompt().Label.c_str(), AxisLockName(P.Lock), SnapName(P.Snap.Classification));
        for (size_t I = 0; I < P.Points.size(); ++I) Row("  point %zu (%.4f %.4f %.4f)", I, P.Points[I].X, P.Points[I].Y, P.Points[I].Z);
        Row("  cursor (%.4f %.4f %.4f)", P.Cursor.X, P.Cursor.Y, P.Cursor.Z);
        for (const std::string& R : P.Readout) Row("  %s", R.c_str());
        return true;
    });
    // "view toggle" for Numpad5 and the show toggles referenced by the chart.
    Add("selectmode", "selectmode control|edge|face|whole|cycle|status — 1/2/3/4 and Tab", [=, this](const CommandLine& C)
    {
        const std::string A = C.Count() ? C.Arguments[0] : "status";
        SelectMode Next = Mode;
        if (A == "control") Next = SelectMode::Control; else if (A == "edge") Next = SelectMode::Edge; else if (A == "face") Next = SelectMode::Face;
        else if (A == "whole" || A == "solid") Next = SelectMode::Whole;
        else if (A == "cycle") Next = Mode == SelectMode::Whole ? SelectMode::Control : SelectMode::Whole;   // Blender Tab: whole ⇄ edit (control points)
        else if (A != "status") return Refuse("selectmode: control|edge|face|whole|cycle|status");
        if (Next != Mode)
        {
            // Changing mode drops the sub-selection of the mode being left; figure selection persists.
            for (SceneFigure& I : Scene.Figures()) { I.SelectedPoles.clear(); I.SelectedFaces.clear(); I.SelectedEdges.clear(); }
            Mode = Next;
        }
        Row("select mode %s  ·  %d figure(s), %d pole(s), %d face(s), %d edge(s) selected", SelectModeName(Mode), Scene.SelectedCount(), Scene.SelectedPoleCount(), Scene.SelectedFaceCount(), Scene.SelectedEdgeCount());
        return true;
    });
}

} // namespace Frontier
