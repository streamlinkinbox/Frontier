//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/SelectionVerification.cpp — Phase 4: pick / box selection, select modes, hide / isolate, undo / redo
//============================================================================================================================================
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <cstdio>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 4 · Selection Verification — pick plane · box · control mode · UndoSequence · duplicate / mirror");
    ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
    SceneDocument& Doc = Host.Document();
    auto Figure = [&](const char* Name) { return Doc.Find(std::string(Name)); };

    Panel.Section("Click and box selection through the pick plane");
    {
        Host.Execute("sphere (0,0,1) 1 ; cylinder (3,0,0) 0.8 2 ; line (-3,-2) (-3,2) ; view top ; view fit ; gizmo off");
        Host.Execute("click 640 400");
        Panel.Expect("Click on the sphere selects it", Figure("Sphere")->Selected && Doc.SelectedCount() == 1);
        Host.Execute("click 400 400 --shift");
        Panel.Expect("Shift-click on empty space keeps the selection", Doc.SelectedCount() == 1);
        Host.Execute("click 640 400 --shift");
        Panel.Expect("Shift-click toggles off", Doc.SelectedCount() == 0);
        Host.Execute("select box 560 300 950 500");
        Panel.Expect("Box takes sphere and cylinder, not the line", Figure("Sphere")->Selected && Figure("Cylinder")->Selected && !Figure("Line")->Selected);
        Host.Execute("select box 0 0 1279 799 --subtract");
        Panel.Expect("Subtract box clears", Doc.SelectedCount() == 0);
        Host.Execute("select box 0 0 1279 799");
        Panel.Expect("Full-viewport box selects all three", Doc.SelectedCount() == 3);
        Host.Execute("select invert");
        Panel.Expect("Invert → none", Doc.SelectedCount() == 0);
        Host.Execute("pointer 640 400");
        Host.Execute("select all ; hide Sphere ; select none ; select box 0 0 1279 799");
        Panel.Expect("Hidden figure are not box-selectable", !Figure("Sphere")->Selected && Doc.SelectedCount() == 2);
        Host.Execute("unhide all ; select Line ; isolate");
        Panel.Expect("Isolate hides everything but the selection", Figure("Sphere")->Hidden && Figure("Cylinder")->Hidden && !Figure("Line")->Hidden);
        Host.Execute("isolate off");
        Panel.Expect("Isolate off restores", !Figure("Sphere")->Hidden && !Figure("Cylinder")->Hidden);
    }

    Panel.Section("Control-point mode");
    {
        Host.Execute("clear ; cpcurve (0,0) (1,2) (3,-1) (5,1) ; view top ; view fit ; selectmode control ; select ControlCurve ; gizmo off");
        Panel.Expect("selectmode control recorded", Host.CurrentSelectMode() == SelectMode::Control);
        // find pole 1 pixel
        double X = 0, Y = 0; (void)Host.Camera().WorldToPixel({ 1, 2, 0 }, 1280, 800, X, Y);
        char Line[128]; std::snprintf(Line, sizeof Line, "click %d %d", int(X), int(Y)); Host.Execute(Line);
        Panel.Expect("Click on a pole selects that pole", Figure("ControlCurve")->PoleSelected(1) && Doc.SelectedPoleCount() == 1);
        (void)Host.Camera().WorldToPixel({ 3, -1, 0 }, 1280, 800, X, Y);
        std::snprintf(Line, sizeof Line, "click %d %d --shift", int(X), int(Y)); Host.Execute(Line);
        Panel.Expect("Shift-click adds a second pole", Doc.SelectedPoleCount() == 2);
        Host.Execute("gizmo on ; gizmo status");
        Vec3 Pivot = Host.Gizmo().CurrentPivot().Origin;
        Panel.Within("Gizmo pivot = centroid of the selected poles", Pivot.Distance({ 2.0, 0.5, 0.0 }), 1e-9);
        Host.Execute("select poles ControlCurve 1 ; gizmo size 160 ; gizmo grips");
        double Hx = 0, Hy = 0; (void)Host.Camera().WorldToPixel(Host.Gizmo().GripAnchor(GizmoGrip::TranslateX, Host.Camera(), 800), 1280, 800, Hx, Hy);
        std::snprintf(Line, sizeof Line, "pointer %d %d ; click ; pointer %d %d --ctrl ; release", int(Hx), int(Hy), int(Hx) + 100, int(Hy)); Host.Execute(Line);
        Vec3 P1 = Figure("ControlCurve")->PolePosition(1);
        Panel.Expect("Gizmo in control mode moves only the selected pole", std::fabs(P1.Y - 2.0) < 1e-9 && P1.X > 1.2 && std::fmod(P1.X + 1e-9, 0.25) < 1e-6);
        Panel.Within("Other poles untouched", Figure("ControlCurve")->PolePosition(2).Distance({ 3, -1, 0 }), 1e-12);
        Host.Execute("selectmode whole");
        Panel.Expect("Leaving control mode drops pole selection", Doc.SelectedPoleCount() == 0 && Figure("ControlCurve")->Selected);
        Host.Execute("selectmode cycle");
        Panel.Expect("Tab cycles whole ⇄ control", Host.CurrentSelectMode() == SelectMode::Control);
        Host.Execute("key 4");
        Panel.Expect("Hotkey 4 = whole mode", Host.CurrentSelectMode() == SelectMode::Whole);
    }

    Panel.Section("UndoSequence");
    {
        Host.Execute("clear ; sphere (0,0,1) 1 ; cylinder (3,0,0) 0.8 2");
        const size_t Depth = Host.Timeline().UndoEntries().size();
        Host.Execute("list ; describe Sphere ; view iso");
        Panel.Expect("Read-only commands do not record", Host.Timeline().UndoEntries().size() == Depth);
        Host.Execute("move Sphere (1,0,0)");
        Panel.Expect("Mutating command records one entry", Host.Timeline().UndoEntries().size() == Depth + 1);
        Host.Execute("undo");
        Panel.Within("Undo restores the pole positions", Figure("Sphere")->Bounds().Centre().Distance({ 0, 0, 1 }), 1e-9);
        Host.Execute("redo");
        Panel.Within("Redo reapplies", Figure("Sphere")->Bounds().Centre().Distance({ 1, 0, 1 }), 1e-9);
        Host.Execute("undo ; delete Cylinder");
        Panel.Expect("New command after undo clears redo", !Host.Timeline().CanRedo() && Figure("Cylinder") == nullptr);
        Host.Execute("undo");
        Panel.Expect("Undo delete brings the figure back with its identity", Figure("Cylinder") != nullptr && Figure("Cylinder")->Identity == 2);
        int Before = Host.RefusalCount();
        Host.Execute("undo 99");
        Host.Execute("undo");
        Panel.Expect("Undo past the beginning is a refusal", Host.RefusalCount() == Before + 1 && Doc.Figures().empty());
        Host.Execute("redo 99");
        Panel.Expect("Redo 99 replays to the tip: sphere present, cylinder deleted again", Figure("Cylinder") == nullptr && Figure("Sphere") != nullptr && !Host.Timeline().CanRedo());
        Host.Execute("key ctrl+z");
        Panel.Expect("Ctrl+Z bound", Figure("Cylinder") != nullptr);
        Host.Execute("key ctrl+shift+z");
        Panel.Expect("Ctrl+Shift+Z bound", Figure("Cylinder") == nullptr);
    }

    Panel.Section("Duplicate and mirror");
    {
        Host.Execute("clear ; cylinder (2,0,0) 0.5 1 ; select Cylinder ; duplicate (0,3,0)");
        Panel.Expect("Duplicate creates a selected copy with a unique name", Figure("Cylinder.2") && Figure("Cylinder.2")->Selected && !Figure("Cylinder")->Selected);
        Panel.Within("Duplicate offset applied", Figure("Cylinder.2")->Bounds().Centre().Distance({ 2, 3, 0.5 }), 1e-9);
        Host.Execute("select Cylinder ; mirror x --copy");
        Panel.Expect("Mirror copy exists", Figure("Cylinder.3") != nullptr);
        Panel.Within("Mirror across x flips the centre", Figure("Cylinder.3")->Bounds().Centre().Distance({ -2, 0, 0.5 }), 1e-9);
        // orientation: mirrored surface must still face outward → normal at a side point points away from the axis
        const BrepBody& M = Figure("Cylinder.3")->Body;
        const NurbsSurface& Side = M.Faces[0].Surface;
        Vec3 Pm = Side.Sample(0.125, 0.5), Nm = M.FaceNormal(0, 0.125, 0.5);
        Vec3 Radial = Vec3{ Pm.X + 2.0, Pm.Y, 0.0 }.Normalised();
        Panel.Expect("Mirrored cylinder keeps outward normals (body re-oriented after reflection)", Nm.Dot(Radial) > 0.9 && M.Validate().Solid());
        Host.Execute("key shift+d");
        Panel.Expect("Shift+D duplicates in place", Doc.Figures().size() == 4);
        Host.Execute("render Proof_04d_DuplicateMirror");
    }

    return Panel.Conclude();
}
