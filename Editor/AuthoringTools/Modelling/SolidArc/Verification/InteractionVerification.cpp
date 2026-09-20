//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/InteractionVerification.cpp — Phase 3 proofs: numeric entry, axis locks, snapping, hotkeys, tool outcomes
//============================================================================================================================================

#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

using namespace Frontier;

namespace
{
const SceneFigure* Last(ConsoleHost& H) { return H.Document().Figures().empty() ? nullptr : &H.Document().Figures().back(); }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 3 · Interaction Verification — ToolSession · SnapResolution · HotkeyChart · numeric entry");
    ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
    Host.Execute("view top; view fit");
    const double PixelsPerMetre = 800.0 / (2.0 * Host.Camera().OrthographicHalfHeight());
    auto Px = [&](double X, double Y) { return std::pair<double, double>{ 640.0 + X * PixelsPerMetre, 400.0 - Y * PixelsPerMetre }; };

    Panel.Section("Numeric entry grammar");
    {
        Host.Execute("tool line; point (0,0); type 4");
        Panel.Within("`4` after a point = 4 m along +X", Last(Host)->Curve.EndPoint().Distance({ 4, 0, 0 }), 1e-12);
        Host.Execute("tool line; point (4,0); type @0,3");
        Panel.Within("`@0,3` = relative offset", Last(Host)->Curve.EndPoint().Distance({ 4, 3, 0 }), 1e-12);
        Host.Execute("tool line; point (4,3); type a180,4");
        Panel.Within("`a180,4` = polar 180°, 4 m", Last(Host)->Curve.EndPoint().Distance({ 0, 3, 0 }), 1e-12);
        Host.Execute("tool line; point (1,1); type 3,5");
        Panel.Within("`3,5` = absolute workplane point", Last(Host)->Curve.EndPoint().Distance({ 3, 5, 0 }), 1e-12);
        Host.Execute("tool line; point (0,0); type 2*3");
        Panel.Equal("`2*3` arithmetic = 6 m", Last(Host)->Curve.Length(), 6.0, 1e-12);
        Host.Execute("tool circle; point (-5,0); type r2");
        Panel.Equal("`r2` radius on a distance prompt", Last(Host)->Curve.Length(), 2.0 * ScalarCriteria::TwoPi, 1e-9);
        Host.Execute("tool polygon; type n8; point (0,-5); type 1");
        Panel.Expect("`n8` sets polygon sides (8 edges → 9 poles)", Last(Host)->Curve.PoleCount() == 9);
        Host.Execute("tool arc; point (5,-4); point (7,-4); type a90");
        Panel.Equal("`a90` arc sweep = quarter circumference", Last(Host)->Curve.Length(), ScalarCriteria::HalfPi * 2.0, 1e-9);
    }

    Panel.Section("Axis / plane locks");
    {
        Host.Execute("tool line; point (6,1); key x; point (9.7,2.9)");
        Panel.Within("X lock projects the end onto the anchor's X line", std::fabs(Last(Host)->Curve.EndPoint().Y - 1.0), 1e-12);
        Panel.Within("… keeping the X component", std::fabs(Last(Host)->Curve.EndPoint().X - 9.7), 1e-12);
        Host.Execute("tool line; point (0,0,0); key y; point (2,3,4)");
        Panel.Within("Y lock", Last(Host)->Curve.EndPoint().Distance({ 0, 3, 0 }), 1e-12);
        Host.Execute("tool line; point (0,0,0); key shift+z; point (2,3,4)");
        Panel.Within("Shift+Z = XY plane lock drops Z", Last(Host)->Curve.EndPoint().Distance({ 2, 3, 0 }), 1e-12);
        Host.Execute("tool line; point (0,0,0); key x; key x; point (2,3,0)");
        Panel.Within("Pressing X twice clears the lock", Last(Host)->Curve.EndPoint().Distance({ 2, 3, 0 }), 1e-12);
    }

