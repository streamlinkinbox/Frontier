/*==========================================================================================================================================
                                                        FORCEFIELDVISUALIZATION.H
==========================================================================================================================================*/
// 🧩 The force-field slice for the Force Field and Particle editors: turbo heatmap, arrows and tracers from 100 m to 100 km,
//    after the approved prototype (Experimental/ForceFieldVisualization/ForceFieldVisualization.js).

#pragma once

#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

struct EditorTypefaces;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One renderer, three projections of the same horizontal slice (y = SliceHeight, field axes X east, Y up, Z south):
//      Map      top-down, log zoom and pan, screen-constant sampling (the Force Field's main view)
//      Oblique  the slice tilted and turned (orthographic), with source columns standing on it
//      Camera   the scene viewport's perspective camera (z-up world), drawn over the rendered image
enum class FieldViewMode : uint8_t
{
    Map     = 0,
    Oblique = 1,
    Camera  = 2,
};

// 📝 The scene camera for Camera mode, in the editor's z-up world (east, north, up), as WorldVectorView.
struct FieldCamera
{
    std::array<double, 3> Eye{};
    std::array<double, 3> Forward{1, 0, 0};
    std::array<double, 3> Right{0, -1, 0};
    std::array<double, 3> Up{0, 0, 1};
    double                VerticalFieldOfView = 1.0471975511965976;
};

enum class FieldOutlineShape : uint8_t
{
    Circle     = 0,
    Box        = 1,
    Everywhere = 2,
};

// 📝 A source as the slice outlines it (field axes): its handle, reach, column, bearing arrow and track.
struct FieldSourceOutline
{
    int32_t                            Index          = -1;   // the authored source (pick, move, solo)
    double                             X = 0, Y = 0, Z = 0;   // handle
    double                             Radius         = 0.0;
    FieldOutlineShape                  Shape          = FieldOutlineShape::Circle;
    double                             HalfX = 0, HalfZ = 0;
    double                             RotationDegrees = 0.0; // box turn about Y (screen clockwise on the map)
    double                             OutlineX = 0, OutlineZ = 0;
    bool                               OutlineOffset  = false; // the box is centred away from the handle
    double                             InnerRadius    = 0.0;
    double                             Height         = 0.0;   // a column standing on the slice (Oblique)
    double                             BearingDegrees = 0.0;
    bool                               ShowBearing    = false;
    ForceContribution                  Contribution   = ForceContribution::Flow;
    std::string                        Label;
    double                             Active         = 1.0;
    bool                               Selected       = false;
    bool                               Dim            = false;   // another contribution than the one shown
    std::vector<std::array<double, 2>> Track;                   // [X, Z] points
};

struct FieldVisualizationOptions
{
    ForceFieldQuantity              Quantity       = ForceFieldQuantity::Speed;
    ForceContribution               Contribution   = ForceContribution::Flow;
    bool                            AutomaticScale = true;
    float                           FixedScale     = 20.0f;
    bool                            Heatmap        = true;
    bool                            SmoothHeatmap  = true;
    float                           Opacity        = 1.0f;
    bool                            Arrows         = true;
    float                           ArrowSpacing   = 40.0f;   // [px]
    bool                            Tracers        = true;
    uint32_t                        TracerCount    = 360;
    double                          SliceHeight    = 10.0;    // [m] field Y of the slice
    float                           CellPixels     = 6.0f;
    uint32_t                        CameraCells    = 48;
    bool                            ShowScaleBar   = true;
    bool                            ShowLegend     = true;
    bool                            ShowSources    = true;
    bool                            ShowLabels     = true;
    bool                            ShowCascades   = false;
    bool                            ShowGroundGrid = true;
    std::vector<ForceCascadeLevel>  Cascades;
    std::array<double, 2>           CascadeFocus{0.0, 0.0};
    std::vector<FieldSourceOutline> Sources;
    bool                            HasExtent      = false;
    std::array<double, 4>           Extent{-50000.0, -50000.0, 50000.0, 50000.0};   // MinimumX, MinimumZ, MaximumX, MaximumZ
    bool                            ClipToExtent   = false;
    bool                            HasProbe       = false;
    std::array<double, 2>           ProbePoint{};
    ImU32                           Background     = IM_COL32(5, 11, 18, 255);
    double                          RefreshSeconds = 0.1;
    bool                            Animated       = true;
    int32_t                         Solo           = ForceFieldSpace::kSoloEvery;
    std::string                     LegendTitle;
    double                          MinimumSpan    = 20.0;
    double                          MaximumSpan    = 250000.0;
};

struct FieldViewPlacement
{
    FieldViewMode Mode    = FieldViewMode::Map;
    double        CentreX = 0.0;
    double        CentreZ = 0.0;
    double        Span    = 2000.0;    // [m] across the width
    double        Yaw     = 0.7853981633974483;
    double        Pitch   = 0.62;
    FieldCamera   Camera{};
};

