/*==========================================================================================================================================
                                                          PLACEMENTGIZMO.CPP
==========================================================================================================================================*/
// 🧩 The transform handles: arrows along the world axes (Move), rings about the Euler axes (Rotate) and axis handles with
//    a centre square (Scale), a constant 90 px long on screen whatever the distance, and their drags.

#include "Engine/Interface/Controls/PlacementGizmo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

using Vector = std::array<double, 3>;

constexpr double Pi              = 3.14159265358979323846;
constexpr double DegreesPerRadian = 180.0 / Pi;
constexpr float  HandlePixels    = 90.0f;   // [px] arrow and axis length on screen
constexpr float  PickPixels      = 8.0f;    // [px] how near the pointer must be

Vector Add(const Vector& Left, const Vector& Right) { return {Left[0] + Right[0], Left[1] + Right[1], Left[2] + Right[2]}; }
Vector Subtract(const Vector& Left, const Vector& Right) { return {Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2]}; }
Vector Multiply(const Vector& Value, double Factor) { return {Value[0] * Factor, Value[1] * Factor, Value[2] * Factor}; }
double Dot(const Vector& Left, const Vector& Right) { return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2]; }
Vector Cross(const Vector& Left, const Vector& Right)
{
    return {Left[1] * Right[2] - Left[2] * Right[1], Left[2] * Right[0] - Left[0] * Right[2], Left[0] * Right[1] - Left[1] * Right[0]};
}
Vector Normalize(const Vector& Value)
{
    const double Length = std::sqrt(Dot(Value, Value));
    return Length > 1.0e-12 ? Multiply(Value, 1.0 / Length) : Vector{0.0, 0.0, 1.0};
}

// 📝 X, Y and Z: warm red, green and blue, brightened while hovered or held.
ImU32 ResolveAxisInk(int Axis, bool Lit)
{
    if (Lit) return IM_COL32(255, 214, 94, 255);
    switch (Axis)
    {
        case 0:  return IM_COL32(242, 101, 92, 245);
        case 1:  return IM_COL32(111, 211, 106, 245);
        case 2:  return IM_COL32(90, 160, 255, 245);
        default: return IM_COL32(236, 236, 236, 245);
    }
}

// 📝 The rotation of ConstructPlacement, R = Rz(γ) · Ry(β) · Rx(α), as three columns (the object's axes).
std::array<Vector, 3> ResolveObjectAxes(const SpatialConfiguration& Spatial)
{
    const double α = Spatial.OrientationDegrees[0] / DegreesPerRadian, β = Spatial.OrientationDegrees[1] / DegreesPerRadian,
                 γ = Spatial.OrientationDegrees[2] / DegreesPerRadian;
    const double Cα = std::cos(α), Sα = std::sin(α), Cβ = std::cos(β), Sβ = std::sin(β), Cγ = std::cos(γ), Sγ = std::sin(γ);
    return {Vector{Cβ * Cγ, Cβ * Sγ, -Sβ}, Vector{Cγ * Sα * Sβ - Cα * Sγ, Cα * Cγ + Sα * Sβ * Sγ, Cβ * Sα},
            Vector{Cα * Cγ * Sβ + Sα * Sγ, Cα * Sβ * Sγ - Cγ * Sα, Cα * Cβ}};
}

// 📝 The axes each Euler angle turns about: α about Rz(γ)·Ry(β)·X, β about Rz(γ)·Y, γ about Z.
std::array<Vector, 3> ResolveEulerAxes(const SpatialConfiguration& Spatial)
{
    const double β = Spatial.OrientationDegrees[1] / DegreesPerRadian, γ = Spatial.OrientationDegrees[2] / DegreesPerRadian;
    const double Cβ = std::cos(β), Sβ = std::sin(β), Cγ = std::cos(γ), Sγ = std::sin(γ);
    return {Vector{Cβ * Cγ, Cβ * Sγ, -Sβ}, Vector{-Sγ, Cγ, 0.0}, Vector{0.0, 0.0, 1.0}};
}

