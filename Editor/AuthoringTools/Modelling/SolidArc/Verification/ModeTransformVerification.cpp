// Native console/kernel regression for the same modes the windowed SolidArc rail drives.
#include "Console/ConsoleHost.h"
#include "Kernel/TweakSolver.h"
#include "VerificationPanel.h"
#include <cmath>
#include <string>

using namespace Frontier;

int main()
{
    VerificationPanel Check("SolidArc selection-mode gizmo transforms");
    ConsoleHost Host("/tmp/SolidArcModeTransform", 320, 240);
    Host.SetDimensionsVisible(false);
    Check.Expect("create starting solid", Host.Execute("box (-1,-1,0) (1,1,1) --name=Body"));
    auto Body = [&]() { return Host.Document().Find("Body"); };
    const auto Initial = Body()->Body;
    const auto InitialReport = Initial.Validate();
    int Top = -1;
    for (int F = 0; F < static_cast<int>(Initial.Faces.size()); ++F)
        if (Initial.FaceNormal(F, .5, .5).Z > .9) { Top = F; break; }
    Check.Expect("box has a selectable top face", Top >= 0);
    if (Top < 0) return Check.Conclude();
    const auto Id = Body()->Identity;
    Check.Expect("enter face mode and select a face", Host.Execute("select faces Body " + std::to_string(Top)));
    Check.Expect("face translation uses the component transform command",
                 Host.Execute("transform selected --move=(0,0,0.25) --components"));
    Check.Within("the selected face moves", std::fabs(Body()->Body.Bounds().High.Z - 1.25), 1e-7);
    Check.Within("the opposite face stays in place", std::fabs(Body()->Body.Bounds().Low.Z), 1e-7);
    Check.Expect("body identity and topology survive face edit", Body()->Identity == Id && Body()->Body.Validate().Solid() &&
                 Body()->Body.Faces.size() == Initial.Faces.size());
    Check.Expect("face edit can undo", Host.Execute("undo"));
    Check.Within("undo restores original top face", std::fabs(Body()->Body.Bounds().High.Z - 1), 1e-7);
    Check.Expect("face edit can redo", Host.Execute("redo"));
    Check.Within("redo restores face edit", std::fabs(Body()->Body.Bounds().High.Z - 1.25), 1e-7);

    // Drive the same Begin/Drag/Finish seam used by the windowed viewport, not just the command parser.
    Host.Execute("view iso"); Host.Execute("view fit"); Host.ResizeGizmoAtView(155);
    (void)Host.RenderIfChanged();
    double GripX = 0, GripY = 0;
    const Vec3 Grip = Host.Gizmo().GripAnchor(GizmoGrip::TranslateX, Host.Camera(), Host.Raster().Height());
    (void)Host.Camera().WorldToPixel(Grip, Host.Raster().Width(), Host.Raster().Height(), GripX, GripY);
    const bool Began = Host.BeginGizmoAtView(GripX, GripY);
    Check.Expect("windowed face gizmo begins a drag", Began);
    if (Began)
    {
        const Vec3 Opposite = Body()->Body.Vertices[0].Point;
        (void)Host.DragGizmoAtView(GripX + 42, GripY, false);
        Check.Expect("face gizmo preview does not move every body vertex", Body()->Body.Vertices[0].Point.Distance(Opposite) < 1e-7);
        Check.Expect("face gizmo commits through component command", Host.FinishGizmoAtView(false));
        Check.Expect("face gizmo retains the opposite corner", Body()->Body.Vertices[0].Point.Distance(Opposite) < 1e-7);
    }

    int Edge = -1;
    for (int E = 0; E < static_cast<int>(Body()->Body.Edges.size()); ++E)
    {
        const auto& Line = Body()->Body.Edges[E];
        if (Line.VertexStart >= 0 && Line.VertexEnd >= 0 &&
            std::fabs(Body()->Body.Vertices[Line.VertexStart].Point.X - 1) < 1e-8 &&
            std::fabs(Body()->Body.Vertices[Line.VertexEnd].Point.X - 1) < 1e-8) { Edge = E; break; }
    }
    Check.Expect("box has a selectable edge", Edge >= 0);
    if (Edge < 0) return Check.Conclude();
    const auto BeforeEdge = Body()->Body;
    Check.Expect("edge mode applies a local edge tweak", Host.Execute("select edges Body " + std::to_string(Edge)) &&
                 Host.Execute("transform selected --move=(0.15,0,0) --components"));
    Check.Expect("edge edit is not whole-object translation", Body()->Body.Validate().Solid() &&
                 Body()->Body.Vertices.size() == BeforeEdge.Vertices.size() &&
                 std::fabs(Body()->Body.Bounds().Low.X - BeforeEdge.Bounds().Low.X) < 1e-7);

    Check.Expect("vertex selection enters control mode", Host.Execute("select poles Body 0") &&
                 Host.CurrentSelectMode() == SelectMode::Control && Body()->PoleCount() == int(Body()->Body.Vertices.size()));
    const auto BeforeVertex = Body()->Body;
    Check.Expect("vertex mode edits the selected corner", Host.Execute("transform selected --move=(0,0,0.1) --components"));
    Check.Expect("only one vertex moves (adjacent B-rep surfaces are refitted)",
        Body()->Body.Validate().Solid() &&
        Body()->Body.Vertices[0].Point.Distance(BeforeVertex.Vertices[0].Point) > .09 &&
        Body()->Body.Vertices[1].Point.Distance(BeforeVertex.Vertices[1].Point) < 1e-8);
    Check.Expect("body mode still moves the whole object", Host.Execute("selectmode whole") && Host.Execute("select Body") &&
                 Host.Execute("transform selected --move=(0,0,0.5)"));
    Check.Expect("whole-object move shifts both corners", Body()->Body.Vertices[0].Point.Distance(BeforeVertex.Vertices[0].Point) > .5 &&
                 Body()->Body.Vertices[1].Point.Distance(BeforeVertex.Vertices[1].Point) > .49);
    Check.Expect("source was a closed solid", InitialReport.Solid());

    ConsoleHost Other("/tmp/SolidArcModeRotation", 320, 240);
    Other.SetDimensionsVisible(false);
    Check.Expect("new body for rotation/scale", Other.Execute("box (-1,-1,0) (1,1,1) --name=Other"));
    auto Figure = [&]() { return Other.Document().Find("Other"); };
    int Cap = -1;
    for (int F = 0; F < static_cast<int>(Figure()->Body.Faces.size()); ++F)
        if (Figure()->Body.FaceNormal(F, .5, .5).Z > .9) { Cap = F; break; }
    Check.Expect("face mode supports local rotation", Cap >= 0 &&
        Other.Execute("select faces Other " + std::to_string(Cap)) &&
        Other.Execute("transform selected --rotate=(0,0,10) --pivot=(0,0,1) --components"));
    Check.Expect("rotated face keeps opposite face anchored", Figure()->Body.Validate().Solid() &&
                 std::fabs(Figure()->Body.Bounds().Low.Z) < 1e-7);
    Check.Expect("face mode supports local scaling", Other.Execute("transform selected --scale=(1.1,1.1,1) --pivot=(0,0,1) --components") &&
                 Figure()->Body.Validate().Solid());
    Check.Expect("component transform without a picked face refuses, never moves the body",
        Other.Execute("selectmode whole") && Other.Execute("selectmode face") &&
        !Other.Execute("transform selected --move=(0,0,1) --components"));
    ConsoleHost Picker("/tmp/SolidArcVertexPick", 640, 480);
    Picker.SetDimensionsVisible(false);
    Picker.Execute("box (-1,-1,0) (1,1,1) --name=PickBody");
    Picker.Execute("selectmode control"); Picker.Execute("view top"); Picker.Execute("view fit");
    (void)Picker.RenderIfChanged();
    double VertexX = 0, VertexY = 0;
    (void)Picker.Camera().WorldToPixel({-1,-1,1}, Picker.Raster().Width(), Picker.Raster().Height(), VertexX, VertexY);
    (void)Picker.SelectAtView(VertexX, VertexY, false);
    Check.Expect("control-mode body vertex can be picked in the rendered viewport", Picker.Document().SelectedPoleCount() == 1);
    return Check.Conclude();
}
