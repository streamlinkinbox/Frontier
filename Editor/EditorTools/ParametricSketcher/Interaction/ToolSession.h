//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/ToolSession.h — Modal tool sequence: points in, geometry out, with preview at every step
//============================================================================================================================================
// A tool is a list of *prompts* ("centre", "radius", "end point"). Each prompt is satisfied by a snapped pointer click
//    or by typed numbers (Blender style: `3`, `2,4`, `r5`, `a45`, `@2,1` relative; Tab jumps between fields). The
//    session owns axis/plane locks, the running preview geometry (rubber band) and finishes by handing a NurbsCurve
//    or surface to a completion callback. Transform tools (G/R/S) reuse the same machine with a different prompt list.
#pragma once

#include "HotkeyChart.h"
#include "SnapResolution.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Frontier
{

enum class ToolChoice : uint8_t
{
    None,
    Line, Polyline, Rectangle, CentreRectangle, Polygon, Slot, Circle, TwoPointCircle, ThreePointCircle,
    Arc, ThreePointArc, Ellipse, Spline, ControlCurve,
    Move, Rotate, Scale,
};

[[nodiscard]] const char* ToolName(ToolChoice Classification) noexcept;
[[nodiscard]] std::optional<ToolChoice> ParseToolChoice(std::string_view Name) noexcept;

enum class AxisLock : uint8_t { None, X, Y, Z, PlaneYZ, PlaneXZ, PlaneXY };
[[nodiscard]] const char* AxisLockName(AxisLock Lock) noexcept;

struct ToolPrompt
{
    std::string Label;                                                                  // [-] "Centre", "Radius", ...
    enum class Want : uint8_t { Point, Distance, Angle, Count } Wants = Want::Point;    // [-] what typed numbers mean here
    bool Optional = false;                                                              // [-] Enter/RMB may finish the tool here
};

struct ToolPreview
{
    std::vector<NurbsCurve>   Curves;                                                   // [-] rubber-band geometry
    std::vector<Vec3>         Points;                                                   // [m] confirmed points
    std::vector<std::string>  Readout;                                                  // [-] "L 3.4142", "∠ 45.0°"
    SnapCandidate             Snap;                                                     // [-] current cursor snap
    Vec3                      Cursor;                                                   // [m] current world position (snapped)
    AxisLock                  Lock = AxisLock::None;                                    // [-]
    std::string               NumericEntry;                                             // [-] text typed so far
};

struct ToolResult
{
    bool                    Completed = false;                                          // [-] false = cancelled
    std::vector<NurbsCurve> Curves;                                                     // [-] produced geometry
    Mat4                    Transform;                                                  // [-] for G/R/S
    std::string             Summary;                                                    // [-] one line for the console
};

class ToolSession
{
public:
    using Completion = std::function<void(const ToolResult&)>;

    struct Context
    {
        const CameraProjection* Camera = nullptr;
        const SceneDocument*    Scene = nullptr;
        const SnapSettings*     Snap = nullptr;
        Workplane               Plane = Workplane::XY();
        uint32_t                Width = 1280, Height = 800;
        Vec3                    SelectionPivot;                                         // [m] for G/R/S
    };

    bool Begin(ToolChoice Classification, const Context& Ctx, Completion OnComplete) noexcept;
    void Cancel() noexcept;
    [[nodiscard]] bool Active() const noexcept { return Classification != ToolChoice::None; }
    [[nodiscard]] ToolChoice CurrentTool() const noexcept { return Classification; }
    [[nodiscard]] const ToolPrompt& CurrentPrompt() const noexcept { return Prompts[std::min(PromptIndex, Prompts.size() - 1)]; }
    [[nodiscard]] size_t CurrentPromptIndex() const noexcept { return PromptIndex; }
    [[nodiscard]] const ToolPreview& Preview() const noexcept { return Live; }
    [[nodiscard]] const Context& CurrentContext() const noexcept { return Ctx; }

    // Feed events; returns true when consumed. Pointer coordinates are pixels.
    bool Receive(const InputEvent& Event) noexcept;

    // Console shortcuts: supply a world point / typed text directly (scripts drive the tool without a pointer).
    bool SupplyPoint(Vec3 P) noexcept;
    bool SupplyText(std::string_view Text) noexcept;                                    // "3", "2,4", "@1,1", "r2.5", "a30", "n6"
    bool Confirm() noexcept;                                                            // Enter / LMB with nothing pending
    void Attach(const Workplane& P) noexcept { Ctx.Plane = P; Rebuild(); }

private:
    void   Rebuild() noexcept;                                                          // recompute preview from confirmed points + cursor
    bool   Advance(Vec3 P) noexcept;                                                    // accept a point for the current prompt
    void   Finish(bool Completed) noexcept;
    Vec3   ApplyLock(Vec3 Anchor, Vec3 P) const noexcept;
    Vec3   PointFromNumbers(const std::vector<double>& Values, bool Relative) const noexcept;
    [[nodiscard]] Deliver<NurbsCurve> Produce(bool Final) const noexcept;               // geometry from Confirmed (+ Cursor when !Final)
    [[nodiscard]] Vec3 Anchor() const noexcept
    {
        if (!Confirmed.empty()) return Confirmed.back();
        return (Classification == ToolChoice::Move || Classification == ToolChoice::Rotate || Classification == ToolChoice::Scale) ? Ctx.SelectionPivot : Ctx.Plane.Origin;
    }

    ToolChoice                Classification = ToolChoice::None;
    Context                 Ctx;
    Completion              OnComplete;
    std::vector<ToolPrompt> Prompts;
    size_t                  PromptIndex = 0;
    std::vector<Vec3>       Confirmed;                                                  // [m]
    std::vector<double>     Scalars;                                                    // [-] typed radius/angle/count per prompt (NaN = none)
    ToolPreview             Live;
    bool                    HavePointer = false;
    double                  PointerX = 0.0, PointerY = 0.0;                             // [px]
    bool                    SnapSuppressed = false;                                     // [-] Ctrl held
    int                     Sides = 6;                                                  // [-] polygon
    int                     Degree = 3;                                                 // [-] spline / control curve
};

} // namespace Frontier
