/*====================================================================================================================================
                                                     DRAWINGVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Drawings D1–D2 (priority 11): orthographic views, exact silhouettes, ray-cast hidden lines, section views with hatching;
//    drawing sheets (layout, automatic dimensions, title block, cutting line) and their SVG / DXF exports read back — every value
//    against a closed form

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Blend/BlendCatalogue.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Drawing/DrawingProjection.h"
#include "Engine/GeometryProcessing/Drawing/DrawingExport.h"
#include "Engine/GeometryProcessing/Drawing/DrawingSheet.h"
#include "Engine/GeometryProcessing/Geometry/CurveGeometry.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi = std::numbers::pi;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - DR1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally BlockGate      = { "DR1", "A block 4 × 2.5 × 1 in the six principal views and the isometric: visible length = both coincident outlines (2 × perimeter), nothing hidden; isometric: the three edges at the far corner (0, 2.5, 0) hidden, 9 visible, every edge foreshortened by √(2/3); view box = the projected extent", "≤ 10⁻¹²" };
    GateTally HoleGate       = { "DR2", "The block minus a through hole r 0.3 at (3, 1.25): top view — both hole circles visible, every point at r from the centre, angular cover 2 × 2π; front / right view — exactly two hidden silhouettes of length 1 at x = 2.7, 3.3 / y = 0.95, 1.55, total hidden = 2", "≤ 10⁻¹²" };
    GateTally SilhouetteGate = { "DR3", "Silhouettes: a cylinder r 0.5 (front: two visible lines at x = ±0.5 of length 2; top: none), a sphere r 1 in front, top and isometric views (every point at radius 1, angular cover 2π), the cylinder in the isometric view (two lines at view x = ±0.5)", "≤ 10⁻¹² · cover ≤ 10⁻⁶" };
    GateTally OcclusionGate  = { "DR4", "Occlusion between bodies: block B [1, 4] × [3, 4] × [0.5, 1.5] behind block A [0, 2] × [0, 1] × [0, 1] in the front view — hidden length 3 (bottom x ∈ [1, 2], left z ∈ [0.5, 1], front and back), visible 25, every hidden stroke exactly (1, 0.5)→(2, 0.5) or (1, 0.5)→(1, 1); hidden lines switched off drops them all", "≤ 10⁻¹²" };
    GateTally SectionGate    = { "DR5", "Section view through the hole axis (plane y = 1.25 toward the front viewer): cap area 4 × 1 − 0.6 × 1 = 3.4, every hatch end on the cap boundary, every hatch midpoint inside, hatch at 45° and the requested spacing, the section outline closed, the source partition untouched", "≤ 10⁻¹²" };
    GateTally TangentGate    = { "DR6", "Tangent edges: a box edge filleted r 0.3 — exactly its two run-out edges flagged tangent (a chamfer: none), switched off they vanish; the right view along the fillet shows its quarter-circle caps at r 0.3 about (0.3, 1.7) and no silhouette on the fillet", "≤ 10⁻¹² · count exact" };
    GateTally RefuseGate     = { "DR7", "Refusals name the cause: no bodies, a view frame that is not orthonormal, a left-handed frame, a section plane that misses every body, a section plane with every body in front of it", "named" };
    GateTally TimingGate     = { "DR8", "Projection time of every view above (block, hole, sphere, occlusion, section, fillet) and composition time of every sheet below", "≤ 5 s per view / sheet" };
    GateTally LayoutGate     = { "DR9", "Sheet layout of the bracket on A4, third and first angle: Top shares Front's sheet x mapping and Right its y mapping exactly, placed per ISO 5456-2; every model corner at origin + 20 × view coordinates; footprints inside the drawing area and 12 mm apart; 20:1 is the largest standard scale that fits (50:1 refused); ISO 216 paper and ISO 5455 scale labels", "≤ 10⁻¹² mm" };
    GateTally DimensionGate  = { "DR10", "Automatic dimensions: bracket 4, 1 (front), 2.5, 3, 1.25, ⌀0.6 (top) — value, feature points, dimension line = 20 × value, arrow tips on the line ends; no value box touching another value, a view, the title block or the frame; centre marks r + 2 mm; a fillet's R0.3 in the right view; a cylinder's ⌀1 in the top view", "≤ 2 × 10⁻¹¹ mm" };
    GateTally TitleGate      = { "DR11", "Frame (20 mm binding edge, 10 mm elsewhere), ISO 7200 title block 180 × 36 mm in the lower-right corner with title, number, revision, scale, paper, company; ISO 5456-2 projection symbol: circles r 2 / 4 left of the cone (third angle) or right of it (first angle), cone ends 4 / 8 mm, inside its cell", "≤ 10⁻¹² mm" };
    GateTally SheetSectionGate = { "DR12", "Section A-A on the sheet: the cutting-plane chain across the top view at model y = 1.25, 5 mm past the outline, 7 mm arrows toward the kept material (+y), letters A, label SECTION A-A; hatching at 45° and 3 mm on paper; cap area 1360 mm²", "≤ 10⁻¹² mm · spacing ≤ 10⁻⁹" };
    GateTally SvgGate        = { "DR13", "SVG export read back by an independent parser: header (A4 in mm), ten layer groups in order with ISO 128 widths and dashes, every polyline, arrowhead and text (content, position, size, rotation) — XML escaping included", "≤ 5 × 10⁻⁷ mm (half the 6th decimal)" };
    GateTally DxfGate        = { "DR14", "DXF R12 export read back by an independent group-code parser: AC1009 header, LTYPE / LAYER / STYLE tables, every LINE / POLYLINE / SOLID / TEXT in drawing order, ⌀ as %%c; ExportDrawingFile writes the same bytes", "≤ 10⁻¹² mm" };
    GateTally SheetRefuseGate = { "DR15", "Sheet refusals name the cause: no bodies, a zero model unit, a negative scale, a scale too large for the paper, a part too large even at 1:1000, a section plane that misses, exporting a refused sheet, an unknown file extension", "named" };

    std::vector<GateTally*> AllGates() { return { &BlockGate, &HoleGate, &SilhouetteGate, &OcclusionGate, &SectionGate, &TangentGate, &RefuseGate, &TimingGate, &LayoutGate, &DimensionGate, &TitleGate, &SheetSectionGate, &SvgGate, &DxfGate, &SheetRefuseGate }; }

    struct DetailRow
    {
        std::string Gate            = {};
        std::string Name            = {};
        std::string Detail          = {};
        bool        PassedCondition = false;
    };
    std::vector<DetailRow> Details;
    std::string            SvgFolder;

    void Report(GateTally& Gate, const std::string& Name, const std::string& Detail, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
        Details.push_back({ Gate.Code, Name, Detail, PassedCondition });
        std::fprintf(stderr, "%s %s %s · %s\n", PassedCondition ? "🟢" : "🔴", Gate.Code, Name.c_str(), Detail.c_str());
    }

    std::string Format(const char* Pattern, double First, double Second = 0.0, double Third = 0.0)
    {
        char Text[512];
        std::snprintf(Text, sizeof(Text), Pattern, First, Second, Third);
        return Text;
    }

    void Timing(const std::string& Name, const DrawingView& View)
    {
        Report(TimingGate, Name, Format("%.1f ms, %.0f strokes, %.0f rays", View.Milliseconds, static_cast<double>(View.Strokes.size()), static_cast<double>(View.VisibilityProbes)), View.Milliseconds / 1000.0, View.Milliseconds <= 5000.0);
    }

    // Σ stroke lengths of one visibility (optionally one kind).
    double SumLength(const DrawingView& View, DrawingVisibility Visibility, std::optional<DrawingStrokeKind> Kind = {})
    {
        double Total = 0.0;
        for (const DrawingStroke& Stroke : View.Strokes)
            if (Stroke.Visibility == Visibility && (!Kind || Stroke.Kind == *Kind) && Stroke.Kind != DrawingStrokeKind::Hatch) Total += MeasureStrokeLength(Stroke);
        return Total;
    }

    // Angle swept by strokes about a centre (sum of the angle increments along each polyline).
    double AngularCover(const std::vector<const DrawingStroke*>& Strokes, Vector2 Centre)
    {
        double Cover = 0.0;
        for (const DrawingStroke* Stroke : Strokes)
            for (size_t Index = 1; Index < Stroke->Points.size(); ++Index)
            {
                const double First  = std::atan2(Stroke->Points[Index - 1].Y - Centre.Y, Stroke->Points[Index - 1].X - Centre.X);
                const double Second = std::atan2(Stroke->Points[Index].Y - Centre.Y, Stroke->Points[Index].X - Centre.X);
                double Turn = Second - First;
                while (Turn > Pi) Turn -= 2.0 * Pi;
                while (Turn < -Pi) Turn += 2.0 * Pi;
                Cover += std::fabs(Turn);
            }
        return Cover;
    }

    double RadiusError(const std::vector<const DrawingStroke*>& Strokes, Vector2 Centre, double Radius)
    {
        double Worst = 0.0;
        for (const DrawingStroke* Stroke : Strokes)
            for (const Vector2& Point : Stroke->Points) Worst = std::max(Worst, std::fabs(std::hypot(Point.X - Centre.X, Point.Y - Centre.Y) - Radius));
        return Worst;
    }

    std::vector<const DrawingStroke*> Select(const DrawingView& View, const std::function<bool(const DrawingStroke&)>& Filter)
    {
        std::vector<const DrawingStroke*> Chosen;
        for (const DrawingStroke& Stroke : View.Strokes) if (Filter(Stroke)) Chosen.push_back(&Stroke);
        return Chosen;
    }

    // Strokes whose every point lies within Precision of the circle about Centre.
    std::vector<const DrawingStroke*> OnCircle(const DrawingView& View, Vector2 Centre, double Radius, double Precision)
    {
        return Select(View, [&](const DrawingStroke& Stroke)
        {
            if (Stroke.Kind == DrawingStrokeKind::Hatch) return false;
            for (const Vector2& Point : Stroke.Points) if (std::fabs(std::hypot(Point.X - Centre.X, Point.Y - Centre.Y) - Radius) > Precision) return false;
            return true;
        });
    }

    // 📝 Evidence: visible strokes solid, hidden dashed, silhouettes blue, section outlines heavy, hatch thin.
    void WriteSvg(const std::string& Name, const DrawingView& View)
    {
        if (SvgFolder.empty() || View.Strokes.empty()) return;
        const std::string Path = SvgFolder + "/" + Name + ".svg";
        std::FILE* File = std::fopen(Path.c_str(), "w");
        if (!File) return;
        const double Width = View.High.X - View.Low.X, Height = View.High.Y - View.Low.Y;
        const double Pad   = 0.08 * std::max(Width, Height) + 1.0e-3;
        const double Scale = 520.0 / std::max(Width + 2.0 * Pad, Height + 2.0 * Pad);
        const double PixelsX = (Width + 2.0 * Pad) * Scale, PixelsY = (Height + 2.0 * Pad) * Scale;
        std::fprintf(File, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n", PixelsX, PixelsY + 24.0, PixelsX, PixelsY + 24.0);
        std::fprintf(File, "<rect width=\"100%%\" height=\"100%%\" fill=\"#fbfbf8\"/>\n<text x=\"8\" y=\"17\" font-family=\"monospace\" font-size=\"13\" fill=\"#333\">%s · %zu strokes · %.0f ms</text>\n", Name.c_str(), View.Strokes.size(), View.Milliseconds);
        for (int Layer = 0; Layer < 3; ++Layer)                                              // Hatch, hidden, visible
        {
            for (const DrawingStroke& Stroke : View.Strokes)
            {
                const bool Hatch = Stroke.Kind == DrawingStrokeKind::Hatch, Hidden = Stroke.Visibility == DrawingVisibility::Hidden;
                if ((Layer == 0) != Hatch || (!Hatch && (Layer == 1) != Hidden)) continue;
                const char* Colour = Hatch ? "#8a8a8a" : Stroke.Kind == DrawingStrokeKind::Silhouette ? "#1f5fbf" : Stroke.Kind == DrawingStrokeKind::TangentEdge ? "#6c8f3a" : "#111";
                const double Weight = Hatch ? 0.7 : Stroke.Kind == DrawingStrokeKind::SectionOutline ? 2.6 : Hidden ? 1.0 : 1.6;
                std::fprintf(File, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"%.1f\" stroke-linecap=\"round\"%s points=\"", Colour, Weight, Hidden ? " stroke-dasharray=\"6 4\"" : "");
                for (const Vector2& Point : Stroke.Points) std::fprintf(File, "%.2f,%.2f ", (Point.X - View.Low.X + Pad) * Scale, 24.0 + (View.High.Y - Point.Y + Pad) * Scale);
                std::fprintf(File, "\"/>\n");
            }
        }
        std::fprintf(File, "</svg>\n");
        std::fclose(File);
    }

    DrawingViewRequest Standard(DrawingStandardView Kind)
    {
        DrawingViewRequest Request;
        Request.Frame = StandardDrawingFrame(Kind);
        return Request;
    }

    // The bracket: block 4 × 2.5 × 1 minus a through hole r 0.3 at (3, 1.25) (the drill longer than the block).
    BodyToken ConstructBracket(ModelPartition& Partition)
    {
        const BodyToken Block = ConstructBlock(Partition, {}, { 4.0, 2.5, 1.0 }).Value;
        const BodyToken Drill = ConstructCylinder(Partition, ConstructFrame({ 3.0, 1.25, -0.5 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.3, 2.0).Value;
        return ComputeBoolean(Partition, Block, Drill, BooleanOperation::Difference).Body;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                GATES
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateBlock()
    {
        ModelPartition  Partition;
        const BodyToken Block = ConstructBlock(Partition, {}, { 4.0, 2.5, 1.0 }).Value;
        struct Expectation { DrawingStandardView Kind; double Perimeter; Vector2 Low, High; };
        const Expectation Views[] = {
            { DrawingStandardView::Front, 10.0, { 0.0, 0.0 }, { 4.0, 1.0 } },   { DrawingStandardView::Back, 10.0, { -4.0, 0.0 }, { 0.0, 1.0 } },
            { DrawingStandardView::Top, 13.0, { 0.0, 0.0 }, { 4.0, 2.5 } },     { DrawingStandardView::Bottom, 13.0, { 0.0, -2.5 }, { 4.0, 0.0 } },
            { DrawingStandardView::Right, 7.0, { 0.0, 0.0 }, { 2.5, 1.0 } },    { DrawingStandardView::Left, 7.0, { -2.5, 0.0 }, { 0.0, 1.0 } },
        };
        for (const Expectation& Expected : Views)
        {
            const DrawingView View = ProjectDrawingView(Partition, { Block }, Standard(Expected.Kind));
            const double Visible = SumLength(View, DrawingVisibility::Visible), Hidden = SumLength(View, DrawingVisibility::Hidden);
            const double Box = std::max({ std::fabs(View.Low.X - Expected.Low.X), std::fabs(View.Low.Y - Expected.Low.Y), std::fabs(View.High.X - Expected.High.X), std::fabs(View.High.Y - Expected.High.Y) });
            const double Error = std::max({ std::fabs(Visible - 2.0 * Expected.Perimeter), Hidden, Box });
            Report(BlockGate, std::string("Block · ") + DrawingStandardViewLabel(Expected.Kind), Format("visible %.15g (2 × %.4g), hidden %.3g", Visible, Expected.Perimeter, Hidden) + Format(", view box error %.2e", Box), Error, View.Refusal.empty() && Error <= 1.0e-12);
            Timing(std::string("Block · ") + DrawingStandardViewLabel(Expected.Kind), View);
            if (Expected.Kind == DrawingStandardView::Front) WriteSvg("BlockFront", View);
        }
        const DrawingView View = ProjectDrawingView(Partition, { Block }, Standard(DrawingStandardView::Isometric));
        const double Foreshorten = std::sqrt(2.0 / 3.0);
        const double Visible = SumLength(View, DrawingVisibility::Visible), Hidden = SumLength(View, DrawingVisibility::Hidden);
        const double Error = std::max(std::fabs(Visible - 3.0 * 7.5 * Foreshorten), std::fabs(Hidden - 7.5 * Foreshorten));
        std::set<uint32_t> HiddenEdges;
        for (const DrawingStroke& Stroke : View.Strokes) if (Stroke.Visibility == DrawingVisibility::Hidden) HiddenEdges.insert(Stroke.Source);
        Report(BlockGate, "Block · Isometric", Format("visible %.15g (22.5·√⅔), hidden %.15g (7.5·√⅔), hidden edges %.0f", Visible, Hidden, static_cast<double>(HiddenEdges.size())), Error, Error <= 1.0e-12 && HiddenEdges.size() == 3);
        Timing("Block · Isometric", View);
        WriteSvg("BlockIsometric", View);
    }

    void ValidateHole()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        const DrawingView Top = ProjectDrawingView(Partition, { Bracket }, Standard(DrawingStandardView::Top));
        const std::vector<const DrawingStroke*> Circles = OnCircle(Top, { 3.0, 1.25 }, 0.3, 1.0e-9);
        double Hidden = SumLength(Top, DrawingVisibility::Hidden);
        bool   AllVisible = true;
        for (const DrawingStroke* Stroke : Circles) AllVisible = AllVisible && Stroke->Visibility == DrawingVisibility::Visible;
        const double Radius = RadiusError(Circles, { 3.0, 1.25 }, 0.3), Cover = AngularCover(Circles, { 3.0, 1.25 });
        const double Silhouettes = static_cast<double>(Select(Top, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Silhouette; }).size());
        Report(HoleGate, "Top view · hole circles", Format("radius error %.2e, cover %.15g (4π = %.15g)", Radius, Cover, 4.0 * Pi) + Format(", hidden %.3g, silhouettes %.0f", Hidden, Silhouettes),
               std::max({ Radius, std::fabs(Cover - 4.0 * Pi), Hidden }), AllVisible && Radius <= 1.0e-12 && std::fabs(Cover - 4.0 * Pi) <= 1.0e-12 && Hidden == 0.0 && Silhouettes == 0.0);
        Timing("Bracket · Top", Top);
        WriteSvg("BracketTop", Top);

        struct Side { DrawingStandardView Kind; double First, Second; const char* Name; };
        for (const Side& Expected : { Side{ DrawingStandardView::Front, 2.7, 3.3, "Front view · hidden hole silhouettes" }, Side{ DrawingStandardView::Right, 0.95, 1.55, "Right view · hidden hole silhouettes" } })
        {
            const DrawingView View = ProjectDrawingView(Partition, { Bracket }, Standard(Expected.Kind));
            const std::vector<const DrawingStroke*> Lines = Select(View, [](const DrawingStroke& Stroke) { return Stroke.Visibility == DrawingVisibility::Hidden; });
            double Error = std::fabs(SumLength(View, DrawingVisibility::Hidden) - 2.0);
            bool   Kinds = Lines.size() == 2;
            std::set<long> Places;
            for (const DrawingStroke* Line : Lines)
            {
                Kinds = Kinds && Line->Kind == DrawingStrokeKind::Silhouette;
                for (const Vector2& Point : Line->Points)
                {
                    const double Near = std::min(std::fabs(Point.X - Expected.First), std::fabs(Point.X - Expected.Second));
                    Error = std::max(Error, Near);
                }
                Places.insert(std::lround(Line->Points.front().X * 100.0));
                Error = std::max({ Error, std::fabs(MeasureStrokeLength(*Line) - 1.0) });
            }
            Report(HoleGate, Expected.Name, Format("%.0f hidden silhouettes, total %.15g, worst offset from x = {%.4g, ", static_cast<double>(Lines.size()), SumLength(View, DrawingVisibility::Hidden), Expected.First) + Format("%.4g} %.2e", Expected.Second, Error),
                   Error, Kinds && Places.size() == 2 && Error <= 1.0e-12);
            Timing(std::string("Bracket · ") + DrawingStandardViewLabel(Expected.Kind), View);
            WriteSvg(std::string("Bracket") + DrawingStandardViewLabel(Expected.Kind), View);
        }
        const DrawingView Isometric = ProjectDrawingView(Partition, { Bracket }, Standard(DrawingStandardView::Isometric));
        Report(HoleGate, "Isometric view · projects", Format("%.0f strokes, %.0f hidden", static_cast<double>(Isometric.Strokes.size()), static_cast<double>(Select(Isometric, [](const DrawingStroke& Stroke) { return Stroke.Visibility == DrawingVisibility::Hidden; }).size())), 0.0, Isometric.Refusal.empty() && !Isometric.Strokes.empty());
        Timing("Bracket · Isometric", Isometric);
        WriteSvg("BracketIsometric", Isometric);
    }

    void ValidateSilhouettes()
    {
        ModelPartition  Partition;
        const BodyToken Cylinder = ConstructCylinder(Partition, ConstructFrame({}, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.5, 2.0).Value;
        {
            const DrawingView View = ProjectDrawingView(Partition, { Cylinder }, Standard(DrawingStandardView::Front));
            const std::vector<const DrawingStroke*> Lines = Select(View, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Silhouette; });
            double Error = 0.0;
            std::set<long> Sides;
            bool Visible = Lines.size() == 2;
            for (const DrawingStroke* Line : Lines)
            {
                Visible = Visible && Line->Visibility == DrawingVisibility::Visible;
                for (const Vector2& Point : Line->Points) Error = std::max(Error, std::fabs(std::fabs(Point.X) - 0.5));
                Error = std::max(Error, std::fabs(MeasureStrokeLength(*Line) - 2.0));
                Sides.insert(Line->Points.front().X > 0.0 ? 1 : -1);
            }
            Report(SilhouetteGate, "Cylinder r 0.5 · front", Format("%.0f silhouettes, worst |x| − 0.5 / length − 2 = %.2e", static_cast<double>(Lines.size()), Error), Error, Visible && Sides.size() == 2 && Error <= 1.0e-12);
            Timing("Cylinder · Front", View);
            WriteSvg("CylinderFront", View);
        }
        {
            const DrawingView View = ProjectDrawingView(Partition, { Cylinder }, Standard(DrawingStandardView::Top));
            const double Silhouettes = static_cast<double>(Select(View, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Silhouette; }).size());
            const std::vector<const DrawingStroke*> Circles = OnCircle(View, {}, 0.5, 1.0e-9);
            const double Cover = AngularCover(Circles, {});
            Report(SilhouetteGate, "Cylinder r 0.5 · top (seen along its axis)", Format("%.0f silhouettes, rims cover %.15g (4π)", Silhouettes, Cover), std::fabs(Cover - 4.0 * Pi), Silhouettes == 0.0 && std::fabs(Cover - 4.0 * Pi) <= 1.0e-12);
        }
        {
            const DrawingView View = ProjectDrawingView(Partition, { Cylinder }, Standard(DrawingStandardView::Isometric));
            const std::vector<const DrawingStroke*> Lines = Select(View, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Silhouette; });
            double Error = 0.0;
            std::set<long> Sides;
            for (const DrawingStroke* Line : Lines)
            {
                for (const Vector2& Point : Line->Points) Error = std::max(Error, std::fabs(std::fabs(Point.X) - 0.5));
                Sides.insert(Line->Points.front().X > 0.0 ? 1 : -1);
            }
            Report(SilhouetteGate, "Cylinder r 0.5 · isometric", Format("%.0f silhouettes, worst | |x| − 0.5 | = %.2e", static_cast<double>(Lines.size()), Error), Error, Lines.size() == 2 && Sides.size() == 2 && Error <= 1.0e-12);
            Timing("Cylinder · Isometric", View);
            WriteSvg("CylinderIsometric", View);
        }
        const BodyToken Sphere = ConstructSphere(Partition, ConstructFrame({ 5.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 1.0).Value;
        for (DrawingStandardView Kind : { DrawingStandardView::Front, DrawingStandardView::Top, DrawingStandardView::Isometric })
        {
            const DrawingView View   = ProjectDrawingView(Partition, { Sphere }, Standard(Kind));
            const Vector2     Centre = ProjectDrawingPoint(StandardDrawingFrame(Kind), { 5.0, 0.0, 0.0 });
            const std::vector<const DrawingStroke*> Lines = Select(View, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Silhouette; });
            const double Radius = RadiusError(Lines, Centre, 1.0), Cover = AngularCover(Lines, Centre);
            bool Visible = !Lines.empty();
            for (const DrawingStroke* Line : Lines) Visible = Visible && Line->Visibility == DrawingVisibility::Visible;
            Report(SilhouetteGate, std::string("Sphere r 1 · ") + DrawingStandardViewLabel(Kind), Format("%.0f silhouette runs, radius error %.2e, cover %.12g (2π)", static_cast<double>(Lines.size()), Radius, Cover), std::max(Radius, std::fabs(Cover - 2.0 * Pi) * 1.0e-6),
                   Visible && Radius <= 1.0e-12 && std::fabs(Cover - 2.0 * Pi) <= 1.0e-6);
            Timing(std::string("Sphere · ") + DrawingStandardViewLabel(Kind), View);
            if (Kind == DrawingStandardView::Isometric) WriteSvg("SphereIsometric", View);
        }
    }

    void ValidateOcclusion()
    {
        ModelPartition  Partition;
        const BodyToken Front = ConstructBlock(Partition, {}, { 2.0, 1.0, 1.0 }).Value;
        const BodyToken Back  = ConstructBlock(Partition, { 1.0, 3.0, 0.5 }, { 3.0, 1.0, 1.0 }).Value;
        DrawingViewRequest Request = Standard(DrawingStandardView::Front);
        const DrawingView View = ProjectDrawingView(Partition, { Front, Back }, Request);
        const double Visible = SumLength(View, DrawingVisibility::Visible), Hidden = SumLength(View, DrawingVisibility::Hidden);
        double Ends = 0.0;
        bool   Owners = true;
        for (const DrawingStroke& Stroke : View.Strokes)
        {
            if (Stroke.Visibility != DrawingVisibility::Hidden) continue;
            Owners = Owners && Stroke.Body == Back;
            // Exactly one of the two hidden pieces: bottom (1, 0.5) → (2, 0.5) or left (1, 0.5) → (1, 1), either direction.
            const Vector2 First = Stroke.Points.front(), Last = Stroke.Points.back();
            double Near = 1.0e300;
            for (const Vector2 Far : { Vector2{ 2.0, 0.5 }, Vector2{ 1.0, 1.0 } })
            {
                const double Forward  = std::max(std::hypot(First.X - 1.0, First.Y - 0.5), std::hypot(Last.X - Far.X, Last.Y - Far.Y));
                const double Backward = std::max(std::hypot(Last.X - 1.0, Last.Y - 0.5), std::hypot(First.X - Far.X, First.Y - Far.Y));
                Near = std::min({ Near, Forward, Backward });
            }
            Ends = std::max(Ends, Near);
        }
        const double Error = std::max({ std::fabs(Visible - 25.0), std::fabs(Hidden - 3.0), Ends });
        Report(OcclusionGate, "Block behind block · front", Format("visible %.15g (25), hidden %.15g (3), worst hidden piece off (1, 0.5)→(2, 0.5) / (1, 0.5)→(1, 1) by %.2e", Visible, Hidden, Ends), Error, Owners && Error <= 1.0e-12);
        Timing("Occlusion · Front", View);
        WriteSvg("OcclusionFront", View);
        Request.HiddenLinesEnabled = false;
        const DrawingView Plain = ProjectDrawingView(Partition, { Front, Back }, Request);
        Report(OcclusionGate, "Hidden lines off", Format("hidden %.3g, visible %.15g (25)", SumLength(Plain, DrawingVisibility::Hidden), SumLength(Plain, DrawingVisibility::Visible)), std::fabs(SumLength(Plain, DrawingVisibility::Visible) - 25.0),
               SumLength(Plain, DrawingVisibility::Hidden) == 0.0 && std::fabs(SumLength(Plain, DrawingVisibility::Visible) - 25.0) <= 1.0e-12);
    }

    void ValidateSection()
    {
        ModelPartition  Partition;
        const BodyToken Bracket   = ConstructBracket(Partition);
        const size_t    BodiesBefore = Partition.Bodies.OccupiedCount(), FacesBefore = Partition.Faces.OccupiedCount();
        DrawingViewRequest Request = Standard(DrawingStandardView::Front);
        Request.Section      = OrthonormalFrame{ { 0.0, 1.25, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        Request.HatchSpacing = 0.1;
        const DrawingView View = ProjectDrawingView(Partition, { Bracket }, Request);
        Report(SectionGate, "Cap area", Format("%.15g (3.4) over %.0f caps", View.SectionArea, static_cast<double>(View.SectionCaps)), std::fabs(View.SectionArea - 3.4), View.Refusal.empty() && std::fabs(View.SectionArea - 3.4) <= 1.0e-12);

        // The cap: x ∈ [0, 2.7] ∪ [3.3, 4], z ∈ [0, 1] in front-view coordinates.
        const auto Boundary = [](Vector2 Point)
        {
            const double Across = std::min({ std::fabs(Point.X), std::fabs(Point.X - 2.7), std::fabs(Point.X - 3.3), std::fabs(Point.X - 4.0) });
            const double Up     = std::min(std::fabs(Point.Y), std::fabs(Point.Y - 1.0));
            return std::min(Across, Up);
        };
        const auto Inside = [](Vector2 Point) { return Point.Y > 0.0 && Point.Y < 1.0 && ((Point.X > 0.0 && Point.X < 2.7) || (Point.X > 3.3 && Point.X < 4.0)); };
        double EndError = 0.0, AngleError = 0.0, SpacingError = 0.0;
        bool   Middles = true;
        std::vector<double> Offsets;
        const std::vector<const DrawingStroke*> Hatch = Select(View, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Hatch; });
        for (const DrawingStroke* Line : Hatch)
        {
            const Vector2 Start = Line->Points.front(), End = Line->Points.back();
            EndError   = std::max({ EndError, Boundary(Start), Boundary(End) });
            Middles    = Middles && Inside({ 0.5 * (Start.X + End.X), 0.5 * (Start.Y + End.Y) });
            AngleError = std::max(AngleError, std::fabs(std::fabs((End.Y - Start.Y) / (End.X - Start.X)) - 1.0));
            Offsets.push_back((Start.Y - Start.X) / std::sqrt(2.0));                         // n·p with n = (−sin 45°, cos 45°)
        }
        std::sort(Offsets.begin(), Offsets.end());
        Offsets.erase(std::unique(Offsets.begin(), Offsets.end(), [](double First, double Second) { return Second - First < 1.0e-9; }), Offsets.end());
        for (const double Offset : Offsets) SpacingError = std::max(SpacingError, std::fabs(Offset / 0.1 - 0.5 - std::round(Offset / 0.1 - 0.5)) * 0.1);
        Report(SectionGate, "Hatching", Format("%.0f hatch lines, worst end off the cap boundary %.2e, worst slope error %.2e", static_cast<double>(Hatch.size()), EndError, AngleError) + Format(", spacing error %.2e, ", SpacingError) + (Middles ? "midpoints inside" : "a midpoint outside"),
               std::max({ EndError, AngleError, SpacingError }), !Hatch.empty() && Middles && EndError <= 1.0e-12 && AngleError <= 1.0e-12 && SpacingError <= 1.0e-12);

        // The section outline: the cap boundary, every point on it, total = 2·(2.7 + 1) + 2·(0.7 + 1) = 10.8.
        const std::vector<const DrawingStroke*> Outline = Select(View, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::SectionOutline; });
        double Off = 0.0, Total = 0.0;
        for (const DrawingStroke* Line : Outline)
        {
            Total += MeasureStrokeLength(*Line);
            for (const Vector2& Point : Line->Points) Off = std::max(Off, Boundary(Point));
        }
        Report(SectionGate, "Section outline", Format("%.0f strokes, length %.15g (10.8), worst point off the boundary %.2e", static_cast<double>(Outline.size()), Total, Off), std::max(Off, std::fabs(Total - 10.8)), Off <= 1.0e-12 && std::fabs(Total - 10.8) <= 1.0e-12);
        Report(SectionGate, "Source partition untouched", Format("bodies %.0f → %.0f, faces %.0f → ", static_cast<double>(BodiesBefore), static_cast<double>(Partition.Bodies.OccupiedCount()), static_cast<double>(FacesBefore)) + Format("%.0f", static_cast<double>(Partition.Faces.OccupiedCount())), 0.0,
               BodiesBefore == Partition.Bodies.OccupiedCount() && FacesBefore == Partition.Faces.OccupiedCount());
        Timing("Bracket · Section A-A", View);
        WriteSvg("BracketSection", View);
    }

    void ValidateTangent()
    {
        for (const char* Name : { "BoxEdgeFillet", "BoxEdgeChamfer" })
        {
            ModelPartition      Partition;
            const BlendSpecimen Specimen = ConstructBlendSpecimen(Partition, Name);
            const BlendOutcome  Blended  = BlendEdges(Partition, Specimen.Body, Specimen.Edges, Specimen.Settings);
            if (!Blended.Refusal.empty()) { Report(TangentGate, Name, "blend refused: " + Blended.Refusal, 1.0, false); continue; }
            const bool Fillet = std::string(Name) == "BoxEdgeFillet";
            DrawingViewRequest Request = Standard(DrawingStandardView::Isometric);
            const DrawingView View = ProjectDrawingView(Partition, { Blended.Body }, Request);
            std::set<uint32_t> Tangent;
            for (const DrawingStroke& Stroke : View.Strokes) if (Stroke.Kind == DrawingStrokeKind::TangentEdge) Tangent.insert(Stroke.Source);
            // Run-outs of the fillet along x: y = 0.3 on the top (z = 2) and z = 1.7 on the front (y = 0).
            double RunOut = 0.0;
            for (uint32_t Slot : Tangent)
            {
                const EdgeProfile& Edge = Partition.Edges.Resolve(Partition.Edges.TokenAt(Slot));
                const CurveGeometry& Curve = Partition.Curves.Resolve(Edge.Curve);
                for (double Parameter : { Edge.IntervalLow, Edge.IntervalHigh })
                {
                    const Vector3 Point = EvaluateCurve(Curve, Parameter).Position;
                    RunOut = std::max(RunOut, std::min(std::hypot(Point.Y - 0.3, Point.Z - 2.0), std::hypot(Point.Y, Point.Z - 1.7)));
                }
            }
            Request.TangentEdgesEnabled = false;
            const DrawingView Plain = ProjectDrawingView(Partition, { Blended.Body }, Request);
            const double PlainTangent = static_cast<double>(Select(Plain, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::TangentEdge; }).size());
            const size_t Expected = Fillet ? 2 : 0;
            Report(TangentGate, std::string(Name) + " · isometric", Format("%.0f tangent edges (%.0f expected), run-out ends off y = 0.3 / z = 1.7 by %.2e", static_cast<double>(Tangent.size()), static_cast<double>(Expected), RunOut) + Format(", %.0f left when switched off", PlainTangent), RunOut,
                   Tangent.size() == Expected && RunOut <= 1.0e-12 && PlainTangent == 0.0);
            Timing(std::string(Name) + " · Isometric", View);
            WriteSvg(std::string(Name) + "Isometric", View);
            if (!Fillet) continue;
            const DrawingView Right = ProjectDrawingView(Partition, { Blended.Body }, Standard(DrawingStandardView::Right));
            const std::vector<const DrawingStroke*> Caps = OnCircle(Right, { 0.3, 1.7 }, 0.3, 1.0e-9);
            const double Cover = AngularCover(Caps, { 0.3, 1.7 }) / static_cast<double>(std::max<size_t>(1, Caps.size()));
            const double Silhouettes = static_cast<double>(Select(Right, [](const DrawingStroke& Stroke) { return Stroke.Kind == DrawingStrokeKind::Silhouette; }).size());
            Report(TangentGate, "BoxEdgeFillet · right view caps", Format("%.0f quarter arcs, radius error %.2e, mean cover %.15g (π/2)", static_cast<double>(Caps.size()), RadiusError(Caps, { 0.3, 1.7 }, 0.3), Cover) + Format(", %.0f silhouettes", Silhouettes),
                   std::max(RadiusError(Caps, { 0.3, 1.7 }, 0.3), std::fabs(Cover - 0.5 * Pi)), !Caps.empty() && RadiusError(Caps, { 0.3, 1.7 }, 0.3) <= 1.0e-12 && std::fabs(Cover - 0.5 * Pi) <= 1.0e-12 && Silhouettes == 0.0);
            WriteSvg("BoxEdgeFilletRight", Right);
        }
    }

    void ValidateRefusals()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        const auto Expect = [&](const std::string& Name, const DrawingView& View, const char* Fragment)
        {
            Report(RefuseGate, Name, View.Refusal.empty() ? std::string("not refused") : View.Refusal, 0.0, View.Refusal.find(Fragment) != std::string::npos);
        };
        Expect("No bodies", ProjectDrawingView(Partition, {}, Standard(DrawingStandardView::Top)), "no bodies");
        DrawingViewRequest Skewed = Standard(DrawingStandardView::Top);
        Skewed.Frame.XAxis = { 1.0, 0.1, 0.0 };
        Expect("Skewed frame", ProjectDrawingView(Partition, { Bracket }, Skewed), "not a right-handed orthonormal");
        DrawingViewRequest Mirrored = Standard(DrawingStandardView::Top);
        Mirrored.Frame.ZAxis = { 0.0, 0.0, -1.0 };
        Expect("Left-handed frame", ProjectDrawingView(Partition, { Bracket }, Mirrored), "not a right-handed orthonormal");
        DrawingViewRequest Beyond = Standard(DrawingStandardView::Front);
        Beyond.Section = OrthonormalFrame{ { 0.0, 10.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        Expect("Plane beyond the body", ProjectDrawingView(Partition, { Bracket }, Beyond), "misses every body");
        DrawingViewRequest Before = Standard(DrawingStandardView::Front);
        Before.Section = OrthonormalFrame{ { 0.0, -5.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        Expect("Plane in front of the body", ProjectDrawingView(Partition, { Bracket }, Before), "misses every body");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SHEET GATES (D2)
    //--------------------------------------------------------------------------------------------------------------------

    constexpr double SheetScale = 20.0;                                                      // [-] - The bracket's expected A4 scale

    DrawingSheet ComposeBracketSheet(ModelPartition& Partition, BodyToken Bracket, DrawingProjectionAngle Angle, bool SectionEnabled, double Scale = 0.0)
    {
        DrawingSheetRequest Request;
        Request.Paper       = DrawingPaperSize::A4;
        Request.Angle       = Angle;
        Request.Scale       = Scale;
        Request.Title.Title = "Bracket";
        if (SectionEnabled) Request.Section = OrthonormalFrame{ { 0.0, 1.25, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        return ComposeDrawingSheet(Partition, { Bracket }, Request);
    }

    const PlacedDrawingView* FindView(const DrawingSheet& Sheet, const std::string& Label)
    {
        for (const PlacedDrawingView& View : Sheet.Views) if (View.Label == Label) return &View;
        return nullptr;
    }

    const char* AngleLabel(DrawingProjectionAngle Angle) { return Angle == DrawingProjectionAngle::Third ? "Third angle" : "First angle"; }

    // Clear distance between two boxes (negative when they overlap).
    double Separation(Vector2 FirstLow, Vector2 FirstHigh, Vector2 SecondLow, Vector2 SecondHigh)
    {
        return std::max({ SecondLow.X - FirstHigh.X, FirstLow.X - SecondHigh.X, SecondLow.Y - FirstHigh.Y, FirstLow.Y - SecondHigh.Y });
    }

    // Distance from Target to the nearest polyline point on Layer.
    double NearestPoint(const DrawingSheet& Sheet, SheetLayer Layer, Vector2 Target)
    {
        double Best = 1.0e300;
        for (const SheetPolyline& Line : Sheet.Lines)
        {
            if (Line.Layer != Layer) continue;
            for (const Vector2& Point : Line.Points) Best = std::min(Best, std::hypot(Point.X - Target.X, Point.Y - Target.Y));
        }
        return Best;
    }

    void SaveSheet(const std::string& Name, const DrawingSheet& Sheet)
    {
        if (SvgFolder.empty() || !Sheet.Refusal.empty()) return;
        for (const char* Suffix : { ".svg", ".dxf" })
        {
            const std::string Failure = ExportDrawingFile(Sheet, SvgFolder + "/" + Name + Suffix);
            if (!Failure.empty()) std::fprintf(stderr, "%s\n", Failure.c_str());
        }
    }

    void SheetTiming(const std::string& Name, const DrawingSheet& Sheet)
    {
        Report(TimingGate, "Sheet · " + Name, Format("%.1f ms, %.0f lines, %.0f texts", Sheet.Milliseconds, static_cast<double>(Sheet.Lines.size()), static_cast<double>(Sheet.Texts.size())), Sheet.Milliseconds / 1000.0, Sheet.Milliseconds <= 5000.0);
    }

    void ValidateLayout()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        for (const DrawingProjectionAngle Angle : { DrawingProjectionAngle::Third, DrawingProjectionAngle::First })
        {
            const DrawingSheet Sheet = ComposeBracketSheet(Partition, Bracket, Angle, false);
            const std::string  Name  = AngleLabel(Angle);
            SheetTiming(Name + " bracket A4", Sheet);
            const PlacedDrawingView* Front = FindView(Sheet, "Front");
            const PlacedDrawingView* Top   = FindView(Sheet, "Top");
            const PlacedDrawingView* Right = FindView(Sheet, "Right");
            const PlacedDrawingView* Iso   = FindView(Sheet, "Isometric");
            if (!Sheet.Refusal.empty() || !Front || !Top || !Right || !Iso) { Report(LayoutGate, Name, "refused or a view missing: " + Sheet.Refusal, 1.0, false); continue; }

            const bool Third = Angle == DrawingProjectionAngle::Third;
            const bool Placement = Third ? Top->Low.Y > Front->High.Y && Right->Low.X > Front->High.X : Top->High.Y < Front->Low.Y && Right->High.X < Front->Low.X;
            Report(LayoutGate, Name + " · projection alignment", Format("Top x origin − Front x origin = %.3g, Right y origin − Front y origin = %.3g, ", Top->Origin.X - Front->Origin.X, Right->Origin.Y - Front->Origin.Y) +
                   (Third ? "Top above Front, Right on its right" : "Top below Front, Right on its left") + (Placement ? "" : " — VIOLATED"), 0.0,
                   Top->Origin.X == Front->Origin.X && Right->Origin.Y == Front->Origin.Y && Placement);

            // Every model corner lands at Origin + 20 · (view coordinates); view boxes are 20 × the extents.
            struct Corner { const PlacedDrawingView* View; double X, Y; };
            double MapError = 0.0;
            for (const Corner& Expected : { Corner{ Front, 0.0, 0.0 }, Corner{ Front, 4.0, 1.0 }, Corner{ Top, 0.0, 0.0 }, Corner{ Top, 4.0, 2.5 }, Corner{ Right, 0.0, 0.0 }, Corner{ Right, 2.5, 1.0 } })
            {
                MapError = std::max(MapError, NearestPoint(Sheet, SheetLayer::Visible, { Expected.View->Origin.X + SheetScale * Expected.X, Expected.View->Origin.Y + SheetScale * Expected.Y }));
            }
            struct Extent { const PlacedDrawingView* View; double Width, Height; };
            for (const Extent& Expected : { Extent{ Front, 4.0, 1.0 }, Extent{ Top, 4.0, 2.5 }, Extent{ Right, 2.5, 1.0 } })
            {
                MapError = std::max({ MapError, std::fabs(Expected.View->High.X - Expected.View->Low.X - SheetScale * Expected.Width), std::fabs(Expected.View->High.Y - Expected.View->Low.Y - SheetScale * Expected.Height) });
            }
            Report(LayoutGate, Name + " · sheet mapping", "scale " + Sheet.ScaleLabel + Format(", worst corner / box error %.2e mm", MapError), MapError, Sheet.Scale == SheetScale && Sheet.ScaleLabel == "20:1" && MapError <= 1.0e-12);

            double Inside = 1.0e300, Gap = 1.0e300;
            for (size_t First = 0; First < Sheet.Views.size(); ++First)
            {
                const PlacedDrawingView& View = Sheet.Views[First];
                Inside = std::min({ Inside, View.FootprintLow.X - Sheet.AreaLow.X, View.FootprintLow.Y - Sheet.AreaLow.Y, Sheet.AreaHigh.X - View.FootprintHigh.X, Sheet.AreaHigh.Y - View.FootprintHigh.Y });
                for (size_t Second = First + 1; Second < Sheet.Views.size(); ++Second)
                {
                    Gap = std::min(Gap, Separation(View.FootprintLow, View.FootprintHigh, Sheet.Views[Second].FootprintLow, Sheet.Views[Second].FootprintHigh));
                }
            }
            Report(LayoutGate, Name + " · footprints", Format("nearest footprint to the area edge %.4g mm (≥ 0), nearest pair %.4g mm (≥ 12)", Inside, Gap), std::max(0.0, -Inside), Inside >= -1.0e-9 && Gap >= 12.0 - 1.0e-9);

            const DrawingSheet Larger = ComposeBracketSheet(Partition, Bracket, Angle, false, 50.0);
            Report(LayoutGate, Name + " · largest standard scale", "20:1 chosen; 50:1 requested → " + (Larger.Refusal.empty() ? std::string("not refused") : Larger.Refusal), 0.0,
                   Larger.Refusal.find("do not fit on A4 at 50:1") != std::string::npos);
            if (Third) SaveSheet("BracketSheetThirdAngle", Sheet);
        }

        double PaperError = 0.0;
        const double Expected[5][2] = { { 297.0, 210.0 }, { 420.0, 297.0 }, { 594.0, 420.0 }, { 841.0, 594.0 }, { 1189.0, 841.0 } };
        std::string Labels;
        for (int Index = 0; Index < 5; ++Index)
        {
            const Vector2 Paper = DrawingPaperDimensions(static_cast<DrawingPaperSize>(Index));
            PaperError = std::max({ PaperError, std::fabs(Paper.X - Expected[Index][0]), std::fabs(Paper.Y - Expected[Index][1]) });
            Labels += std::string(Index ? " " : "") + DrawingPaperLabel(static_cast<DrawingPaperSize>(Index));
        }
        const std::string Scales = FormatDrawingScale(50.0) + " " + FormatDrawingScale(1.0) + " " + FormatDrawingScale(0.2) + " " + FormatDrawingScale(0.001);
        Report(LayoutGate, "Paper sizes and scale labels", "ISO 216 landscape " + Labels + Format(", error %.3g mm; ", PaperError) + Scales, PaperError,
               PaperError == 0.0 && Labels == "A4 A3 A2 A1 A0" && Scales == "50:1 1:1 1:5 1:1000" && EnumerateStandardScales().size() == 16);
    }

    void ValidateDimensions()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        for (const DrawingProjectionAngle Angle : { DrawingProjectionAngle::Third, DrawingProjectionAngle::First })
        {
            const DrawingSheet Sheet = ComposeBracketSheet(Partition, Bracket, Angle, false);
            const std::string  Name  = AngleLabel(Angle);
            if (!Sheet.Refusal.empty()) { Report(DimensionGate, Name, "refused: " + Sheet.Refusal, 1.0, false); continue; }
            // Expected: view, kind, value, text, feature span along the measured axis (model, view coordinates).
            struct Expectation { int View; DrawingDimensionKind Kind; double Value; const char* Text; double Low, High; };
            const Expectation Table[] = {
                { 0, DrawingDimensionKind::Horizontal, 4.0,  "4",    0.0, 4.0  },
                { 0, DrawingDimensionKind::Vertical,   1.0,  "1",    0.0, 1.0  },
                { 1, DrawingDimensionKind::Vertical,   2.5,  "2.5",  0.0, 2.5  },
                { 1, DrawingDimensionKind::Horizontal, 3.0,  "3",    0.0, 3.0  },
                { 1, DrawingDimensionKind::Vertical,   1.25, "1.25", 0.0, 1.25 },
                { 1, DrawingDimensionKind::Diameter,   0.6,  "⌀0.6", 3.0, 1.25 },             // Low, High = centre
            };
            for (const Expectation& Expected : Table)
            {
                const DrawingDimension* Found = nullptr;
                int Matches = 0;
                for (const DrawingDimension& Dimension : Sheet.Dimensions) if (Dimension.Text == Expected.Text && Dimension.View == Expected.View && Dimension.Kind == Expected.Kind) { Found = &Dimension; ++Matches; }
                const std::string Check = Name + " · " + Sheet.Views[static_cast<size_t>(Expected.View)].Label + " " + Expected.Text;
                if (!Found || Matches != 1) { Report(DimensionGate, Check, Format("%.0f matching dimensions", Matches), 1.0, false); continue; }
                const Vector2 Origin = Sheet.Views[static_cast<size_t>(Expected.View)].Origin;
                double Feature = 0.0, Length = 0.0, Arrows = 0.0;
                if (Expected.Kind == DrawingDimensionKind::Diameter)
                {
                    Feature = std::hypot(Found->First.X - (Origin.X + SheetScale * Expected.Low), Found->First.Y - (Origin.Y + SheetScale * Expected.High));
                    Length  = std::fabs(std::hypot(Found->Second.X - Found->First.X, Found->Second.Y - Found->First.Y) - SheetScale * 0.5 * Expected.Value);
                    Arrows  = 1.0e300;
                    for (const SheetSolid& Solid : Sheet.Solids) Arrows = std::min(Arrows, std::hypot(Solid.Corners[0].X - Found->Second.X, Solid.Corners[0].Y - Found->Second.Y));
                }
                else
                {
                    const bool   Horizontal = Expected.Kind == DrawingDimensionKind::Horizontal;
                    const double Base = Horizontal ? Origin.X : Origin.Y;
                    const double First = Horizontal ? Found->First.X : Found->First.Y, Second = Horizontal ? Found->Second.X : Found->Second.Y;
                    Feature = std::max(std::fabs(std::min(First, Second) - (Base + SheetScale * Expected.Low)), std::fabs(std::max(First, Second) - (Base + SheetScale * Expected.High)));
                    Length  = std::fabs(std::hypot(Found->LineEnd.X - Found->LineStart.X, Found->LineEnd.Y - Found->LineStart.Y) - SheetScale * Expected.Value);
                    for (const Vector2 Tip : { Found->LineStart, Found->LineEnd })
                    {
                        double Nearest = 1.0e300;
                        for (const SheetSolid& Solid : Sheet.Solids) Nearest = std::min(Nearest, std::hypot(Solid.Corners[0].X - Tip.X, Solid.Corners[0].Y - Tip.Y));
                        Arrows = std::max(Arrows, Nearest);
                    }
                }
                const double ValueError = std::fabs(Found->Value - Expected.Value);
                const double Worst      = std::max({ ValueError, Feature, Length, Arrows });
                Report(DimensionGate, Check, Format("value %.17g (error %.2e), ", Found->Value, ValueError) + Format("feature points off by %.2e mm, line − 20 × value %.2e mm, ", Feature, Length) + Format("arrow tips off by %.2e mm", Arrows),
                       Worst, Worst <= 1.0e-12 * SheetScale);
            }
            // Values do not collide with each other, with any view's strokes or with the title block, and stay in the frame.
            double Clear = 1.0e300;
            std::vector<std::pair<Vector2, Vector2>> Obstacles = { { Sheet.TitleLow, Sheet.TitleHigh } };
            for (const PlacedDrawingView& View : Sheet.Views) Obstacles.push_back({ View.Low, View.High });
            for (size_t First = 0; First < Sheet.Dimensions.size(); ++First)
            {
                const DrawingDimension& Dimension = Sheet.Dimensions[First];
                for (const auto& [Low, High] : Obstacles) Clear = std::min(Clear, Separation(Dimension.TextLow, Dimension.TextHigh, Low, High));
                for (size_t Second = First + 1; Second < Sheet.Dimensions.size(); ++Second) Clear = std::min(Clear, Separation(Dimension.TextLow, Dimension.TextHigh, Sheet.Dimensions[Second].TextLow, Sheet.Dimensions[Second].TextHigh));
                Clear = std::min({ Clear, Dimension.TextLow.X - Sheet.BorderLow.X, Dimension.TextLow.Y - Sheet.BorderLow.Y, Sheet.BorderHigh.X - Dimension.TextHigh.X, Sheet.BorderHigh.Y - Dimension.TextHigh.Y });
            }
            double Escape = 0.0;
            for (const SheetPolyline& Line : Sheet.Lines)
            {
                for (const Vector2& Point : Line.Points) Escape = std::max({ Escape, Sheet.BorderLow.X - Point.X, Sheet.BorderLow.Y - Point.Y, Point.X - Sheet.BorderHigh.X, Point.Y - Sheet.BorderHigh.Y });
            }
            Report(DimensionGate, Name + " · no collisions", Format("%.0f dimensions, nearest value box to another value / view / title block / frame %.4g mm (> 0), lines past the frame %.2e mm",
                   static_cast<double>(Sheet.Dimensions.size()), Clear, Escape), std::max(0.0, -Clear), Sheet.Dimensions.size() == 6 && Clear > 0.0 && Escape <= 0.0);
            double Marks = 0.0;
            for (const SheetPolyline& Line : Sheet.Lines)
            {
                if (Line.Layer != SheetLayer::Centre || Line.Points.size() != 2) continue;
                const double Span = std::hypot(Line.Points[1].X - Line.Points[0].X, Line.Points[1].Y - Line.Points[0].Y);
                if (std::fabs(Span - 2.0 * (SheetScale * 0.3 + 2.0)) <= 1.0e-9) Marks += 1.0;
            }
            Report(DimensionGate, Name + " · centre mark", Format("%.0f centre-mark arms of 2 × (r + 2 mm) = 16 mm", Marks), 0.0, Marks == 2.0);
        }

        // A fillet's radius and a cylinder's diameter.
        {
            ModelPartition      Blended;
            const BlendSpecimen Specimen = ConstructBlendSpecimen(Blended, "BoxEdgeFillet");
            const BlendOutcome  Outcome  = BlendEdges(Blended, Specimen.Body, Specimen.Edges, Specimen.Settings);
            DrawingSheetRequest Request;
            Request.Paper       = DrawingPaperSize::A4;
            Request.Title.Title = "Box edge fillet";
            const DrawingSheet Sheet = ComposeDrawingSheet(Blended, { Outcome.Body }, Request);
            SheetTiming("Fillet A4", Sheet);
            const DrawingDimension* Radius = nullptr;
            for (const DrawingDimension& Dimension : Sheet.Dimensions) if (Dimension.Kind == DrawingDimensionKind::Radius) Radius = &Dimension;
            double Error = 1.0;
            if (Radius && !Sheet.Views.empty())
            {
                const Vector2 Origin = Sheet.Views[static_cast<size_t>(Radius->View)].Origin;
                Error = std::max({ std::fabs(Radius->Value - 0.3), std::hypot(Radius->First.X - (Origin.X + Sheet.Scale * 0.3), Radius->First.Y - (Origin.Y + Sheet.Scale * 1.7)) / Sheet.Scale,
                                   std::fabs(std::hypot(Radius->Second.X - Radius->First.X, Radius->Second.Y - Radius->First.Y) - Sheet.Scale * 0.3) / Sheet.Scale });
            }
            Report(DimensionGate, "BoxEdgeFillet · R", Radius ? Radius->Text + " in the " + Sheet.Views[static_cast<size_t>(Radius->View)].Label + " view, scale " + Sheet.ScaleLabel + Format(", value / centre (0.3, 1.7) / tip error %.2e", Error) : std::string("no radius dimension: ") + Sheet.Refusal,
                   Error, Radius && Radius->Text == "R0.3" && Sheet.Views[static_cast<size_t>(Radius->View)].Label == "Right" && Error <= 1.0e-12);
            SaveSheet("FilletSheet", Sheet);
        }
        {
            ModelPartition  Round;
            const BodyToken Cylinder = ConstructCylinder(Round, ConstructFrame({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.5, 2.0).Value;
            DrawingSheetRequest Request;
            Request.Paper       = DrawingPaperSize::A4;
            Request.Title.Title = "Cylinder";
            const DrawingSheet Sheet = ComposeDrawingSheet(Round, { Cylinder }, Request);
            SheetTiming("Cylinder A4", Sheet);
            std::string Texts;
            const DrawingDimension* Diameter = nullptr;
            for (const DrawingDimension& Dimension : Sheet.Dimensions)
            {
                Texts += (Texts.empty() ? "" : " ") + Sheet.Views[static_cast<size_t>(Dimension.View)].Label + " " + Dimension.Text;
                if (Dimension.Kind == DrawingDimensionKind::Diameter) Diameter = &Dimension;
            }
            const double Error = Diameter ? std::fabs(Diameter->Value - 1.0) : 1.0;
            Report(DimensionGate, "Cylinder · ⌀ alone sizes the end view", Texts + " (no repeated size or position), scale " + Sheet.ScaleLabel + Format(", ⌀ error %.2e", Error), Error,
                   Diameter && Diameter->Text == "⌀1" && Sheet.Views[static_cast<size_t>(Diameter->View)].Label == "Top" && Error <= 1.0e-12 && Texts == "Front 1 Front 2 Top ⌀1");
            SaveSheet("CylinderSheet", Sheet);
        }
    }

    void ValidateTitleBlock()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        for (const DrawingProjectionAngle Angle : { DrawingProjectionAngle::Third, DrawingProjectionAngle::First })
        {
            const DrawingSheet Sheet = ComposeBracketSheet(Partition, Bracket, Angle, false);
            const std::string  Name  = AngleLabel(Angle);
            const double Frame = std::max({ std::fabs(Sheet.BorderLow.X - 20.0), std::fabs(Sheet.BorderLow.Y - 10.0), std::fabs(Sheet.BorderHigh.X - 287.0), std::fabs(Sheet.BorderHigh.Y - 200.0),
                                            std::fabs(Sheet.TitleHigh.X - Sheet.BorderHigh.X), std::fabs(Sheet.TitleLow.Y - Sheet.BorderLow.Y),
                                            std::fabs(Sheet.TitleHigh.X - Sheet.TitleLow.X - 180.0), std::fabs(Sheet.TitleHigh.Y - Sheet.TitleLow.Y - 36.0) });
            std::set<std::string> Contents;
            for (const SheetText& Text : Sheet.Texts) if (Text.Layer == SheetLayer::Title) Contents.insert(Text.Content);
            const char* Caption = Angle == DrawingProjectionAngle::Third ? "THIRD ANGLE" : "FIRST ANGLE";
            bool Fields = true;
            for (const char* Field : { "Bracket", "DWG-0001", "A", "20:1", "A4 mm", "Frontier", "TITLE", "SCALE", "PAPER", Caption }) Fields = Fields && Contents.count(Field) == 1;
            Report(TitleGate, Name + " · frame and title block", Format("frame 20 / 10 mm, title block 180 × 36 mm in the lower-right corner, error %.2e mm; ", Frame) + (Fields ? "title, number, revision, scale 20:1, A4 mm, company, " + std::string(Caption) : std::string("a field is missing")),
                   Frame, Frame == 0.0 && Fields);

            // Symbol: two concentric circles r 2 / 4 and a trapezoid 8 long, small end (height 4) on the left.
            std::vector<std::pair<Vector2, double>> Circles;
            double CircleError = 0.0, ConeLow = 0.0, ConeHigh = 0.0, SmallEnd = 0.0, LargeEnd = 0.0;
            int Cones = 0;
            for (const SheetPolyline& Line : Sheet.Lines)
            {
                if (Line.Layer != SheetLayer::Title) continue;
                if (Line.Points.size() == 73)
                {
                    Vector2 Centre = {};
                    for (size_t Index = 0; Index + 1 < Line.Points.size(); ++Index) { Centre.X += Line.Points[Index].X / 72.0; Centre.Y += Line.Points[Index].Y / 72.0; }
                    const double Radius = std::hypot(Line.Points[0].X - Centre.X, Line.Points[0].Y - Centre.Y);
                    for (const Vector2& Point : Line.Points) CircleError = std::max(CircleError, std::fabs(std::hypot(Point.X - Centre.X, Point.Y - Centre.Y) - Radius));
                    Circles.push_back({ Centre, Radius });
                }
                if (Line.Points.size() == 5 && std::fabs(Line.Points[1].X - Line.Points[0].X - 8.0) <= 1.0e-12)
                {
                    ++Cones;
                    ConeLow  = Line.Points[0].X;
                    ConeHigh = Line.Points[1].X;
                    SmallEnd = Line.Points[3].Y - Line.Points[0].Y;
                    LargeEnd = Line.Points[2].Y - Line.Points[1].Y;
                }
            }
            const bool Pair = Circles.size() == 2 && std::hypot(Circles[0].first.X - Circles[1].first.X, Circles[0].first.Y - Circles[1].first.Y) <= 1.0e-12;
            const double Radii = Pair ? std::max(std::fabs(std::min(Circles[0].second, Circles[1].second) - 2.0), std::fabs(std::max(Circles[0].second, Circles[1].second) - 4.0)) : 1.0;
            const double CentreX = Pair ? Circles[0].first.X : 0.0;
            const bool Order = Angle == DrawingProjectionAngle::Third ? CentreX + 4.0 < ConeLow : CentreX - 4.0 > ConeHigh;
            const bool InCell = Pair && ConeLow > Sheet.TitleLow.X + 110.0 && ConeHigh < Sheet.TitleLow.X + 145.0 && CentreX > Sheet.TitleLow.X + 110.0 && CentreX < Sheet.TitleLow.X + 145.0;
            Report(TitleGate, Name + " · projection symbol", Format("circles r 2 / 4 (error %.2e), cone ends %.4g / ", std::max(Radii, CircleError), SmallEnd) + Format("%.4g mm, circles ", LargeEnd) +
                   (Angle == DrawingProjectionAngle::Third ? "left of the cone" : "right of the cone") + (Order ? "" : " — WRONG SIDE") + (InCell ? ", inside its cell" : ", outside its cell"),
                   std::max(Radii, CircleError), Pair && Cones == 1 && Radii <= 1.0e-12 && CircleError <= 1.0e-12 && std::fabs(SmallEnd - 4.0) <= 1.0e-12 && std::fabs(LargeEnd - 8.0) <= 1.0e-12 && Order && InCell);
        }
    }

    void ValidateSheetSection()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        for (const DrawingProjectionAngle Angle : { DrawingProjectionAngle::Third, DrawingProjectionAngle::First })
        {
            const DrawingSheet Sheet = ComposeBracketSheet(Partition, Bracket, Angle, true);
            const std::string  Name  = AngleLabel(Angle);
            SheetTiming(Name + " bracket section A4", Sheet);
            const PlacedDrawingView* Top     = FindView(Sheet, "Top");
            const PlacedDrawingView* Section = FindView(Sheet, "Section A-A");
            if (!Sheet.Refusal.empty() || !Top || !Section) { Report(SheetSectionGate, Name, "refused or a view missing: " + Sheet.Refusal, 1.0, false); continue; }

            // Cutting line: the chain across the top view at model y = 1.25, 5 mm past the outline, arrows of 7 mm toward +y.
            const double Level = Top->Origin.Y + SheetScale * 1.25;
            double LineError = 1.0, ArrowError = 0.0;
            int Arrows = 0, Chains = 0;
            for (const SheetPolyline& Line : Sheet.Lines)
            {
                if (Line.Layer != SheetLayer::CuttingPlane || Line.Points.size() != 2) continue;
                const Vector2 From = Line.Points[0], To = Line.Points[1];
                if (std::fabs(To.Y - From.Y) < 1.0e-9)
                {
                    ++Chains;
                    LineError = std::max({ std::fabs(From.Y - Level), std::fabs(To.Y - Level), std::fabs(std::min(From.X, To.X) - (Top->Low.X - 5.0)), std::fabs(std::max(From.X, To.X) - (Top->High.X + 5.0)) });
                }
                else
                {
                    ++Arrows;
                    ArrowError = std::max({ ArrowError, std::fabs(From.Y - Level), std::fabs(To.X - From.X), std::fabs(To.Y - From.Y - 7.0) });
                }
            }
            int Heads = 0, Letters = 0;
            for (const SheetSolid& Solid : Sheet.Solids) if (Solid.Layer == SheetLayer::CuttingPlane && Solid.Corners[0].Y > Solid.Corners[1].Y && Solid.Corners[0].Y > Solid.Corners[2].Y) ++Heads;
            bool Label = false;
            for (const SheetText& Text : Sheet.Texts) { Letters += Text.Layer == SheetLayer::CuttingPlane && Text.Content == "A"; Label = Label || Text.Content == "SECTION A-A"; }
            const double Worst = std::max(LineError, ArrowError);
            Report(SheetSectionGate, Name + " · cutting line", Format("chain at y = Top origin + 20 × 1.25, 5 mm past the outline: error %.2e mm; %.0f arrows of 7 mm toward +y", LineError, Arrows) + Format(" (error %.2e), %.0f heads up, ", ArrowError, Heads) +
                   Format("%.0f letters A, ", Letters) + (Label ? "label SECTION A-A" : "no label"), Worst, Chains == 1 && Arrows == 2 && Heads == 2 && Letters == 2 && Label && Worst <= 1.0e-12);

            // Hatching: 45°, 3 mm apart on paper; cap area 3.4 model units² (1360 mm²).
            std::vector<double> Offsets;
            double Slope = 0.0;
            for (const SheetPolyline& Line : Sheet.Lines)
            {
                if (Line.Layer != SheetLayer::Hatch || Line.Points.size() != 2) continue;
                const double Length = std::hypot(Line.Points[1].X - Line.Points[0].X, Line.Points[1].Y - Line.Points[0].Y);
                Slope = std::max(Slope, std::fabs(std::fabs((Line.Points[1].X - Line.Points[0].X) / Length) - std::sqrt(0.5)) + std::fabs(std::fabs((Line.Points[1].Y - Line.Points[0].Y) / Length) - std::sqrt(0.5)));
                Offsets.push_back(((Line.Points[0].Y - Section->Origin.Y) - (Line.Points[0].X - Section->Origin.X)) * std::sqrt(0.5));
            }
            std::sort(Offsets.begin(), Offsets.end());
            double Spacing = Offsets.empty() ? 1.0 : 0.0;
            for (size_t Index = 1; Index < Offsets.size(); ++Index)
            {
                const double Step = Offsets[Index] - Offsets[Index - 1];
                if (Step < 1.0e-6) continue;                                                  // Same hatch line split by the hole
                Spacing = std::max(Spacing, std::fabs(Step / 3.0 - std::round(Step / 3.0)) * 3.0);
            }
            const double Area = std::fabs(Section->View.SectionArea * SheetScale * SheetScale - 1360.0);
            Report(SheetSectionGate, Name + " · hatching", Format("%.0f hatch lines, slope error %.2e, spacing off a 3 mm multiple %.2e mm, ", static_cast<double>(Offsets.size()), Slope, Spacing) + Format("cap area %.12g mm² (1360)", Section->View.SectionArea * SheetScale * SheetScale),
                   std::max({ Slope, Spacing, Area }), !Offsets.empty() && Slope <= 1.0e-12 && Spacing <= 1.0e-9 && Area <= 1.0e-9);
            if (Angle == DrawingProjectionAngle::First) SaveSheet("BracketSheetFirstAngleSection", Sheet);
        }
    }

    // 📝 Independent readers: they know the file formats, not the writer's code.
    struct ParsedSheet
    {
        std::vector<std::string>                       Layers;
        std::vector<std::pair<std::string, std::vector<Vector2>>> Lines;                     // Layer, points (sheet 𝑦 up)
        std::vector<std::pair<std::string, std::vector<Vector2>>> Solids;
        struct Text { std::string Layer, Content; Vector2 Position; double Size = 0.0, Angle = 0.0; };
        std::vector<Text>                              Texts;
        std::string                                    Problem;
    };

    std::string Attribute(const std::string& Tag, const std::string& Name)
    {
        const size_t At = Tag.find(" " + Name + "=\"");
        if (At == std::string::npos) return {};
        const size_t Begin = At + Name.size() + 3;
        return Tag.substr(Begin, Tag.find('"', Begin) - Begin);
    }

    std::vector<Vector2> PointList(const std::string& Text, double Height)
    {
        std::vector<Vector2> Points;
        const char* Cursor = Text.c_str();
        while (*Cursor)
        {
            char* End = nullptr;
            const double X = std::strtod(Cursor, &End);
            if (End == Cursor || *End != ',') break;
            const double Y = std::strtod(End + 1, &End);
            Points.push_back({ X, Height - Y });
            Cursor = End;
            while (*Cursor == ' ') ++Cursor;
        }
        return Points;
    }

    ParsedSheet ReadSvg(const std::string& Source, double Height)
    {
        ParsedSheet Parsed;
        size_t Open = 0, Close = 0;
        for (size_t At = Source.find("<g "); At != std::string::npos; At = Source.find("<g ", At + 1)) ++Open;
        for (size_t At = Source.find("</g>"); At != std::string::npos; At = Source.find("</g>", At + 1)) ++Close;
        if (Open != Close || Source.find("<svg ") == std::string::npos || Source.rfind("</svg>\n") != Source.size() - 7) Parsed.Problem = "unbalanced groups or svg element";
        for (size_t At = Source.find("<g "); At != std::string::npos; At = Source.find("<g ", At + 1))
        {
            const size_t TagEnd = Source.find('>', At), End = Source.find("</g>", At);
            const std::string Layer = Attribute(Source.substr(At, TagEnd - At), "id");
            Parsed.Layers.push_back(Layer);
            const std::string Body = Source.substr(TagEnd + 1, End - TagEnd - 1);
            for (size_t Tag = Body.find('<'); Tag != std::string::npos; Tag = Body.find('<', Tag + 1))
            {
                const size_t Stop = Body.find('>', Tag);
                const std::string Element = Body.substr(Tag, Stop - Tag + 1);
                if (Element.rfind("<polyline", 0) == 0) Parsed.Lines.push_back({ Layer, PointList(Attribute(Element, "points"), Height) });
                else if (Element.rfind("<polygon", 0) == 0) Parsed.Solids.push_back({ Layer, PointList(Attribute(Element, "points"), Height) });
                else if (Element.rfind("<text", 0) == 0)
                {
                    ParsedSheet::Text Text;
                    Text.Layer    = Layer;
                    Text.Position = { std::atof(Attribute(Element, "x").c_str()), Height - std::atof(Attribute(Element, "y").c_str()) };
                    Text.Size     = std::atof(Attribute(Element, "font-size").c_str());
                    const std::string Rotation = Attribute(Element, "transform");
                    if (!Rotation.empty()) Text.Angle = -std::atof(Rotation.c_str() + 7);
                    const size_t Finish = Body.find("</text>", Stop);
                    std::string Content = Body.substr(Stop + 1, Finish - Stop - 1);
                    for (const auto& [Entity, Character] : { std::pair{ "&lt;", "<" }, std::pair{ "&gt;", ">" }, std::pair{ "&quot;", "\"" }, std::pair{ "&amp;", "&" } })
                    {
                        for (size_t Found = Content.find(Entity); Found != std::string::npos; Found = Content.find(Entity, Found + 1)) Content.replace(Found, std::strlen(Entity), Character);
                    }
                    Text.Content = Content;
                    Parsed.Texts.push_back(Text);
                    Tag = Finish;
                }
            }
        }
        return Parsed;
    }

    ParsedSheet ReadDxf(const std::string& Source, std::vector<std::pair<int, std::string>>& Pairs)
    {
        ParsedSheet Parsed;
        std::vector<std::string> Rows;
        for (size_t Begin = 0; Begin < Source.size();)
        {
            const size_t End = Source.find('\n', Begin);
            Rows.push_back(Source.substr(Begin, End - Begin));
            Begin = End == std::string::npos ? Source.size() : End + 1;
        }
        if (Rows.size() % 2 != 0) { Parsed.Problem = "odd number of rows"; return Parsed; }
        for (size_t Index = 0; Index < Rows.size(); Index += 2) Pairs.push_back({ std::atoi(Rows[Index].c_str()), Rows[Index + 1] });
        // Entities: walk the ENTITIES section.
        size_t Index = 0;
        while (Index < Pairs.size() && !(Pairs[Index].first == 2 && Pairs[Index].second == "ENTITIES")) ++Index;
        std::string Kind, Layer;
        std::vector<Vector2> Points;
        std::array<Vector2, 4> Corners = {};
        ParsedSheet::Text Text;
        bool InPolyline = false;
        const auto Flush = [&]()
        {
            if (Kind == "LINE") Parsed.Lines.push_back({ Layer, Points });
            if (Kind == "SOLID") Parsed.Solids.push_back({ Layer, { Corners[0], Corners[1], Corners[2] } });
            if (Kind == "TEXT") { Text.Layer = Layer; Parsed.Texts.push_back(Text); }
            if (Kind == "VERTEX") Points.push_back(Corners[0]);
        };
        for (++Index; Index < Pairs.size(); ++Index)
        {
            const auto& [Code, Value] = Pairs[Index];
            if (Code == 0)
            {
                Flush();
                if (Value == "POLYLINE") { InPolyline = true; Points.clear(); }
                else if (Value == "SEQEND") { Parsed.Lines.push_back({ Layer, Points }); InPolyline = false; }
                else if (Value == "LINE") Points.assign(2, {});
                if (Value == "ENDSEC") break;
                Kind = Value;
                Text = {};
                continue;
            }
            const double Number = std::atof(Value.c_str());
            if (Code == 8 && !(InPolyline && Kind != "POLYLINE")) Layer = Value;
            if (Kind == "LINE" && (Code == 10 || Code == 11)) Points[static_cast<size_t>(Code - 10)].X = Number;
            if (Kind == "LINE" && (Code == 20 || Code == 21)) Points[static_cast<size_t>(Code - 20)].Y = Number;
            if ((Kind == "SOLID" || Kind == "VERTEX") && Code >= 10 && Code <= 13) Corners[static_cast<size_t>(Code - 10)].X = Number;
            if ((Kind == "SOLID" || Kind == "VERTEX") && Code >= 20 && Code <= 23) Corners[static_cast<size_t>(Code - 20)].Y = Number;
            if (Kind == "TEXT")
            {
                if (Code == 10) Text.Position.X = Number;
                if (Code == 20) Text.Position.Y = Number;
                if (Code == 40) Text.Size = Number;
                if (Code == 50) Text.Angle = Number;
                if (Code == 1)
                {
                    std::string Content = Value;
                    for (size_t Found = Content.find("%%c"); Found != std::string::npos; Found = Content.find("%%c", Found + 1)) Content.replace(Found, 3, "⌀");
                    Text.Content = Content;
                }
            }
        }
        return Parsed;
    }

    // Worst coordinate mismatch between the sheet and a parsed copy (1 when the structure differs).
    double CompareSheet(const DrawingSheet& Sheet, const ParsedSheet& Parsed, bool LayerOrder, double SizeScale, double AngleScale, std::string& Problem)
    {
        std::vector<std::pair<std::string, std::vector<Vector2>>> Lines, Solids;
        std::vector<const SheetText*> Texts;
        const int Count = LayerOrder ? 10 : 1;
        for (int Layer = 0; Layer < Count; ++Layer)                                          // SVG groups by layer, DXF keeps drawing order
        {
            for (const SheetPolyline& Line : Sheet.Lines) if (Line.Points.size() >= 2 && (!LayerOrder || static_cast<int>(Line.Layer) == Layer)) Lines.push_back({ SheetLayerLabel(Line.Layer), Line.Points });
            for (const SheetSolid& Solid : Sheet.Solids) if (!LayerOrder || static_cast<int>(Solid.Layer) == Layer) Solids.push_back({ SheetLayerLabel(Solid.Layer), { Solid.Corners[0], Solid.Corners[1], Solid.Corners[2] } });
            for (const SheetText& Text : Sheet.Texts) if (!LayerOrder || static_cast<int>(Text.Layer) == Layer) Texts.push_back(&Text);
        }
        if (Lines.size() != Parsed.Lines.size() || Solids.size() != Parsed.Solids.size() || Texts.size() != Parsed.Texts.size()) { Problem = "entity counts differ"; return 1.0; }
        double Worst = 0.0;
        for (size_t Index = 0; Index < Lines.size(); ++Index)
        {
            if (Lines[Index].first != Parsed.Lines[Index].first || Lines[Index].second.size() != Parsed.Lines[Index].second.size()) { Problem = "line layer or length differs"; return 1.0; }
            for (size_t Corner = 0; Corner < Lines[Index].second.size(); ++Corner)
            {
                Worst = std::max({ Worst, std::fabs(Lines[Index].second[Corner].X - Parsed.Lines[Index].second[Corner].X), std::fabs(Lines[Index].second[Corner].Y - Parsed.Lines[Index].second[Corner].Y) });
            }
        }
        for (size_t Index = 0; Index < Solids.size(); ++Index)
        {
            if (Solids[Index].first != Parsed.Solids[Index].first) { Problem = "solid layer differs"; return 1.0; }
            for (size_t Corner = 0; Corner < 3; ++Corner) Worst = std::max({ Worst, std::fabs(Solids[Index].second[Corner].X - Parsed.Solids[Index].second[Corner].X), std::fabs(Solids[Index].second[Corner].Y - Parsed.Solids[Index].second[Corner].Y) });
        }
        for (size_t Index = 0; Index < Texts.size(); ++Index)
        {
            const SheetText& Text = *Texts[Index];
            const ParsedSheet::Text& Copy = Parsed.Texts[Index];
            if (Text.Content != Copy.Content || std::string(SheetLayerLabel(Text.Layer)) != Copy.Layer) { Problem = "text '" + Text.Content + "' read back as '" + Copy.Content + "'"; return 1.0; }
            Worst = std::max({ Worst, std::fabs(Text.Position.X - Copy.Position.X), std::fabs(Text.Position.Y - Copy.Position.Y), std::fabs(Text.Height * SizeScale - Copy.Size), std::fabs(Text.Angle * AngleScale - Copy.Angle) });
        }
        return Worst;
    }

    void ValidateSvgExport()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        for (const bool SectionEnabled : { false, true })
        {
            DrawingSheet Sheet = ComposeBracketSheet(Partition, Bracket, DrawingProjectionAngle::Third, SectionEnabled);
            if (SectionEnabled) Sheet.Texts.push_back({ SheetLayer::Title, { 40.0, 30.0 }, 3.5, 0.0, SheetTextAnchor::Start, "Plate \"A\" & <B>" });   // Escaping
            const std::string Name   = SectionEnabled ? "Bracket section sheet (with an XML-escaped note)" : "Bracket sheet";
            const std::string Source = WriteDrawingSvg(Sheet);
            const ParsedSheet Parsed = ReadSvg(Source, Sheet.Height);
            std::string Problem = Parsed.Problem;
            const double Error = Problem.empty() ? CompareSheet(Sheet, Parsed, true, 1.0 / 0.72, -180.0 / Pi * -1.0, Problem) : 1.0;
            bool Layers = Parsed.Layers.size() == 10;
            for (size_t Index = 0; Layers && Index < 10; ++Index) Layers = Parsed.Layers[Index] == SheetLayerLabel(static_cast<SheetLayer>(Index));
            const bool Header = Source.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"297.000000mm\" height=\"210.000000mm\" viewBox=\"0 0 297.000000 210.000000\">", 0) == 0;
            const bool Dashes = Source.find("<g id=\"HIDDEN\" stroke=\"#000000\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"0.25\" stroke-dasharray=\"3 1.5\">") != std::string::npos;
            Report(SvgGate, Name, Format("%.0f bytes, %.0f polylines, ", static_cast<double>(Source.size()), static_cast<double>(Parsed.Lines.size())) + Format("%.0f arrowheads, %.0f texts read back; worst coordinate / size / angle error %.2e", static_cast<double>(Parsed.Solids.size()), static_cast<double>(Parsed.Texts.size()), Error) +
                   (Problem.empty() ? "" : " — " + Problem) + (Header ? ", A4 mm header" : ", HEADER WRONG") + (Layers ? ", 10 layer groups in order" : ", LAYERS WRONG") + (Dashes ? ", hidden dashed 3 1.5" : ", DASHES WRONG"),
                   Error, Problem.empty() && Error <= 5.0e-7 + 1.0e-12 && Header && Layers && Dashes);
        }
    }

    void ValidateDxfExport()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        for (const bool SectionEnabled : { false, true })
        {
            const DrawingSheet Sheet  = ComposeBracketSheet(Partition, Bracket, DrawingProjectionAngle::First, SectionEnabled);
            const std::string  Name   = SectionEnabled ? "Bracket section sheet" : "Bracket sheet";
            const std::string  Source = WriteDrawingDxf(Sheet);
            std::vector<std::pair<int, std::string>> Pairs;
            const ParsedSheet Parsed = ReadDxf(Source, Pairs);
            std::string Problem = Parsed.Problem;
            const double Error = Problem.empty() ? CompareSheet(Sheet, Parsed, false, 1.0, 180.0 / Pi, Problem) : 1.0;
            // Structure: header version, the three line types, the ten layers with their line types, EOF.
            const auto Has = [&](int Code, const std::string& Value) { for (const auto& Pair : Pairs) if (Pair.first == Code && Pair.second == Value) return true; return false; };
            bool Tables = Has(1, "AC1009") && Has(2, "CONTINUOUS") && Has(2, "HIDDEN") && Has(2, "CENTER") && Has(2, "STANDARD");
            std::vector<std::pair<std::string, std::string>> Layers;
            for (size_t Index = 0; Index + 1 < Pairs.size(); ++Index)
            {
                if (Pairs[Index].first == 0 && Pairs[Index].second == "LAYER" && Pairs[Index + 1].first == 2)
                {
                    std::string Type;
                    for (size_t Look = Index + 2; Look < Pairs.size() && Pairs[Look].first != 0; ++Look) if (Pairs[Look].first == 6) Type = Pairs[Look].second;
                    Layers.push_back({ Pairs[Index + 1].second, Type });
                }
            }
            bool LayerTable = Layers.size() == 10;
            for (size_t Index = 0; LayerTable && Index < 10; ++Index)
            {
                const SheetLayer Layer = static_cast<SheetLayer>(Index);
                const std::string Expected = Layer == SheetLayer::Hidden ? "HIDDEN" : (Layer == SheetLayer::Centre || Layer == SheetLayer::CuttingPlane) ? "CENTER" : "CONTINUOUS";
                LayerTable = Layers[Index].first == SheetLayerLabel(Layer) && Layers[Index].second == Expected;
            }
            const bool Ends = !Pairs.empty() && Pairs.front() == std::pair<int, std::string>{ 0, "SECTION" } && Pairs.back() == std::pair<int, std::string>{ 0, "EOF" };
            int LineCount = 0, PolylineCount = 0, SolidCount = 0, TextCount = 0;
            for (const auto& Pair : Pairs)
            {
                if (Pair.first != 0) continue;
                LineCount += Pair.second == "LINE"; PolylineCount += Pair.second == "POLYLINE"; SolidCount += Pair.second == "SOLID"; TextCount += Pair.second == "TEXT";
            }
            const bool Diameter = Source.find("\n%%c0.6\n") != std::string::npos;
            Tables = Tables && LayerTable && Ends;
            Report(DxfGate, Name, Format("%.0f LINE, %.0f POLYLINE, ", LineCount, PolylineCount) + Format("%.0f SOLID, %.0f TEXT; worst coordinate / height / angle error %.2e", SolidCount, TextCount, Error) + (Problem.empty() ? "" : " — " + Problem) +
                   (Tables ? ", AC1009 header, LTYPE / LAYER / STYLE tables, EOF" : ", STRUCTURE WRONG") + (Diameter ? ", ⌀ as %%c" : ", %%c MISSING"),
                   Error, Problem.empty() && Error <= 1.0e-12 && Tables && Diameter && static_cast<size_t>(SolidCount) == Sheet.Solids.size() && static_cast<size_t>(TextCount) == Sheet.Texts.size());
        }

        // Files: ExportDrawingFile picks the format by extension and writes exactly the strings above.
        const DrawingSheet Sheet  = ComposeBracketSheet(Partition, Bracket, DrawingProjectionAngle::Third, false);
        const std::string  Folder = std::filesystem::temp_directory_path().string();
        bool Same = true;
        for (const char* Suffix : { ".svg", ".dxf" })
        {
            const std::string Path = Folder + "/FrontierDrawingValidation" + Suffix;
            const std::string Failure = ExportDrawingFile(Sheet, Path);
            std::ifstream File(Path, std::ios::binary);
            const std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
            Same = Same && Failure.empty() && Content == (std::string(Suffix) == ".svg" ? WriteDrawingSvg(Sheet) : WriteDrawingDxf(Sheet));
            std::remove(Path.c_str());
        }
        Report(DxfGate, "ExportDrawingFile", Same ? "the .svg and .dxf files equal the writers' strings byte for byte" : "a file differs", Same ? 0.0 : 1.0, Same);
    }

    void ValidateSheetRefusals()
    {
        ModelPartition  Partition;
        const BodyToken Bracket = ConstructBracket(Partition);
        const auto Expect = [&](const std::string& Name, const std::string& Refusal, const char* Fragment)
        {
            Report(SheetRefuseGate, Name, Refusal.empty() ? std::string("not refused") : Refusal, 0.0, Refusal.find(Fragment) != std::string::npos);
        };
        DrawingSheetRequest Request;
        Request.Paper = DrawingPaperSize::A4;
        Expect("No bodies", ComposeDrawingSheet(Partition, {}, Request).Refusal, "no bodies");
        DrawingSheetRequest Unitless = Request;
        Unitless.ModelUnitMillimetres = 0.0;
        Expect("Model unit 0", ComposeDrawingSheet(Partition, { Bracket }, Unitless).Refusal, "model unit must be a positive length");
        DrawingSheetRequest Negative = Request;
        Negative.Scale = -2.0;
        Expect("Scale −2", ComposeDrawingSheet(Partition, { Bracket }, Negative).Refusal, "scale must be positive");
        Expect("Bracket at 50:1 on A4", ComposeBracketSheet(Partition, Bracket, DrawingProjectionAngle::Third, false, 50.0).Refusal, "do not fit on A4 at 50:1");
        ModelPartition  Vast;
        const BodyToken Slab = ConstructBlock(Vast, {}, { 2.0e6, 1.0e6, 1.0e5 }).Value;
        Expect("A 2000 km slab", ComposeDrawingSheet(Vast, { Slab }, Request).Refusal, "even at 1:1000");
        DrawingSheetRequest Missed = Request;
        Missed.Section = OrthonormalFrame{ { 0.0, 10.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        Expect("Section plane beyond the body", ComposeDrawingSheet(Partition, { Bracket }, Missed).Refusal, "misses every body");
        const DrawingSheet Refused = ComposeDrawingSheet(Partition, {}, Request);
        Expect("Export of a refused sheet", ExportDrawingFile(Refused, std::filesystem::temp_directory_path().string() + "/Refused.svg"), "the sheet was refused");
        Expect("Unknown extension", ExportDrawingFile(ComposeBracketSheet(Partition, Bracket, DrawingProjectionAngle::Third, false), "Sheet.pdf"), "unknown drawing format");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    std::string Only;
    for (int Index = 1; Index + 1 < Count; ++Index)
    {
        if (std::string(Arguments[Index]) == "--only") Only = Arguments[Index + 1];
        if (std::string(Arguments[Index]) == "--svg") SvgFolder = Arguments[Index + 1];
    }
    const auto Start = std::chrono::steady_clock::now();
    if (Only.empty() || Only == "block") ValidateBlock();
    if (Only.empty() || Only == "hole") ValidateHole();
    if (Only.empty() || Only == "silhouette") ValidateSilhouettes();
    if (Only.empty() || Only == "occlusion") ValidateOcclusion();
    if (Only.empty() || Only == "section") ValidateSection();
    if (Only.empty() || Only == "tangent") ValidateTangent();
    if (Only.empty() || Only == "refusal") ValidateRefusals();
    if (Only.empty() || Only == "layout") ValidateLayout();
    if (Only.empty() || Only == "dimension") ValidateDimensions();
    if (Only.empty() || Only == "title") ValidateTitleBlock();
    if (Only.empty() || Only == "sheetsection") ValidateSheetSection();
    if (Only.empty() || Only == "svg") ValidateSvgExport();
    if (Only.empty() || Only == "dxf") ValidateDxfExport();
    if (Only.empty() || Only == "sheetrefusal") ValidateSheetRefusals();
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Drawing validation\n\n%s Priority 11 D1–D2: orthographic views, exact silhouettes, ray-cast hidden lines, section views with hatching; drawing sheets with automatic dimensions, title block and SVG / DXF export (%.2f s)\n\n", PassedCondition ? "🟢" : "🔴", Seconds);
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    const auto Cell = [](std::string Text)                                                   // Details carry |x| and the like
    {
        for (size_t At = Text.find('|'); At != std::string::npos; At = Text.find('|', At + 2)) Text.replace(At, 1, "\\|");
        return Text;
    };
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Cell(Row.Name).c_str(), Cell(Row.Detail).c_str());
    return PassedCondition ? 0 : 1;
}