struct FieldStatistics
{
    uint32_t Samples      = 0;
    double   Milliseconds = 0.0;
    double   CellSpan     = 1.0;
    double   Scale        = 1.0;
    double   Peak         = 0.0;
    uint32_t Columns      = 0;
    uint32_t Rows         = 0;
    double   ArrowSpan    = 1.0;
    uint32_t Refreshes    = 0;
};

// 📝 An exact reading at a point of the slice, with ω and ∇·v from four samples half a cell away.
struct FieldProbeReading
{
    bool          Valid = false;
    double        X = 0, Y = 0, Z = 0;
    SpatialVector Vector{};
    double        Speed = 0, Horizontal = 0, Bearing = 0, Vorticity = 0, Divergence = 0, CellSpan = 1;
    const char*   Unit = "m/s";
};

// 📝 What the pointer did this frame: a source picked or dragged, the view moved.
struct FieldInteraction
{
    int32_t               PickedSource = -1;
    int32_t               MovedSource  = -1;
    std::array<double, 2> MovedTo{};      // [X, Z]
    bool                  ViewChanged  = false;
    bool                  Hovering     = false;
    std::array<double, 2> HoverPoint{};   // [X, Z]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VISUALIZATION
//------------------------------------------------------------------------------------------------------------------------

class ForceFieldVisualization
{
  public:
    ForceFieldVisualization();
    ~ForceFieldVisualization();
    ForceFieldVisualization(const ForceFieldVisualization&)            = delete;
    ForceFieldVisualization& operator=(const ForceFieldVisualization&) = delete;

    FieldVisualizationOptions Options;
    FieldViewPlacement        View;

    // 📝 The field the slice samples: a copy, so the lattice can be sampled away from the interface thread while the
    //    editor edits. Revision changes re-sample at once.
    void ConfigureField(const ForceFieldSpace& Field, uint64_t Revision);

    // 📝 One refresh into the rectangle. Time is the field's timeline time; the lattice is re-sampled when the view, the
    //    slice or the field changed, or every RefreshSeconds while time advances.
    void Render(ImDrawList* Ink, const EditorTypefaces& Faces, ImVec2 Minimum, ImVec2 Maximum, double Time, float Scale);

    // 📝 Map / Oblique navigation over the rectangle: drag empty ground to pan, drag a source's handle to move it, right-drag
    //    (or Alt-drag) to turn and tilt the oblique view, wheel to zoom about the cursor, double-click to centre.
    FieldInteraction ConstructNavigation(const char* Id, ImVec2 Minimum, ImVec2 Maximum);

    // 📝 The minimap (the whole extent at 72 × 72 samples, the sources and the view outline) into its own rectangle; a click
    //    or drag centres the view there.
    bool ConstructMinimap(const char* Id, ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, double Time, float Scale);

    void Invalidate() noexcept { Dirty_ = true; }
    // 📝 Waits for the sampling in flight and shows it (captures and tests, where the next refresh must hold the result).
    void SettleSampling();
    void Zoom(double Factor, ImVec2 About);
    void JumpToSpan(double Span);

    [[nodiscard]] const FieldStatistics& QueryStatistics() const noexcept { return Statistics_; }
    [[nodiscard]] FieldProbeReading      ProbeWorld(double X, double Z) const;
    [[nodiscard]] FieldProbeReading      ProbeScreen(ImVec2 Screen) const;
    [[nodiscard]] bool                   ScreenToPlane(ImVec2 Screen, double& OutX, double& OutZ) const;
    [[nodiscard]] bool                   Project(double X, double Y, double Z, ImVec2& OutScreen) const;
    [[nodiscard]] const char*            QueryUnit() const noexcept { return Unit_; }

    // 📝 The outlines of the authored sources the field holds (ConfigureAuthoredSources' order), as the prototype's
    //    DescribeSources: reach, column, bearing, track and the blast's front; Names label them, Selected is an index.
    [[nodiscard]] static std::vector<FieldSourceOutline> DescribeSources(const ForceFieldSpace& Field, const std::vector<std::string>& Names,
                                                                         int32_t Selected, ForceContribution Shown, double TimelineDurationSeconds);

    // 📝 Colour, unit and number formatting the inspector pages and the viewport chrome read too.
    [[nodiscard]] static ImU32       ResolveColour(double Share, float Alpha = 1.0f) noexcept;
    [[nodiscard]] static const char* ResolveUnit(ForceContribution Contribution, ForceFieldQuantity Quantity) noexcept;
    [[nodiscard]] static const char* ResolveQuantityLabel(ForceFieldQuantity Quantity) noexcept;
    [[nodiscard]] static bool        QuerySigned(ForceFieldQuantity Quantity) noexcept;
    static void                      FormatDistance(double Metres, char* Text, size_t Capacity) noexcept;
    static void                      FormatReading(double Reading, char* Text, size_t Capacity) noexcept;
    [[nodiscard]] static double      ResolveNiceLength(double Target) noexcept;