    Panel.Section("Cancel / confirm semantics");
    {
        size_t Before = Host.Document().Figures().size();
        Host.Execute("tool line; point (0,0); key esc");
        Panel.Expect("Esc cancels without creating geometry", Host.Document().Figures().size() == Before);
        Host.Execute("tool polyline; point (0,0); point (1,0); point (1,1); key enter");
        Panel.Expect("Enter finishes an open-ended polyline (3 poles)", Last(Host)->Curve.PoleCount() == 3);
        Host.Execute("tool polyline; point (0,0); point (1,0); click --right");
        Panel.Expect("Right-click confirms an optional prompt", Last(Host)->Curve.PoleCount() == 2);
        Host.Execute("tool spline; point (0,0); point (1,2); point (2,0); point (3,2); key backspace; key enter");
        Panel.Expect("Backspace removes the last point of a repeating prompt", Last(Host)->Curve.Sample(Last(Host)->Curve.DomainEnd()).Distance({ 2, 0, 0 }) < 1e-9);
        Host.Execute("tool circle; point (1,1); point (1,1)");
        Panel.Expect("Coincident second point is refused, tool still running", Host.Document().Figures().back().Classification == FigureClassification::Curve && Last(Host)->Curve.Classification != CurveClassification::Circle);
        Host.Execute("tool cancel");
    }

    Panel.Section("Snapping through pixels");
    {
        Host.Execute("clear; tool line; point (0,0); type 4; tool line; point (4,0); type @0,3; tool circle; point (-5,0); type r2; tool rect; point (2,-6); point (6,-2)");
        auto Locate = [&](double X, double Y, const Vec3* Anchor = nullptr)
        {
            return SnapResolution::Resolve(X, Y, Host.Camera(), 1280, 800, Workplane::XY(), Host.Document(), SnapSettings{}, Anchor);
        };
        auto [Ex, Ey] = Px(4.0, 0.0);
        SnapCandidate S = Locate(Ex + 5, Ey + 4);
        Panel.Expect("5 px off a shared endpoint → intersection/endpoint at (4,0)", (S.Classification == SnapClassification::Intersection || S.Classification == SnapClassification::Endpoint) && S.Position.Distance({ 4, 0, 0 }) < 1e-9);
        auto [Mx, My] = Px(2.0, 0.0);
        S = Locate(Mx + 3, My - 2);
        Panel.Expect("Midpoint of the 4 m line", S.Classification == SnapClassification::Midpoint && S.Position.Distance({ 2, 0, 0 }) < 1e-9);
        auto [Cx, Cy] = Px(-5.0, 0.0);
        S = Locate(Cx - 4, Cy + 6);
        Panel.Expect("Circle centre", S.Classification == SnapClassification::Centre && S.Position.Distance({ -5, 0, 0 }) < 1e-9);
        auto [Qx, Qy] = Px(-5.0, 2.0);
        S = Locate(Qx + 2, Qy + 3);
        Panel.Expect("Circle quadrant (top)", S.Classification == SnapClassification::Quadrant && S.Position.Distance({ -5, 2, 0 }) < 1e-6);
        auto [Ox, Oy] = Px(-5.0 + 2.0 * std::cos(0.7), 2.0 * std::sin(0.7));
        S = Locate(Ox + 3, Oy);
        Panel.Expect("Off the special points: on-curve snap lands on the circle", S.Classification == SnapClassification::OnCurve && std::fabs(S.Position.Distance({ -5, 0, 0 }) - 2.0) < 1e-9);
        auto [Gx, Gy] = Px(0.93, 1.9);
        S = Locate(Gx, Gy);
        Panel.Expect("Empty space → lattice (1,2)", S.Classification == SnapClassification::Lattice && S.Position.Distance({ 1, 2, 0 }) < 1e-9);
        Vec3 Anchor{ 0, 3, 0 };
        auto [Ax, Ay] = Px(3.0, 3.05);
        S = Locate(Ax, Ay, &Anchor);
        Panel.Expect("Axis-aligned with the anchor → axis-x snap keeps Y = 3", S.Classification == SnapClassification::AxisX && std::fabs(S.Position.Y - 3.0) < 1e-9);
        auto [Rx, Ry] = Px(2.0, -2.0);
        S = Locate(Rx + 6, Ry + 6);
        Panel.Expect("Rectangle corner = polyline vertex endpoint", S.Classification == SnapClassification::Endpoint && S.Position.Distance({ 2, -2, 0 }) < 1e-9);
        SnapSettings Off; Off.Enabled = false;
        S = SnapResolution::Resolve(Ex + 5, Ey + 4, Host.Camera(), 1280, 800, Workplane::XY(), Host.Document(), Off, nullptr);
        Panel.Expect("Snapping disabled (Ctrl) → free workplane hit", S.Classification == SnapClassification::Free && S.Position.Distance({ 4, 0, 0 }) > 0.01);
    }