std::array<Vector, 3> ResolveHandleAxes(PlacementGizmoMode Mode, const SpatialConfiguration& Spatial)
{
    if (Mode == PlacementGizmoMode::Scale) return ResolveObjectAxes(Spatial);
    if (Mode == PlacementGizmoMode::Rotate) return ResolveEulerAxes(Spatial);
    return {Vector{1.0, 0.0, 0.0}, Vector{0.0, 1.0, 0.0}, Vector{0.0, 0.0, 1.0}};
}

float SegmentDistance(ImVec2 Point, ImVec2 From, ImVec2 To)
{
    const float Dx = To.x - From.x, Dy = To.y - From.y;
    const float Span = Dx * Dx + Dy * Dy;
    const float T    = Span > 1.0e-6f ? std::clamp(((Point.x - From.x) * Dx + (Point.y - From.y) * Dy) / Span, 0.0f, 1.0f) : 0.0f;
    const float Ex = From.x + T * Dx - Point.x, Ey = From.y + T * Dy - Point.y;
    return std::sqrt(Ex * Ex + Ey * Ey);
}

// 📝 A ring of Radius about Axis through Centre, as screen points (the ones behind the camera break it).
std::vector<std::vector<ImVec2>> ProjectRing(const PlacementView& View, const Vector& Centre, const Vector& Axis, double Radius)
{
    const Vector Reference = std::abs(Axis[2]) < 0.9 ? Vector{0.0, 0.0, 1.0} : Vector{1.0, 0.0, 0.0};
    const Vector First     = Normalize(Cross(Axis, Reference));
    const Vector Second    = Cross(Axis, First);
    std::vector<std::vector<ImVec2>> Runs(1);
    for (int Step = 0; Step <= 72; ++Step)
    {
        const double Angle = 2.0 * Pi * Step / 72.0;
        const Vector Point = Add(Centre, Add(Multiply(First, Radius * std::cos(Angle)), Multiply(Second, Radius * std::sin(Angle))));
        ImVec2       Screen;
        if (View.Project(Point, Screen)) Runs.back().push_back(Screen);
        else if (!Runs.back().empty()) Runs.emplace_back();
    }
    return Runs;
}

// 📝 The parameter along the line Centre + s·Axis nearest the ray (Origin, Direction); false when they run parallel.
bool ResolveAxisParameter(const Vector& Centre, const Vector& Axis, const Vector& Origin, const Vector& Direction, double& OutParameter)
{
    const Vector Offset = Subtract(Centre, Origin);
    const double Facing = Dot(Axis, Direction);
    const double Denominator = 1.0 - Facing * Facing;
    if (Denominator < 1.0e-5) return false;
    OutParameter = (Facing * Dot(Direction, Offset) - Dot(Axis, Offset)) / Denominator;
    return true;
}

