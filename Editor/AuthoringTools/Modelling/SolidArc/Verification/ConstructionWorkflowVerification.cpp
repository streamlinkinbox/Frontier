// CAD drawing regression: camera/workplane coordinates, mouse gestures, Enter/Escape,
// and the interactive-resolution seam used by the native windowed viewport.
#include "Console/ConsoleHost.h"
#include "Interaction/SnapResolution.h"
#include "VerificationPanel.h"

using namespace Frontier;

int main()
{
    VerificationPanel Check("SolidArc construction gestures");
    ConsoleHost Host("/tmp/SolidArcConstruction", 640, 480);
    Host.SetDimensionsVisible(false);
    Check.Expect("top view", Host.Execute("view top"));
    Check.Expect("disable snaps for precise mouse-to-workplane proof", Host.Execute("snap off"));
    Vec3 First, Second;
    Check.Expect("first camera ray hits workplane",
        SnapResolution::PlaneHit(190, 140, Host.Camera(), 640, 480, Workplane::XY(), First));
    Check.Expect("Enter before pointer does not create a line", Host.Execute("tool line") &&
        !Host.Execute("key enter") && Host.HasActiveTool() && Host.Document().Figures().empty());
    Check.Expect("Escape cancels without creating anything", Host.Execute("key esc") &&
        !Host.HasActiveTool() && Host.Document().Figures().empty());

    Check.Expect("arm line without placing geometry", Host.Execute("tool line") &&
        Host.HasActiveTool() && Host.Document().Figures().empty());
    Host.MoveToolPointer(190, 140, false);
    Check.Expect("pointer hover has no document side effects", Host.Document().Figures().empty());
    Check.Expect("first click sets a point without placing geometry", Host.Execute("click 190 140") &&
        Host.HasActiveTool() && Host.Document().Figures().empty());
    // The native UI reduces its working pixel budget during interaction. The tool
    // must remap its pointer and camera context when that target changes size.
    Host.SeatSurface(800, 600, 1);
    Check.Expect("second camera ray hits resized workplane",
        SnapResolution::PlaneHit(610, 370, Host.Camera(), 800, 600, Workplane::XY(), Second));
    Host.MoveToolPointer(610, 370, false);
    Check.Expect("second click finishes the line", Host.Execute("click 610 370") &&
        !Host.HasActiveTool() && Host.Document().Figures().size() == 1);
    if (!Host.Document().Figures().empty())
    {
        const NurbsCurve& Line = Host.Document().Figures().front().Curve;
        Check.Within("start follows first click in world space", Line.StartPoint().Distance(First), 1e-5);
        Check.Within("end follows second click after target resize", Line.EndPoint().Distance(Second), 1e-5);
    }

    const size_t Existing = Host.Document().Figures().size();
    Check.Expect("polyline arms without inserting a figure", Host.Execute("tool polyline") &&
        Host.Document().Figures().size() == Existing);
    Check.Expect("polyline gathers two mouse points", Host.Execute("click 210 200") &&
        Host.Execute("click 400 230") && Host.HasActiveTool());
    Check.Expect("Enter commits the polyline", Host.Execute("key enter") &&
        !Host.HasActiveTool() && Host.Document().Figures().size() == Existing + 1);
    Check.Expect("Escape discards a partially drawn rectangle", Host.Execute("tool rect") &&
        Host.Execute("click 240 180") && Host.Execute("key esc") &&
        Host.Document().Figures().size() == Existing + 1);
    ConsoleHost Edges("/tmp/SolidArcEdgeShortcut", 320, 240);
    Edges.SetDimensionsVisible(false);
    Check.Expect("create body for edge shortcuts", Edges.Execute("box (-1,-1,0) (1,1,1) --name=Block"));
    const uint32_t Identity = Edges.Document().Find("Block")->Identity;
    Check.Expect("pick one edge explicitly", Edges.Execute("select edges Block 0"));
    // Same command assembled by the windowed B/C handler: numeric IDs (not #ID,
    // since # begins a console comment) and an explicit --edges list.
    Check.Expect("C chamfers the picked edge transactionally", Edges.Execute("chamfer " +
        std::to_string(Identity) + " 0.1 --edges=0") && Edges.Document().Figures().size() == 1);
    Check.Expect("undo restores the uncut box", Edges.Execute("undo") &&
        Edges.Document().Find("Block") != nullptr);
    Check.Expect("B fillets the picked edge transactionally", Edges.Execute("fillet " +
        std::to_string(Identity) + " 0.1 --edges=0") && Edges.Document().Figures().size() == 1);
    return Check.Conclude();
}