    Panel.Section("Pointer-driven tool with snap");
    {
        auto [Sx, Sy] = Px(4.0, 3.0);
        char Cmd[256];
        std::snprintf(Cmd, sizeof Cmd, "tool line; pointer %.0f %.0f; click; type @2,0", Sx + 4, Sy - 5);
        Host.Execute(Cmd);
        Panel.Within("Start snapped exactly to the endpoint despite a 6 px miss", Last(Host)->Curve.StartPoint().Distance({ 4, 3, 0 }), 1e-9);
        Panel.Within("Typed relative end", Last(Host)->Curve.EndPoint().Distance({ 6, 3, 0 }), 1e-9);
    }

    Panel.Section("Hotkeys and transforms");
    {
        Host.Execute("select none; select Line; key g; key x; type 12");
        Panel.Within("G X 12 moves the selection 12 m along X", Host.Document().Find(std::string("Line"))->Curve.StartPoint().Distance({ 12, 0, 0 }), 1e-9);
        Host.Execute("key r; type 90");
        Vec3 E = Host.Document().Find(std::string("Line"))->Curve.EndPoint(), St = Host.Document().Find(std::string("Line"))->Curve.StartPoint();
        Panel.Within("R 90 rotates about the selection pivot: the line is now vertical", std::fabs(E.X - St.X), 1e-9);
        Host.Execute("key s; type 0.5");
        Panel.Equal("S 0.5 halves the length", Host.Document().Find(std::string("Line"))->Curve.Length(), 2.0, 1e-9);
        Host.Execute("key numpad1");
        Panel.Within("Numpad1 = front view", Host.Camera().Forward().Distance(Vec3::UnitY()), 1e-9);
        Host.Execute("key ctrl+numpad7");
        Panel.Within("Ctrl+Numpad7 = bottom view", Host.Camera().Forward().Distance(Vec3::UnitZ()), 1e-3);
        Host.Execute("bind ctrl+shift+q \"echo bound\"");
        int Before = Host.RefusalCount();
        Host.Execute("key ctrl+shift+q");
        Panel.Expect("User hotkey dispatches", Host.RefusalCount() == Before);
        Host.Execute("key f12");
        Panel.Expect("Unbound key is reported as a refusal", Host.RefusalCount() == Before + 1);
        Key K; uint8_t M;
        Panel.Expect("ParseKeyChord ctrl+alt+numpad+", ParseKeyChord("ctrl+alt+numpad+", K, M) && K == Key::NumpadPlus && M == (ModifierCtrl | ModifierAlt));
        Panel.Expect("ParseKeyChord shift+space", ParseKeyChord("shift+space", K, M) && K == Key::Space && M == ModifierShift);
    }