double WrapDegrees(double Degrees)
{
    Degrees = std::fmod(Degrees + 180.0, 360.0);
    if (Degrees < 0.0) Degrees += 360.0;
    return Degrees - 180.0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEW
//------------------------------------------------------------------------------------------------------------------------

PlacementView PlacementView::Resolve(const std::array<double, 3>& Eye, const std::array<double, 3>& Forward, const std::array<double, 3>& Up,
                                     double VerticalFieldOfView, ImVec2 StageMin, ImVec2 StageExtent) noexcept
{
    PlacementView View;
    View.Eye                 = Eye;
    View.Forward             = Normalize(Forward);
    View.Right               = Normalize(Cross(View.Forward, Up));
    View.Up                  = Cross(View.Right, View.Forward);
    View.VerticalFieldOfView = VerticalFieldOfView;
    View.StageMin            = StageMin;
    View.StageExtent         = StageExtent;
    return View;
}

bool PlacementView::Project(const std::array<double, 3>& World, ImVec2& OutScreen) const noexcept
{
    const Vector Offset = Subtract(World, Eye);
    const double Depth  = Dot(Offset, Forward);
    if (Depth < 0.05) return false;
    const double Tangent = std::tan(0.5 * VerticalFieldOfView);
    const double Aspect  = StageExtent.y > 0.0f ? static_cast<double>(StageExtent.x) / StageExtent.y : 1.0;
    const double Across  = Dot(Offset, Right) / (Depth * Tangent * Aspect);
    const double Upward  = Dot(Offset, Up) / (Depth * Tangent);
    OutScreen = ImVec2(StageMin.x + static_cast<float>((Across + 1.0) * 0.5) * StageExtent.x,
                       StageMin.y + static_cast<float>((1.0 - Upward) * 0.5) * StageExtent.y);
    return true;
}

void PlacementView::ResolveRay(ImVec2 Screen, std::array<double, 3>& OutOrigin, std::array<double, 3>& OutDirection) const noexcept
{
    const double Tangent = std::tan(0.5 * VerticalFieldOfView);
    const double Aspect  = StageExtent.y > 0.0f ? static_cast<double>(StageExtent.x) / StageExtent.y : 1.0;
    const double Across  = 2.0 * (Screen.x - StageMin.x) / std::max(1.0f, StageExtent.x) - 1.0;
    const double Upward  = 1.0 - 2.0 * (Screen.y - StageMin.y) / std::max(1.0f, StageExtent.y);
    OutOrigin            = Eye;
    OutDirection         = Normalize(Add(Forward, Add(Multiply(Right, Across * Tangent * Aspect), Multiply(Up, Upward * Tangent))));
}

double PlacementView::ResolvePixelMetres(const std::array<double, 3>& World) const noexcept
{
    const double Depth = std::max(0.05, Dot(Subtract(World, Eye), Forward));
    return 2.0 * Depth * std::tan(0.5 * VerticalFieldOfView) / std::max(1.0f, StageExtent.y);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HANDLES
//------------------------------------------------------------------------------------------------------------------------

int PlacementGizmo::Probe(const PlacementView& View, PlacementGizmoMode Mode, const SpatialConfiguration& Spatial, ImVec2 Pointer, float Scale) const
{
    const Vector Centre = {Spatial.PositionMetres[0], Spatial.PositionMetres[1], Spatial.PositionMetres[2]};
    ImVec2       CentreScreen;
    if (!View.Project(Centre, CentreScreen)) return -1;
    const double                Length = HandlePixels * Scale * View.ResolvePixelMetres(Centre);
    const std::array<Vector, 3> Axes   = ResolveHandleAxes(Mode, Spatial);
    int                         Best   = -1;
    float                       Nearest = PickPixels * Scale;
    if (Mode == PlacementGizmoMode::Scale)
    {
        const float Dx = Pointer.x - CentreScreen.x, Dy = Pointer.y - CentreScreen.y;
        if (std::max(std::abs(Dx), std::abs(Dy)) <= 9.0f * Scale) return 3;
    }
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float Distance = 1.0e9f;
        if (Mode == PlacementGizmoMode::Rotate)
        {
            for (const auto& Run : ProjectRing(View, Centre, Axes[static_cast<size_t>(Axis)], Length * 0.85))
                for (size_t Index = 1; Index < Run.size(); ++Index) Distance = std::min(Distance, SegmentDistance(Pointer, Run[Index - 1], Run[Index]));
        }
        else
        {
            ImVec2 Tip;
            if (!View.Project(Add(Centre, Multiply(Axes[static_cast<size_t>(Axis)], Length)), Tip)) continue;
            Distance = SegmentDistance(Pointer, CentreScreen, Tip);
        }
        if (Distance < Nearest)
        {
            Nearest = Distance;
            Best    = Axis;
        }
    }
    return Best;
}

PlacementGizmoOutcome PlacementGizmo::Construct(ImDrawList* DrawList, const PlacementView& View, PlacementGizmoMode Mode, SpatialConfiguration& Spatial,
                                                bool Snap, bool PressAllowed, float Scale)
{
    PlacementGizmoOutcome Outcome;
    const ImVec2          Pointer = ImGui::GetIO().MousePos;
    if (Handle_ >= 0 && Mode_ != Mode) Cancel(Spatial);

    // ① press and release
    const int Hover = Handle_ >= 0 ? Handle_ : Probe(View, Mode, Spatial, Pointer, Scale);
    Outcome.Hovered = Hover >= 0;
    const Vector StartCentre = {Start_.PositionMetres[0], Start_.PositionMetres[1], Start_.PositionMetres[2]};
    if (Handle_ < 0 && Hover >= 0 && PressAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Handle_       = Hover;
        Mode_         = Mode;
        Start_        = Spatial;
        PressPointer_ = Pointer;
        Swept_        = 0.0;
        const Vector Centre = {Spatial.PositionMetres[0], Spatial.PositionMetres[1], Spatial.PositionMetres[2]};
        ImVec2       CentreScreen;
        (void)View.Project(Centre, CentreScreen);
        if (Mode == PlacementGizmoMode::Move)
        {
            Vector Origin{}, Direction{};
            View.ResolveRay(Pointer, Origin, Direction);
            if (!ResolveAxisParameter(Centre, ResolveHandleAxes(Mode, Spatial)[static_cast<size_t>(Handle_)], Origin, Direction, PressParameter_))
                PressParameter_ = 0.0;
        }
        else if (Mode == PlacementGizmoMode::Rotate)
        {
            LastAngle_ = std::atan2(-(Pointer.y - CentreScreen.y), Pointer.x - CentreScreen.x);
        }
        else if (Handle_ < 3)
        {
            const Vector Axis = ResolveHandleAxes(Mode, Spatial)[static_cast<size_t>(Handle_)];
            ImVec2       Tip;
            const double Length = HandlePixels * Scale * View.ResolvePixelMetres(Centre);
            (void)View.Project(Add(Centre, Multiply(Axis, Length)), Tip);
            const float Dx = Tip.x - CentreScreen.x, Dy = Tip.y - CentreScreen.y;
            const float Span = std::max(1.0f, std::sqrt(Dx * Dx + Dy * Dy));
            PressParameter_ = ((Pointer.x - CentreScreen.x) * Dx + (Pointer.y - CentreScreen.y) * Dy) / Span;
        }
    }
    else if (Handle_ >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        Handle_           = -1;
        Outcome.Finished  = true;
    }

    // ② the drag
    char Readout[64] = {};
    if (Handle_ >= 0)
    {
        Outcome.Engaged = true;
        const size_t Axis = static_cast<size_t>(std::min(Handle_, 2));
        SpatialConfiguration Next = Start_;
        if (Mode == PlacementGizmoMode::Move)
        {
            const Vector Direction = ResolveHandleAxes(Mode, Start_)[Axis];
            Vector       Origin{}, Ray{};
            View.ResolveRay(Pointer, Origin, Ray);
            double Parameter = 0.0;
            if (ResolveAxisParameter(StartCentre, Direction, Origin, Ray, Parameter))
            {
                double Travel = Parameter - PressParameter_;
                if (Snap) Travel = std::round(Travel / 0.25) * 0.25;
                for (size_t Component = 0; Component < 3; ++Component)
                    Next.PositionMetres[Component] = static_cast<float>(Start_.PositionMetres[Component] + Direction[Component] * Travel);
                std::snprintf(Readout, sizeof(Readout), "%c %+.2f m", "XYZ"[Axis], Travel);
            }
            else Next = Spatial;
        }
        else if (Mode == PlacementGizmoMode::Rotate)
        {
            ImVec2 CentreScreen;
            (void)View.Project(StartCentre, CentreScreen);
            const double Angle = std::atan2(-(Pointer.y - CentreScreen.y), Pointer.x - CentreScreen.x);
            Swept_ += std::remainder(Angle - LastAngle_, 2.0 * Pi);
            LastAngle_ = Angle;
            const Vector Turning = ResolveHandleAxes(Mode, Start_)[Axis];
            const double Facing  = Dot(Turning, Subtract(View.Eye, StartCentre)) >= 0.0 ? 1.0 : -1.0;
            double       Degrees = Swept_ * Facing * DegreesPerRadian;
            if (Snap) Degrees = std::round(Degrees / 15.0) * 15.0;
            Next.OrientationDegrees[Axis] = static_cast<float>(WrapDegrees(Start_.OrientationDegrees[Axis] + Degrees));
            std::snprintf(Readout, sizeof(Readout), "%c %+.1f\xC2\xB0", "XYZ"[Axis], Degrees);
        }
        else
        {
            if (Handle_ == 3)
            {
                double Factor = std::max(0.01, 1.0 + ((Pointer.x - PressPointer_.x) - (Pointer.y - PressPointer_.y)) / (150.0 * Scale));
                for (size_t Component = 0; Component < 3; ++Component)
                {
                    double Value = Start_.Scale[Component] * Factor;
                    if (Snap) Value = std::max(0.1, std::round(Value / 0.1) * 0.1);
                    Next.Scale[Component] = static_cast<float>(Value);
                }
                std::snprintf(Readout, sizeof(Readout), "\xC3\x97 %.2f", Factor);
            }
            else
            {
                ImVec2 CentreScreen, Tip;
                (void)View.Project(StartCentre, CentreScreen);
                const double Length = HandlePixels * Scale * View.ResolvePixelMetres(StartCentre);
                (void)View.Project(Add(StartCentre, Multiply(ResolveHandleAxes(Mode, Start_)[Axis], Length)), Tip);
                const float  Dx = Tip.x - CentreScreen.x, Dy = Tip.y - CentreScreen.y;
                const float  Span   = std::max(1.0f, std::sqrt(Dx * Dx + Dy * Dy));
                const double Along  = ((Pointer.x - CentreScreen.x) * Dx + (Pointer.y - CentreScreen.y) * Dy) / Span;
                const double Factor = std::max(0.01, 1.0 + (Along - PressParameter_) / Span);
                double       Value  = Start_.Scale[Axis] * Factor;
                if (Snap) Value = std::max(0.1, std::round(Value / 0.1) * 0.1);
                Next.Scale[Axis] = static_cast<float>(Value);
                std::snprintf(Readout, sizeof(Readout), "%c \xC3\x97 %.2f", "XYZ"[Axis], Value);
            }
        }
        for (size_t Component = 0; Component < 3; ++Component)
        {
            Outcome.Changed = Outcome.Changed || Next.PositionMetres[Component] != Spatial.PositionMetres[Component] ||
                              Next.OrientationDegrees[Component] != Spatial.OrientationDegrees[Component] ||
                              Next.Scale[Component] != Spatial.Scale[Component];
            Spatial.PositionMetres[Component]     = Next.PositionMetres[Component];
            Spatial.OrientationDegrees[Component] = Next.OrientationDegrees[Component];
            Spatial.Scale[Component]              = Next.Scale[Component];
        }
    }

    // ③ the handles, a constant HandlePixels long on screen
    if (DrawList == nullptr) return Outcome;
    const Vector Centre = {Spatial.PositionMetres[0], Spatial.PositionMetres[1], Spatial.PositionMetres[2]};
    ImVec2       CentreScreen;
    if (!View.Project(Centre, CentreScreen)) return Outcome;
    const double                Length = HandlePixels * Scale * View.ResolvePixelMetres(Centre);
    const std::array<Vector, 3> Axes   = ResolveHandleAxes(Mode, Spatial);
    const float                 Stroke = 2.5f * Scale;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const bool  Lit = Hover == Axis;
        const ImU32 Ink = ResolveAxisInk(Axis, Lit);
        if (Mode == PlacementGizmoMode::Rotate)
        {
            for (const auto& Run : ProjectRing(View, Centre, Axes[static_cast<size_t>(Axis)], Length * 0.85))
                if (Run.size() > 1) DrawList->AddPolyline(Run.data(), static_cast<int>(Run.size()), Ink, ImDrawFlags_None, Lit ? Stroke * 1.4f : Stroke);
            continue;
        }
        ImVec2 Tip;
        if (!View.Project(Add(Centre, Multiply(Axes[static_cast<size_t>(Axis)], Length)), Tip)) continue;
        DrawList->AddLine(CentreScreen, Tip, IM_COL32(0, 0, 0, 110), Stroke + 2.0f * Scale);
        DrawList->AddLine(CentreScreen, Tip, Ink, Lit ? Stroke * 1.3f : Stroke);
        const float Dx = Tip.x - CentreScreen.x, Dy = Tip.y - CentreScreen.y;
        const float Span = std::sqrt(Dx * Dx + Dy * Dy);
        if (Span < 1.0f) continue;
        const ImVec2 Along(Dx / Span, Dy / Span), Across(-Dy / Span, Dx / Span);
        if (Mode == PlacementGizmoMode::Move)
        {
            const float  Head = 13.0f * Scale, Half = 5.5f * Scale;
            const ImVec2 Point(Tip.x + Along.x * Head, Tip.y + Along.y * Head);
            DrawList->AddTriangleFilled(Point, ImVec2(Tip.x + Across.x * Half, Tip.y + Across.y * Half), ImVec2(Tip.x - Across.x * Half, Tip.y - Across.y * Half), Ink);
        }
        else
        {
            const float Half = 5.0f * Scale;
            DrawList->AddRectFilled(ImVec2(Tip.x - Half, Tip.y - Half), ImVec2(Tip.x + Half, Tip.y + Half), Ink, 1.5f * Scale);
        }
    }
    if (Mode == PlacementGizmoMode::Scale)
    {
        const float Half = 7.0f * Scale;
        DrawList->AddRectFilled(ImVec2(CentreScreen.x - Half, CentreScreen.y - Half), ImVec2(CentreScreen.x + Half, CentreScreen.y + Half),
                                ResolveAxisInk(3, Hover == 3), 2.0f * Scale);
    }
    else
    {
        DrawList->AddCircleFilled(CentreScreen, 4.0f * Scale, IM_COL32(236, 236, 236, 235), 16);
    }
    if (Readout[0] != '\0')
    {
        const ImVec2 Text(Pointer.x + 16.0f * Scale, Pointer.y + 10.0f * Scale);
        const ImVec2 Size = ImGui::CalcTextSize(Readout);
        DrawList->AddRectFilled(ImVec2(Text.x - 6.0f * Scale, Text.y - 3.0f * Scale), ImVec2(Text.x + Size.x + 6.0f * Scale, Text.y + Size.y + 3.0f * Scale),
                                IM_COL32(18, 18, 18, 220), 6.0f * Scale);
        DrawList->AddText(Text, IM_COL32(240, 240, 240, 255), Readout);
    }
    return Outcome;
}

void PlacementGizmo::Cancel(SpatialConfiguration& Spatial) noexcept
{
    if (Handle_ < 0) return;
    for (size_t Component = 0; Component < 3; ++Component)
    {
        Spatial.PositionMetres[Component]     = Start_.PositionMetres[Component];
        Spatial.OrientationDegrees[Component] = Start_.OrientationDegrees[Component];
        Spatial.Scale[Component]              = Start_.Scale[Component];
    }
    Handle_ = -1;
}

} // namespace Frontier
