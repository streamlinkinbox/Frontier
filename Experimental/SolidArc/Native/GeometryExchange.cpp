//============================================================================================================================================
//                                                           GEOMETRYEXCHANGE.CPP
//============================================================================================================================================
// 📦 Browser access to the existing SolidArc command interpreter, document and software rasterizer.

#include "Console/ConsoleHost.h"
#include <emscripten/emscripten.h>
#include <sstream>
#include <iomanip>

using namespace Frontier;
namespace
{
std::map<int, std::unique_ptr<ConsoleHost>> Documents;
int ActiveIdentity = 0;
int NextIdentity = 1;
std::string Summary;
RasterImage Picture;

ConsoleHost& ActiveDocument()
{
    return *Documents.at(ActiveIdentity);
}
std::string EncodeText(const std::string& Text)
{
    std::ostringstream Stream;
    Stream << '"';
    for (unsigned char Character : Text)
    {
        if (Character == '"' || Character == '\\') Stream << '\\' << Character;
        else if (Character < 32) Stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(Character) << std::dec;
        else Stream << Character;
    }
    Stream << '"';
    return Stream.str();
}
void EncodePosition(std::ostringstream& Stream, Vec3 Position)
{
    Stream << '[' << Position.X << ',' << Position.Y << ',' << Position.Z << ']';
}
}