  private:
    struct Lattice
    {
        uint32_t             Columns = 0, Rows = 0;
        double               OriginX = 0, OriginZ = 0, CellSpan = 1, Time = -1;
        std::vector<float>   Vectors;    // 3 per point
        std::vector<float>   Quantity;
        std::vector<uint8_t> Inside;
        std::vector<ImU32>   Colours;
        double               MedianVertical = 0;
        double               Milliseconds   = 0;
    };
    struct LatticeRequest
    {
        double            OriginX = 0, OriginZ = 0, CellSpan = 1, SliceY = 0, Time = 0;
        uint32_t          Columns = 0, Rows = 0;
        ForceContribution Contribution = ForceContribution::Flow;
        int32_t           Solo = ForceFieldSpace::kSoloEvery;
    };
    struct Tracer
    {
        double                 X = 0, Z = 0, Age = 0, Life = 1;
        uint32_t               Count = 0, Head = 0;
        std::array<double, 20> Trail{};
    };
    struct Coverage
    {
        double MinimumX = 0, MinimumZ = 0, MaximumX = 0, MaximumZ = 0;
    };
    struct Affine
    {
        double A = 1, B = 0, C = 0, D = 1, Lift = 0, PixelsPerMetre = 1;
    };

    static std::shared_ptr<Lattice> SampleLattice(std::shared_ptr<const ForceFieldSpace> Field, LatticeRequest Request);

    [[nodiscard]] Affine      ResolveAffine() const noexcept;
    [[nodiscard]] Coverage    ResolveCoverage() const;
    [[nodiscard]] double      ResolveCellSpan() const;
    [[nodiscard]] std::string ResolveStamp() const;
    void   AcceptLattice(std::shared_ptr<Lattice> Finished);
    void   DeriveQuantity(Lattice& Target);
    bool   Interpolate(double X, double Z, double Out[3]) const;
    double ResolveArrowSpan() const;
    void   ReseedTracer(Tracer& Target, const Coverage& Area, bool Scattered);
    void   AdvanceTracers(double Delta);

    void RenderHeatmap(ImDrawList* Ink) const;
    void RenderGroundGrid(ImDrawList* Ink) const;
    void RenderCascades(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const;
    void RenderArrows(ImDrawList* Ink, float Scale);
    void RenderTracers(ImDrawList* Ink, float Scale) const;
    void RenderSources(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const;
    void RenderProbe(ImDrawList* Ink, float Scale) const;
    void RenderLegend(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const;
    void RenderScaleBar(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const;
    void StrokeWorldPath(ImDrawList* Ink, const std::vector<std::array<double, 3>>& Points, bool Close, ImU32 Colour, float Width,
                         float DashOn = 0.0f, float DashOff = 0.0f) const;
    int32_t HitSource(ImVec2 Screen) const;

    std::shared_ptr<const ForceFieldSpace> Field_;
    uint64_t                               FieldRevision_ = ~0ull;
    std::shared_ptr<Lattice>               Lattice_;
    std::future<std::shared_ptr<Lattice>>  Pending_;
    std::string                            PendingStamp_;
    std::string                            LatticeStamp_;
    std::string                            DerivationStamp_;
    std::chrono::steady_clock::time_point  RefreshedAt_{};
    std::chrono::steady_clock::time_point  LastRender_{};
    std::vector<Tracer>                    Tracers_;
    FieldStatistics                        Statistics_{};
    double                                 ScaleReading_ = 0.0;
    const char*                            Unit_         = "m/s";
    bool                                   Dirty_        = true;
    ImVec2                                 Origin_{0, 0};
    float                                  Width_ = 1, Height_ = 1;
    double                                 Time_  = 0.0;
    uint32_t                               RandomSeed_ = 0x9E3779B9u;

    // 📝 Navigation (Map and Oblique).
    enum class DragMode : uint8_t { None, Pan, Turn, Source };
    DragMode              Drag_ = DragMode::None;
    int32_t               DragSource_ = -1;
    std::array<double, 2> DragOffset_{};
    std::array<double, 2> DragPlane_{};
    std::array<double, 2> DragCentre_{};
    ImVec2                DragScreen_{};

    // 📝 The minimap's own samples (refreshed at most every 400 ms).
    std::vector<ImU32>                    MinimapColours_;
    std::chrono::steady_clock::time_point MinimapAt_{};
    std::string                           MinimapStamp_;
};

} // namespace Frontier