    Panel.Section("GizmoPRO and shading");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        Host.Execute("cylinder (0,0,0) 1 1.5 ; select Cylinder ; view iso ; view fit ; gizmo size 160");
        const TransformGizmo& G = Host.Gizmo();
        Panel.Equal("Pivot = selection bounds centre z", G.CurrentPivot().Origin.Z, 0.75, 1e-9);
        // every grip anchor probes back to itself (or to a nearer grip in front of it)
        int Hits = 0;
        for (int I = int(GizmoGrip::TranslateX); I <= int(GizmoGrip::RotateZ); ++I)
        {
            GizmoGrip H = static_cast<GizmoGrip>(I);
            Vec3 W = G.GripAnchor(H, Host.Camera(), 800);
            double X = 0, Y = 0; (void)Host.Camera().WorldToPixel(W, 1280, 800, X, Y);
            if (G.Locate(X, Y, Host.Camera(), 1280, 800) != GizmoGrip::None) ++Hits;
        }
        Panel.Expect("All 12 grip anchors are pickable", Hits == 12);
        Panel.Expect("Empty pixel probes none", G.Locate(50, 50, Host.Camera(), 1280, 800) == GizmoGrip::None);
        // scripted drags
        auto PixelOf = [&](GizmoGrip H, double& X, double& Y) { (void)Host.Camera().WorldToPixel(G.GripAnchor(H, Host.Camera(), 800), 1280, 800, X, Y); };
        double X, Y;
        PixelOf(GizmoGrip::TranslateX, X, Y);
        char Line[256];
        std::snprintf(Line, sizeof Line, "pointer %d %d ; click ; pointer %d %d --ctrl ; release", int(X), int(Y), int(X) + 97, int(Y) + 49);
        Host.Execute(Line);
        Box3 B = Host.Document().Find(std::string("Cylinder"))->Bounds();
        Panel.Equal("X move with Ctrl snaps to 0.25 multiples (min x)", std::fmod(std::fabs(B.Low.X + 1.0) + 1e-9, 0.25), 0.0, 1e-6);
        Panel.Expect("X move only changes x", std::fabs(B.Low.Y + 1) < 1e-9 && std::fabs(B.High.Z - 1.5) < 1e-9 && B.Low.X > -1.0 + 0.2);
        double Moved = B.Low.X + 1.0;
        PixelOf(GizmoGrip::ScaleX, X, Y);
        std::snprintf(Line, sizeof Line, "pointer %d %d ; click ; pointer %d %d --ctrl ; release", int(X), int(Y), int(X) + 37, int(Y) + 21);
        Host.Execute(Line);
        B = Host.Document().Find(std::string("Cylinder"))->Bounds();
        double Factor = (B.High.X - B.Low.X) / 2.0;
        Panel.Equal("X scale snaps to 0.1 multiples", std::fmod(Factor + 1e-9, 0.1), 0.0, 1e-6);
        Panel.Equal("X scale keeps the pivot fixed", (B.High.X + B.Low.X) * 0.5, Moved, 1e-6);
        Panel.Equal("X scale leaves y untouched", B.High.Y - B.Low.Y, 2.0, 1e-9);
        PixelOf(GizmoGrip::RotateZ, X, Y);
        std::snprintf(Line, sizeof Line, "pointer %d %d ; click ; pointer %d %d --ctrl ; release", int(X), int(Y), int(X) + 47, int(Y) - 24);
        Host.Execute(Line);
        B = Host.Document().Find(std::string("Cylinder"))->Bounds();
        Panel.Expect("Z rotate widens the y extent of the stretched cylinder", B.High.Y - B.Low.Y > 2.0 + 1e-6);
        Panel.Equal("Z rotate keeps height", B.High.Z - B.Low.Z, 1.5, 1e-9);
        Panel.Expect("Release ends the drag", !G.Dragging());
        Host.Execute("gizmo rotate");
        Panel.Expect("Separable layout hides translate cones", G.Locate(X, Y, Host.Camera(), 1280, 800) != GizmoGrip::TranslateX);
        // shading verbs
        int Before = Host.RefusalCount();
        Host.Execute("show shading plastic ; show shading flat ; show shading matcap ; matcap Cylinder gold ; matcap Cylinder 9 ; tint Cylinder 1 0 0");
        Panel.Expect("Shading / matcap / tint verbs accepted", Host.RefusalCount() == Before);
        Panel.Expect("Per-whole matcap stored on the figure", Host.Document().Find(std::string("Cylinder"))->Matcap == 9);
        Host.Execute("matcap Cylinder velvet");
        Panel.Expect("Unknown studio refused", Host.RefusalCount() == Before + 1);
        Panel.Expect("Ten studios listed", MatcapCount() == 10 && std::string(MatcapName(1)) == "chrome");
    }

    return Panel.Conclude();
}