extern "C"
{
EMSCRIPTEN_KEEPALIVE int CreateDocument()
{
    ActiveIdentity = NextIdentity++;
    Documents[ActiveIdentity] = std::make_unique<ConsoleHost>("/", 800, 600);
    ActiveDocument().SeatSurface(800, 600, 1);
    return ActiveIdentity;
}
EMSCRIPTEN_KEEPALIVE int ActivateDocument(int Identity)
{
    if (!Documents.contains(Identity)) return 0;
    ActiveIdentity = Identity;
    return 1;
}
EMSCRIPTEN_KEEPALIVE void ReleaseDocument(int Identity)
{
    Documents.erase(Identity);
    if (ActiveIdentity == Identity && !Documents.empty()) ActiveIdentity = Documents.begin()->first;
}
EMSCRIPTEN_KEEPALIVE int ExecuteCommand(const char* Text)
{
    return ActiveDocument().Execute(Text);
}
EMSCRIPTEN_KEEPALIVE const char* DescribeDocument()
{
    auto& Host = ActiveDocument();
    std::ostringstream Stream;
    Stream << std::setprecision(12) << "{\"identity\":" << ActiveIdentity << ",\"undo\":" << Host.Timeline().CanUndo()
           << ",\"redo\":" << Host.Timeline().CanRedo() << ",\"mode\":" << int(Host.CurrentSelectMode()) << ",\"figures\":[";
    bool Separator = false;
    for (const auto& Figure : Host.AllFigures())
    {
        if (Separator) Stream << ',';
        Separator = true;
        const auto Bounds = Figure.Bounds();
        Stream << "{\"id\":" << Figure.Identity << ",\"name\":" << EncodeText(Figure.Name)
               << ",\"classification\":" << int(Figure.Classification) << ",\"selected\":" << Figure.Selected
               << ",\"locked\":" << Figure.Locked << ",\"hidden\":" << Figure.Hidden << ",\"faces\":" << Figure.Body.Faces.size()
               << ",\"edges\":" << Figure.Body.Edges.size() << ",\"vertices\":" << Figure.Body.Vertices.size()
               << ",\"poles\":" << Figure.PoleCount() << ",\"low\":";
        EncodePosition(Stream, Bounds.Empty() ? Vec3{} : Bounds.Low);
        Stream << ",\"high\":"; EncodePosition(Stream, Bounds.Empty() ? Vec3{} : Bounds.High);
        Stream << ",\"pickedFaces\":[";
        for (size_t Index = 0; Index < Figure.SelectedFaces.size(); ++Index) { if (Index) Stream << ','; Stream << Figure.SelectedFaces[Index]; }
        Stream << "],\"pickedEdges\":[";
        for (size_t Index = 0; Index < Figure.SelectedEdges.size(); ++Index) { if (Index) Stream << ','; Stream << Figure.SelectedEdges[Index]; }
        Stream << "],\"pickedPoles\":[";
        for (size_t Index = 0; Index < Figure.SelectedPoles.size(); ++Index) { if (Index) Stream << ','; Stream << Figure.SelectedPoles[Index]; }
        Stream << "]}";
    }
    Stream << "],\"areas\":[";
    Separator = false;
    for (const auto& Region : Host.Document().Areas())
    {
        if (Separator) Stream << ',';
        Separator = true;
        Stream << "{\"id\":" << Region.Identity - SceneDocument::AreaIdentityBase << ",\"selected\":" << Region.Selected
               << ",\"area\":" << Region.Cell.Area << ",\"holes\":" << Region.Cell.Holes.size() << '}';
    }
    Stream << "],\"dimensions\":[";
    Separator = false;
    for (const auto& Dimension : Host.AllDimensions())
    {
        if (Separator) Stream << ',';
        Separator = true;
        Stream << "{\"id\":" << Dimension.Id << ",\"anchor\":" << Dimension.Anchor << ",\"label\":" << EncodeText(Dimension.Label.empty() ? Dimension.AnchorName : Dimension.Label)
               << ",\"number\":" << Dimension.Value << ",\"slot\":" << (Dimension.BlueprintForm == SceneFigure::ParametricForm::None ? -1 : Dimension.Slot) << ",\"form\":" << int(Dimension.Form) << '}';
    }
    Stream << "],\"history\":[";
    Separator = false;
    for (const auto& Revision : Host.Timeline().UndoEntries())
    {
        if (Separator) Stream << ',';
        Separator = true;
        Stream << EncodeText(Revision.Label);
    }
    Stream << "],\"gizmo\":{\"visible\":" << Host.GizmoVisible() << ",\"layout\":" << int(Host.Gizmo().CurrentLayout())
           << ",\"dragging\":" << Host.Gizmo().Dragging() << ",\"hover\":" << int(Host.Gizmo().Hovered())
           << ",\"readout\":" << EncodeText(Host.Gizmo().Drag().Readout) << ",\"grips\":[";
    Separator = false;
    const bool Selected = Host.Document().SelectedCount() + Host.Document().SelectedPoleCount() + Host.Document().SelectedFaceCount() + Host.Document().SelectedEdgeCount() > 0;
    for (int Index = 1; Selected && Host.GizmoVisible() && Index <= 12; ++Index)
    {
        const auto Grip = static_cast<GizmoGrip>(Index);
        if (!Host.Gizmo().Visible(Grip)) continue;
        const Vec3 Anchor = Host.Gizmo().GripAnchor(Grip, Host.Camera(), Host.Raster().Height());
        double Horizontal = 0, Vertical = 0;
        if (!Host.Camera().WorldToPixel(Anchor, Host.Raster().Width(), Host.Raster().Height(), Horizontal, Vertical)) continue;
        if (!std::isfinite(Horizontal) || !std::isfinite(Vertical)) continue;
        if (Separator) Stream << ',';
        Separator = true;
        Stream << "{\"id\":" << Index << ",\"name\":" << EncodeText(GizmoGripName(Grip))
               << ",\"u\":" << Horizontal / Host.Raster().Width() << ",\"v\":" << Vertical / Host.Raster().Height() << '}';
    }
    Stream << "]},\"camera\":{" << "\"yaw\":" << Host.Camera().Yaw << ",\"pitch\":" << Host.Camera().Pitch
           << ",\"distance\":" << Host.Camera().Distance << ",\"orthographic\":" << Host.Camera().Orthographic << ",\"pivot\":";
    EncodePosition(Stream, Host.Camera().Pivot);
    Stream << "}}";
    Summary = Stream.str();
    return Summary.c_str();
}
EMSCRIPTEN_KEEPALIVE const unsigned char* RenderDocument(int Width, int Height)
{
    Width = std::clamp(Width, 64, 1600);
    Height = std::clamp(Height, 64, 1200);
    ActiveDocument().SeatSurface(Width, Height, 1);
    ActiveDocument().Render();
    Picture = ActiveDocument().Raster().Readback();
    return Picture.Pixels.data();
}
EMSCRIPTEN_KEEPALIVE void OrbitDocument(double Horizontal, double Vertical)
{
    ActiveDocument().Camera().Orbit(Horizontal, Vertical);
}
EMSCRIPTEN_KEEPALIVE void PanDocument(double Horizontal, double Vertical, double Height)
{
    ActiveDocument().Camera().Pan(Horizontal, Vertical, Height);
}
EMSCRIPTEN_KEEPALIVE void ZoomDocument(double Steps)
{
    if (!std::isfinite(Steps)) return;
    auto& Camera = ActiveDocument().Camera();
    Camera.Dolly(std::clamp(Steps, -40.0, 40.0));
    Camera.Distance = std::clamp(Camera.Distance, 0.01, 1e8);
}
EMSCRIPTEN_KEEPALIVE void SeatDocument(int Width, int Height, double Scale)
{
    ActiveDocument().SeatSurface(std::clamp(Width, 64, 1600), std::clamp(Height, 64, 1200), 1);
    ActiveDocument().ResizeGizmoAtView(110.0 * std::clamp(Scale, 0.1, 2.0));
}
EMSCRIPTEN_KEEPALIVE int AimGizmoDocument(double Horizontal, double Vertical)
{
    return ActiveDocument().AimGizmoAtView(Horizontal, Vertical);
}
EMSCRIPTEN_KEEPALIVE int BeginGizmoDocument(double Horizontal, double Vertical)
{
    return ActiveDocument().BeginGizmoAtView(Horizontal, Vertical);
}
EMSCRIPTEN_KEEPALIVE int DragGizmoDocument(double Horizontal, double Vertical, int Snapping)
{
    return ActiveDocument().DragGizmoAtView(Horizontal, Vertical, Snapping != 0);
}
EMSCRIPTEN_KEEPALIVE int FinishGizmoDocument(int Cancel)
{
    return ActiveDocument().FinishGizmoAtView(Cancel != 0);
}
EMSCRIPTEN_KEEPALIVE int PickDocument(double Horizontal, double Vertical, int Extend)
{
    return ActiveDocument().SelectAtView(Horizontal, Vertical, Extend != 0);
}
}
