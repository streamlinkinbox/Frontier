//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.cpp — Command set: sketch curves, primitive surfaces, extrude/revolve/loft, scene, view, render
//============================================================================================================================================

#include "ConsoleHost.h"
#include "Presentation/ScenePresentation.h"
#include "Kernel/ProfileSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Kernel/ConstraintSolver.h"
#include "Kernel/ConstraintGraph.h"
#include "Kernel/MirrorSolver.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>

namespace Frontier
{

namespace
{
const float Backdrop[4] = { 0.0f, 0.0f, 0.0f, 1.0f };                              // Phase 20: black background per user request
const char* ClassName(FigureClassification K) noexcept { return K == FigureClassification::Curve ? "curve" : "surface"; }
}

ConsoleHost::ConsoleHost(std::string ProofFolder, uint32_t Width, uint32_t Height) noexcept
    : Proofs(std::move(ProofFolder)), Surface(std::make_unique<SoftwareRaster>(Width, Height))
{
    Register();
    RegisterInteraction();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  OUTPUT
//------------------------------------------------------------------------------------------------------------------------

bool ConsoleHost::Refuse(const char* Format, ...) noexcept
{
    ++Refusals;
    std::printf("  ✗ ");
    va_list Args; va_start(Args, Format); std::vprintf(Format, Args); va_end(Args);
    std::printf("\n");
    return false;
}

void ConsoleHost::Row(const char* Format, ...) noexcept
{
    std::printf("  · ");
    va_list Args; va_start(Args, Format); std::vprintf(Format, Args); va_end(Args);
    std::printf("\n");
}

void ConsoleHost::DescribeFigure(const SceneFigure& Figure) noexcept
{
    Box3 B = Figure.Bounds();
    if (Figure.Classification == FigureClassification::Curve)
    {
        const NurbsCurve& C = Figure.Curve;
        Row("#%-3u %-18s curve    deg %d  poles %-4d %s%s  length %.4f  bounds [%.2f %.2f %.2f]–[%.2f %.2f %.2f]%s%s",
            Figure.Identity, Figure.Name.c_str(), C.Degree, C.PoleCount(), C.Rational() ? "rational" : "integral", C.Closed() ? " closed" : "",
            C.Length(), B.Low.X, B.Low.Y, B.Low.Z, B.High.X, B.High.Y, B.High.Z, Figure.Construction ? "  [construction]" : "", Figure.Selected ? "  [selected]" : "");
    }
    else if (Figure.Classification == FigureClassification::Body)
    {
        BodyReport R = Figure.Body.Validate();
        Row("#%-3u %-18s %-8s V%d E%d F%d  χ=%d genus %d  vol %.4f  area %.4f  bounds [%.2f %.2f %.2f]–[%.2f %.2f %.2f]%s%s%s",
            Figure.Identity, Figure.Name.c_str(), Describe(Figure.Body.Classification()), R.Vertices, R.Edges, R.Faces, R.EulerCharacteristic, R.Genus, R.Volume, R.Area,
            B.Low.X, B.Low.Y, B.Low.Z, B.High.X, B.High.Y, B.High.Z, R.Solid() ? "" : (R.OpenEdges ? "  [open]" : R.MisorientedEdges ? "  [misoriented]" : "  [not solid]"),
            Figure.Selected ? "  [selected]" : "", Figure.SelectedFaces.empty() && Figure.SelectedEdges.empty() ? "" : "  [sub-selection]");
    }
    else if (Figure.Classification == FigureClassification::Empty)
    {
        Row("#%-3u %-18s empty    at (%.3f, %.3f, %.3f)%s%s",
            Figure.Identity, Figure.Name.c_str(), Figure.Blueprint.A.X, Figure.Blueprint.A.Y, Figure.Blueprint.A.Z,
            Figure.Selected ? "  [selected]" : "", Figure.Hidden ? "  [hidden]" : "");
    }
    else
    {
        const NurbsSurface& S = Figure.Surface;
        Row("#%-3u %-18s surface  deg %dx%d  poles %dx%d  %s%s%s  bounds [%.2f %.2f %.2f]–[%.2f %.2f %.2f]%s",
            Figure.Identity, Figure.Name.c_str(), S.DegreeU, S.DegreeV, S.CountU, S.CountV, S.Rational() ? "rational" : "integral",
            S.ClosedU() ? " closedU" : "", S.ClosedV() ? " closedV" : "", B.Low.X, B.Low.Y, B.Low.Z, B.High.X, B.High.Y, B.High.Z, Figure.Selected ? "  [selected]" : "");
    }
}

// ──────────────────────────────────────────────────────────────────────────────────────────
//  Dimensions (Phase 13): world-space annotations drawn as an overlay (no depth test, no pick).
//  A dim has two endpoints A, B and an up-vector N; for linear dims N is the dim's label-plane
//  normal (we project A and B into a screen-aligned coordinate frame), for radius dims N is the
//  centre of the curve, for angle dims A = B = the vertex and N is the included-angle bisector
//  of the two incident edges. The value is editable via `dim edit <id> <new-value>` and the
//  label is recomputed from the value every frame (so an edit shows up on the next render).
// ──────────────────────────────────────────────────────────────────────────────────────────

// Phase 20: dim placement polish. Return a unit normal pointing at the camera, picked from
//    ±N. The logic is: project the world-space `Forward()` direction onto the plane whose
//    normal is `N`. If the projected vector is on the +N side, return `+N`; else return `-N`.
//    If `N` is parallel to the camera direction (the degenerate case), use the screen-right
//    vector to pick: the dim sits on whichever side is to the right of the screen, so it
//    reads naturally from the viewer's POV in axis-aligned views (front / back / side / top).
Vec3 ConsoleHost::CameraFacingSide(Vec3 N) const noexcept
{
    if (N.LengthSquared() < 1e-12) return Vec3(0, 1, 0);
    Vec3 Nu = N.Normalised();
    Vec3 Fwd = View.Forward();
    Vec3 Right = View.Right();
    double Side = Fwd.Dot(Nu);
    if (std::fabs(Side) < 1e-6)
    {
        // Degenerate: camera looks along N. Use the screen-right vector to decide which side.
        return Right.Dot(Nu) > 0 ? Nu : -Nu;
    }
    return Side > 0 ? Nu : -Nu;
}

uint32_t ConsoleHost::EmitDimension(DimensionForm Form, uint32_t Anchor, const std::string& AnchorName, Vec3 A, Vec3 B, Vec3 N, double Value, bool Auto) noexcept
{
    DimensionEntry D; D.Id = NextDimensionId++; D.Form = Form; D.Anchor = Anchor; D.AnchorName = AnchorName; D.A = A; D.B = B; D.N = N; D.Value = Value; D.Auto = Auto;
    Dimensions.push_back(std::move(D));
    return Dimensions.back().Id;
}

void ConsoleHost::DeleteAutoDimensionsFor(uint32_t Anchor) noexcept
{
    // Re-emitting dims for a figure starts by removing any old auto dims attached to that figure.
    if (Anchor == 0) return;
    Dimensions.erase(std::remove_if(Dimensions.begin(), Dimensions.end(), [Anchor](const DimensionEntry& D) { return D.Auto && D.Anchor == Anchor; }), Dimensions.end());
}

namespace
{
    // Format a measurement for a label: linear dims get mm precision (3 decimals), angles 1 decimal
    //    with a degree suffix, radii 3 decimals. The user-edited value overrides anything measured.
    std::string FormatDimensionLabel(const ConsoleHost::DimensionEntry& D) noexcept
    {
        char Buf[64];
        switch (D.Form)
        {
            case ConsoleHost::DimensionForm::Linear:
            case ConsoleHost::DimensionForm::Bbox:
            case ConsoleHost::DimensionForm::Radius:
            case ConsoleHost::DimensionForm::Diameter:
            case ConsoleHost::DimensionForm::ArcLength:
                std::snprintf(Buf, sizeof Buf, "%.3f", D.Value);
                return std::string(Buf);
            case ConsoleHost::DimensionForm::Angle:
            {
                // Build the label with a non-ASCII degree mark by appending the UTF-8 bytes of U+00B0.
                std::snprintf(Buf, sizeof Buf, "%.1f", D.Value * (180.0 / 3.14159265358979323846));
                std::string S(Buf);
                S.push_back(char(0xC2)); S.push_back(char(0xB0));                       // U+00B0 in UTF-8
                return S;
            }
        }
        return "";
    }

    // A 5×7 bitmap font for digits, period, sign, and a degree sign. The bitmap is one bit per
    //    pixel, packed left-to-right, top-to-bottom. We render each char as 5x7 quads (one quad
    //    per lit pixel) so we stay in the segment stream without a font atlas. The character set
    //    is enough for "0123456789.-<deg>" — anything else becomes '0'.
    const uint8_t* Glyph(unsigned char C) noexcept
    {
        static const uint8_t G[13][7] =
        {
            { 0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110 }, // 0
            { 0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 }, // 1
            { 0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111 }, // 2
            { 0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110 }, // 3
            { 0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010 }, // 4
            { 0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110 }, // 5
            { 0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110 }, // 6
            { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000 }, // 7
            { 0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110 }, // 8
            { 0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100 }, // 9
            { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100 }, // . (bottom dots)
            { 0b00000, 0b00000, 0b00000, 0b01110, 0b00000, 0b00000, 0b00000 }, // - (middle bar)
            { 0b00000, 0b11000, 0b11000, 0b00000, 0b00000, 0b00000, 0b00000 }, // deg (top circles, U+00B0 first byte 0xC2)
        };
        if (C >= '0' && C <= '9') return G[C - '0'];
        if (C == '.') return G[10];
        if (C == '-') return G[11];
        if (C == 0xC2) return G[12];                                                   // first byte of UTF-8 U+00B0
        return G[0];
    }
}

void ConsoleHost::AutoEmitDimensions(const SceneFigure& Figure) noexcept
{
    // Re-emit: drop the previous auto set for this anchor, then add a fresh one.
    DeleteAutoDimensionsFor(Figure.Identity);
    Box3 B = Figure.Bounds();
    if (B.Low.X > B.High.X) return;                                                // empty figure — no dim
    if (Figure.Construction)
    {
        // Construction geometry: no live-dim auto set (the dim tree would clutter the workplane and
        //    live-edit a construction line doesn't make sense — they're not the design's truth).
        //    The figure is still clickable + selectable; `dim <name> <p1> <p2>` (user-added) still works.
        return;
    }
    if (Figure.Classification == FigureClassification::Curve)
    {
        const NurbsCurve& C = Figure.Curve;
        if (C.PoleCount() == 0) return;
        // Arc length dim — endpoints are the curve's endpoints (or the same point for closed curves).
        //    Value is the analytic length (parametric Length() collapses to 0 for closed periodic forms).
        Vec3 Start = C.Sample(0.0);
        Vec3 End   = C.Sample(1.0);
        double AnalyticLength = C.Length();
        if (C.Classification == CurveClassification::Circle) AnalyticLength = 2.0 * 3.14159265358979323846 * C.RadiusMajor;
        else if (C.Classification == CurveClassification::Ellipse)
        {
            double A = C.RadiusMajor, B = C.RadiusMinor, H = std::pow((A - B) / (A + B), 2);
            AnalyticLength = 3.14159265358979323846 * (A + B) * (1.0 + 3.0 * H / (10.0 + std::sqrt(4.0 - 3.0 * H)));
        }
        if (AnalyticLength < 1e-9) AnalyticLength = C.Length();
        // Live arc-length dim tied to source field B - A (length of A→B for a line) or to the curve's
        //    analytic radius (for circle/ellipse) or the polyline sum. Slot assignment is per-form below.
        {
            DimensionEntry D; D.Form = DimensionForm::ArcLength; D.Anchor = Figure.Identity; D.AnchorName = Figure.Name + " length";
            D.A = Start; D.B = End; D.N = Plane.Normal(); D.Value = AnalyticLength; D.Slot = 14; D.BlueprintForm = Figure.Blueprint.Form;
            D.Id = NextDimensionId++; D.Auto = true; Dimensions.push_back(std::move(D));
        }
        // For circles / arcs / ellipses we know the analytic centre and radii — emit those dims too.
        if (C.Classification == CurveClassification::Circle || C.Classification == CurveClassification::Arc)
        {
            Vec3 Ctr = C.Centre;
            Vec3 OnCurve = C.Sample(0.0);
            // Radius dim is live: editing it changes R0 on the source (R0 = circle/arc radius).
            DimensionEntry R; R.Form = DimensionForm::Radius; R.Anchor = Figure.Identity; R.AnchorName = Figure.Name + " radius";
            R.A = Ctr; R.B = OnCurve; R.N = Plane.Normal(); R.Value = C.RadiusMajor; R.Slot = 12; R.BlueprintForm = Figure.Blueprint.Form;
            R.Id = NextDimensionId++; R.Auto = true; Dimensions.push_back(std::move(R));
        }
        else if (C.Classification == CurveClassification::Ellipse)
        {
            Vec3 Ctr = C.Centre;
            Vec3 MajorEnd = Ctr + Vec3{ C.RadiusMajor, 0, 0 };
            Vec3 MinorEnd = Ctr + Vec3{ 0, C.RadiusMinor, 0 };
            DimensionEntry R1; R1.Form = DimensionForm::Radius; R1.Anchor = Figure.Identity; R1.AnchorName = Figure.Name + " Rmajor";
            R1.A = Ctr; R1.B = MajorEnd; R1.N = Plane.Normal(); R1.Value = C.RadiusMajor; R1.Slot = 12; R1.BlueprintForm = Figure.Blueprint.Form;
            R1.Id = NextDimensionId++; R1.Auto = true; Dimensions.push_back(std::move(R1));
            DimensionEntry R2; R2.Form = DimensionForm::Radius; R2.Anchor = Figure.Identity; R2.AnchorName = Figure.Name + " Rminor";
            R2.A = Ctr; R2.B = MinorEnd; R2.N = Plane.Normal(); R2.Value = C.RadiusMinor; R2.Slot = 13; R2.BlueprintForm = Figure.Blueprint.Form;
            R2.Id = NextDimensionId++; R2.Auto = true; Dimensions.push_back(std::move(R2));
        }
        // For lines, also emit a "X" and "Y" and "Z" extent dim (so editing the line's endpoints is live).
        if (C.Classification == CurveClassification::Line)
        {
            auto ExtDim = [&](const char* Tag, int Slot, double V, Vec3 Lo, Vec3 Hi)
            {
                DimensionEntry D; D.Form = DimensionForm::Linear; D.Anchor = Figure.Identity; D.AnchorName = Figure.Name + " " + Tag;
                D.A = Lo; D.B = Hi; D.N = Vec3(0, 1, 0); D.Value = V; D.Slot = Slot; D.BlueprintForm = Figure.Blueprint.Form;
                D.Id = NextDimensionId++; D.Auto = true; Dimensions.push_back(std::move(D));
            };
            // Each axis dim lies along its axis, lifted slightly in +Y to keep it off the line.
            ExtDim("X", 3, std::fabs(C.Poles[1].Divide().X - C.Poles[0].Divide().X), Vec3(C.Poles[0].Divide().X, C.Poles[0].Divide().Y + 0.04, C.Poles[0].Divide().Z), Vec3(C.Poles[1].Divide().X, C.Poles[0].Divide().Y + 0.04, C.Poles[0].Divide().Z));
            ExtDim("Y", 4, std::fabs(C.Poles[1].Divide().Y - C.Poles[0].Divide().Y), Vec3(C.Poles[0].Divide().X - 0.04, C.Poles[0].Divide().Y, C.Poles[0].Divide().Z), Vec3(C.Poles[0].Divide().X - 0.04, C.Poles[1].Divide().Y, C.Poles[0].Divide().Z));
            ExtDim("Z", 5, std::fabs(C.Poles[1].Divide().Z - C.Poles[0].Divide().Z), Vec3(C.Poles[0].Divide().X, C.Poles[0].Divide().Y, C.Poles[0].Divide().Z), Vec3(C.Poles[1].Divide().Z > C.Poles[0].Divide().Z ? C.Poles[0].Divide() : C.Poles[1].Divide()));
        }
        // For polyline: emit per-vertex X/Y/Z dims (Phase 15: per-vertex live edit). Each vertex K
        //    gets three dims, with slot 18+K*3 (X), 19+K*3 (Y), 20+K*3 (Z). The dim line is drawn
        //    as a tiny 0-length line at the vertex, lifted by 4 cm along the world axis, so the user
        //    sees a small "X0=1.23" / "Y1=-0.5" / "Z3=2.0" label per vertex. Live edit mutates
        //    PolylinePoints[K].Component and rebuilds the polyline from the new points.
        if (C.Classification == CurveClassification::Polyline && C.Poles.size() >= 2)
        {
            // Use the source PolylinePoints if the figure has a Blueprint, else the current poles.
            // The source is what ApplyLiveEdit mutates; emitting dims that point at the source means
            //    the value label tracks the live value, not the rendered one.
            const std::vector<Vec3>* Points = nullptr;
            if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Polyline && !Figure.Blueprint.PolylinePoints.empty())
                Points = &Figure.Blueprint.PolylinePoints;
            size_t N = Points ? Points->size() : C.Poles.size();
            for (size_t K = 0; K < N; ++K)
            {
                Vec3 P = Points ? (*Points)[K] : C.Poles[K].Divide();
                int BaseSlot = int(18 + int(K) * 3);
                char Tag[16];
                auto EmitVertex = [&](const char* Axis, int Comp, int Slot, double V, Vec3 Lift)
                {
                    std::snprintf(Tag, sizeof(Tag), "%s%zu", Axis, K);
                    DimensionEntry D; D.Form = DimensionForm::Linear; D.Anchor = Figure.Identity; D.AnchorName = Figure.Name + " " + Tag;
                    D.A = P; D.B = P + Lift; D.N = Vec3(0, 1, 0); D.Value = V; D.Slot = Slot; D.BlueprintForm = Figure.Blueprint.Form;
                    D.Id = NextDimensionId++; D.Auto = true; Dimensions.push_back(std::move(D));
                };
                // Lift along each axis; the X dim lifts in +Y, the Y dim lifts in +X, the Z dim lifts in +X.
                EmitVertex("X", 0, BaseSlot + 0, P.X, Vec3(0, 0.04, 0));
                EmitVertex("Y", 1, BaseSlot + 1, P.Y, Vec3(-0.04, 0, 0));
                EmitVertex("Z", 2, BaseSlot + 2, P.Z, Vec3(0, 0, 0));
            }
        }
    }
    else
    {
        // Body / Surface: emit one linear dim per axis. Endpoints are the face-centres of the two
        //    opposing faces, so the dim lies on the body surface. The dim-line normal is set to the
        //    world axis perpendicular to the feature so the renderer lifts the dim line off the body
        //    in a consistent direction (X dim → +Y, Y dim → +X, Z dim → +X, all offset by a small
        //    world-space amount so the line sits ON the face, not 22 px in screen space).
        Vec3 Centre = (B.Low + B.High) * 0.5;
        // Per-primitive: the live slot for each bbox dim is the corresponding B component (3,4,5 for
        //    box) or R2 (cylinder/cone height), etc. The mapping is set per-Form in the live-edit switch.
        auto EmitBbox = [&](const char* Tag, Vec3 Lo, Vec3 Hi, double V, Vec3 Lift, int Slot)
        {
            DimensionEntry D; D.Form = DimensionForm::Bbox; D.Anchor = Figure.Identity; D.AnchorName = std::string(Figure.Name) + " " + Tag;
            D.A = Lo; D.B = Hi; D.N = Lift; D.Value = V; D.Slot = Slot; D.BlueprintForm = Figure.Blueprint.Form;
            D.Id = NextDimensionId++; D.Auto = true; Dimensions.push_back(std::move(D));
        };
        // Box: B.X (3), B.Y (4), B.Z (5) — all live.
        // Cylinder/Cone: R2 = height (slot 14). Radius is R0 (slot 12).
        // Sphere: R0 = radius (slot 12). R1 = unused.
        // Torus: R3 = radiusMajor (slot 15), R2 = radiusMinor (slot 14).
        // For now, set Slot based on form. The default offset is a small world-space amount
        //    (0.04 m ≈ 4 cm) so the line sits ON the surface, not far from it.
        if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Box)
        {
            // Phase 20: dim placement is camera-facing. The X dim's lift is ±Y — pick the side of the
            //    box that the camera is on. The dim line endpoints sit on the face plane, with the
            //    renderer adding the 4 cm world-space lift, so we DON'T pre-offset the endpoints.
            Vec3 XSide = CameraFacingSide(Vec3(0, 1, 0));
            Vec3 YSide = CameraFacingSide(Vec3(1, 0, 0));
            Vec3 ZSide = CameraFacingSide(Vec3(1, 0, 0));
            double Xface = (XSide.Y > 0) ? B.High.Y : B.Low.Y;
            double Yface = (YSide.X > 0) ? B.High.X : B.Low.X;
            double Zface = (ZSide.X > 0) ? B.High.X : B.Low.X;
            // Y dim normal is the camera-facing X side; Z dim normal is the camera-facing X side too
            //    (the dim sits on the ±X face of the box and points along Z).
            Vec3 XNormal = (XSide.Y > 0) ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
            Vec3 YNormal = (YSide.X > 0) ? Vec3(1, 0, 0) : Vec3(-1, 0, 0);
            Vec3 ZNormal = (ZSide.X > 0) ? Vec3(1, 0, 0) : Vec3(-1, 0, 0);
            EmitBbox("X", Vec3(B.Low.X, Xface, Centre.Z), Vec3(B.High.X, Xface, Centre.Z), B.High.X - B.Low.X, XNormal, 3);
            EmitBbox("Y", Vec3(Yface, B.Low.Y, Centre.Z), Vec3(Yface, B.High.Y, Centre.Z), B.High.Y - B.Low.Y, YNormal, 4);
            EmitBbox("Z", Vec3(Zface, Centre.Y, B.Low.Z), Vec3(Zface, Centre.Y, B.High.Z), B.High.Z - B.Low.Z, ZNormal, 5);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Cylinder || Figure.Blueprint.Form == SceneFigure::ParametricForm::Cone)
        {
            // For a cylinder/cone, the height dim is live (slot 14 = R2).
            // Phase 20: pick the perpendicular direction (side) that faces the camera. The dim line
            //    sits on the side of the body at radius R0 (the surface), with the renderer adding
            //    its 4 cm world-space lift. We no longer pre-offset by 0.04 — let the renderer do it.
            Vec3 AxisU = Figure.Blueprint.Axis.LengthSquared() > 1e-12 ? Figure.Blueprint.Axis.Normalised() : Vec3(0, 0, 1);
            Vec3 Foot = Figure.Blueprint.A;  // foot of the cylinder/cone
            Vec3 Top  = Foot + AxisU * Figure.Blueprint.R2;
            // Build a perpendicular to the axis that points "outward" — pick any direction
            //    perpendicular to the axis, then we'll flip it to face the camera.
            Vec3 Perp = std::fabs(AxisU.Z) < 0.9 ? Vec3(0, 0, 1).Cross(AxisU).Normalised() : Vec3(1, 0, 0).Cross(AxisU).Normalised();
            // Camera-facing side: flip the perpendicular so its component in the camera direction
            //    is positive. The dim line lives in this plane (it's parallel to the axis, offset
            //    by R0 along the camera-facing perpendicular).
            Vec3 Fwd = View.Forward();
            if (Perp.Dot(Fwd) < 0) Perp = -Perp;
            // The line itself sits ON the surface: the two endpoints are Foot + Perp*R0 and
            //    Top + Perp*R0. The renderer lifts by OffM (4 cm) along Perp, so the line ends
            //    up 4 cm off the body — close enough to read, not too far.
            EmitBbox("height", Foot + Perp * Figure.Blueprint.R0, Top + Perp * Figure.Blueprint.R0, Figure.Blueprint.R2, Perp, 14);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Sphere)
        {
            // Radius dim from centre to a point on the sphere — live (slot 12 = R0).
            // Phase 20: pick a direction from the centre to a point on the sphere that faces the
            //    camera. We project the camera forward into the screen plane and use that as the
            //    "outward" direction; the dim line goes from the centre to the sphere surface in
            //    that direction.
            Vec3 Ctr = Figure.Blueprint.A;  // sphere centre
            Vec3 Fwd = View.Forward();
            Vec3 Up = View.Up();
            // Screen-plane direction: project Forward into the Up+Right plane, then take the
            //    screen-up component (so the radius dim reads "vertical" from the viewer's POV).
            Vec3 ScreenDir = Up - Fwd * Fwd.Dot(Up);
            if (ScreenDir.LengthSquared() < 1e-12) ScreenDir = Vec3(0, 1, 0);
            ScreenDir = ScreenDir.Normalised();
            Vec3 Pnt = Ctr + ScreenDir * Figure.Blueprint.R0;
            // Lift perpendicular to the radius in the screen plane (so the dim line sits beside
            //    the radius line, not on top of it).
            Vec3 Lift = ScreenDir.Cross(Fwd);
            if (Lift.LengthSquared() < 1e-12) Lift = Vec3(0, 1, 0);
            Lift = Lift.Normalised();
            EmitBbox("radius", Ctr, Pnt, Figure.Blueprint.R0, Lift, 12);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Torus)
        {
            // Major radius (slot 15) and minor radius (slot 14).
            // Phase 20: pick directions that face the camera (Rmajor in the screen-up direction
            //    from the centre, Rminor in the screen-right direction from the major point).
            Vec3 Ctr = Figure.Blueprint.A;  // torus centre
            Vec3 Fwd = View.Forward();
            Vec3 Up = View.Up();
            Vec3 Right = View.Right();
            Vec3 ScreenUp = Up - Fwd * Fwd.Dot(Up);
            if (ScreenUp.LengthSquared() < 1e-12) ScreenUp = Vec3(0, 1, 0);
            ScreenUp = ScreenUp.Normalised();
            Vec3 ScreenRight = Right - Fwd * Fwd.Dot(Right);
            if (ScreenRight.LengthSquared() < 1e-12) ScreenRight = Vec3(1, 0, 0);
            ScreenRight = ScreenRight.Normalised();
            Vec3 PntMajor = Ctr + ScreenUp * Figure.Blueprint.R3;
            Vec3 PntMinor = PntMajor + ScreenRight * Figure.Blueprint.R2;
            Vec3 LiftMajor = ScreenUp.Cross(Fwd);
            if (LiftMajor.LengthSquared() < 1e-12) LiftMajor = Vec3(0, 1, 0);
            LiftMajor = LiftMajor.Normalised();
            Vec3 LiftMinor = ScreenRight.Cross(Fwd);
            if (LiftMinor.LengthSquared() < 1e-12) LiftMinor = Vec3(1, 0, 0);
            LiftMinor = LiftMinor.Normalised();
            EmitBbox("Rmajor", Ctr, PntMajor, Figure.Blueprint.R3, LiftMajor, 15);
            EmitBbox("Rminor", PntMajor, PntMinor, Figure.Blueprint.R2, LiftMinor, 14);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::ChamferEdge)
        {
            // Plasticity-style: a dim **on the chamfered edge** showing the set-back distance. Endpoints
            //    are the edge's start/stop, the lift is perpendicular to the edge in the world frame (so
            //    the line sits on the chamfered face, offset 4 cm), and the value is the set-back
            //    distance (slot 12). After the chamfer dim, also emit a generic bbox X/Y/Z so the
            //    chamfered body has the usual outline dims.
            int EdgeIdx = Figure.Blueprint.I0;
            if (EdgeIdx >= 0 && EdgeIdx < (int)Figure.Body.Edges.size())
            {
                const BrepEdge& E = Figure.Body.Edges[EdgeIdx];
                Vec3 Lo = E.Curve.Sample(E.Curve.DomainStart());
                Vec3 Hi = E.Curve.Sample(E.Curve.DomainEnd());
                Vec3 EdgeDir = (Hi - Lo); if (EdgeDir.Length() < 1e-9) EdgeDir = Vec3(1, 0, 0); else EdgeDir = EdgeDir.Normalised();
                Vec3 Perp = EdgeDir.Cross(Vec3(0, 0, 1));
                if (Perp.LengthSquared() < 1e-12) Perp = EdgeDir.Cross(Vec3(1, 0, 0));
                if (Perp.LengthSquared() < 1e-12) Perp = Vec3(0, 1, 0);
                Perp = Perp.Normalised();
                // Phase 20: flip Perp to face the camera.
                if (Perp.Dot(View.Forward()) < 0) Perp = -Perp;
                Vec3 Mid = (Lo + Hi) * 0.5;
                EmitBbox("chamfer", Mid - EdgeDir * (Figure.Blueprint.R0 * 0.5), Mid + EdgeDir * (Figure.Blueprint.R0 * 0.5), Figure.Blueprint.R0, Perp, 12);
            }
            // Also emit the bbox X/Y/Z dims so the chamfered body has the standard outline set
            //    (these are read-only, slot = -1, since editing them is meaningless for a chamfered body).
            // Phase 20: dim line on the camera-facing side of each face.
            Vec3 XSide = CameraFacingSide(Vec3(0, 1, 0));
            Vec3 YSide = CameraFacingSide(Vec3(1, 0, 0));
            Vec3 ZSide = CameraFacingSide(Vec3(1, 0, 0));
            double Xface = (XSide.Y > 0) ? B.High.Y : B.Low.Y;
            double Yface = (YSide.X > 0) ? B.High.X : B.Low.X;
            double Zface = (ZSide.X > 0) ? B.High.X : B.Low.X;
            Vec3 XNormal = (XSide.Y > 0) ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
            Vec3 YNormal = (YSide.X > 0) ? Vec3(1, 0, 0) : Vec3(-1, 0, 0);
            Vec3 ZNormal = (ZSide.X > 0) ? Vec3(1, 0, 0) : Vec3(-1, 0, 0);
            EmitBbox("X", Vec3(B.Low.X, Xface, Centre.Z), Vec3(B.High.X, Xface, Centre.Z), B.High.X - B.Low.X, XNormal, -1);
            EmitBbox("Y", Vec3(Yface, B.Low.Y, Centre.Z), Vec3(Yface, B.High.Y, Centre.Z), B.High.Y - B.Low.Y, YNormal, -1);
            EmitBbox("Z", Vec3(Zface, Centre.Y, B.Low.Z), Vec3(Zface, Centre.Y, B.High.Z), B.High.Z - B.Low.Z, ZNormal, -1);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Revolve)
        {
            // Phase 16: live angle dim. The dim is drawn between the curve's foot on the axis and a
            //    point swept by the axis + a perpendicular direction, so it reads as a sector.
            // For now, emit a linear dim showing the swept angle in degrees (slot 12 = R0 in radians).
            //    The endpoints sit on the axis above and below the foot, lifted by the axis unit vector.
            Vec3 AxisU = Figure.Blueprint.B.LengthSquared() > 1e-12 ? Figure.Blueprint.B.Normalised() : Vec3(0, 1, 0);
            Vec3 Lo = Figure.Blueprint.A;
            Vec3 Hi = Lo + AxisU * 0.4;
            EmitBbox("angle", Lo, Hi, ScalarCriteria::Degrees(Figure.Blueprint.R0), Vec3(0, 1, 0), 12);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Loft)
        {
            // Phase 16: V-degree (I0). Emit a label dim on the bbox top, read-only-ish but live.
            // Phase 20: dim line on the camera-facing side of the top face.
            Vec3 Side = CameraFacingSide(Vec3(0, 1, 0));
            double Yface = (Side.Y > 0) ? B.High.Y : B.Low.Y;
            Vec3 YNormal = (Side.Y > 0) ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
            EmitBbox("degree", Vec3(B.Low.X, Yface, Centre.Z), Vec3(B.High.X, Yface, Centre.Z), double(Figure.Blueprint.I0), YNormal, 16);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Sweep)
        {
            // Phase 16: scale + twist + stations dims. Each on the body's top edge with a small offset.
            // Phase 20: dim line on the camera-facing side of the top face.
            Vec3 Side = CameraFacingSide(Vec3(0, 1, 0));
            double Yface = (Side.Y > 0) ? B.High.Y : B.Low.Y;
            Vec3 YNormal = (Side.Y > 0) ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
            EmitBbox("scale", Vec3(B.Low.X, Yface, Centre.Z), Vec3(B.High.X, Yface, Centre.Z), Figure.Blueprint.R0, YNormal, 12);
            EmitBbox("twist", Vec3(B.Low.X, Yface, Centre.Z), Vec3(B.High.X, Yface, Centre.Z), ScalarCriteria::Degrees(Figure.Blueprint.R1), YNormal, 13);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Pipe)
        {
            // Phase 16: radius dim. Drawn as a chord through the tube cross-section, on the side of the tube.
            // Phase 20: pick a point on the side facing the camera.
            Vec3 Ctr = (B.Low + B.High) * 0.5;
            Vec3 Fwd = View.Forward();
            Vec3 Up = View.Up();
            Vec3 ScreenUp = Up - Fwd * Fwd.Dot(Up);
            if (ScreenUp.LengthSquared() < 1e-12) ScreenUp = Vec3(0, 1, 0);
            ScreenUp = ScreenUp.Normalised();
            Vec3 Pnt = Ctr + ScreenUp * Figure.Blueprint.R0;
            Vec3 Lift = ScreenUp.Cross(Fwd);
            if (Lift.LengthSquared() < 1e-12) Lift = Vec3(0, 1, 0);
            Lift = Lift.Normalised();
            EmitBbox("radius", Ctr, Pnt, Figure.Blueprint.R0, Lift, 12);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Boolean)
        {
            // Phase 16: no live slot — the boolean consumed its inputs. Emit a label dim only (slot = -1).
            // Phase 20: dim line on the camera-facing side of the top face.
            Vec3 Side = CameraFacingSide(Vec3(0, 1, 0));
            double Yface = (Side.Y > 0) ? B.High.Y : B.Low.Y;
            Vec3 YNormal = (Side.Y > 0) ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
            EmitBbox("boolean", Vec3(B.Low.X, Yface, Centre.Z), Vec3(B.High.X, Yface, Centre.Z), 0.0, YNormal, -1);
        }
        else if (Figure.Blueprint.Form == SceneFigure::ParametricForm::Extrude)
        {
            // Phase 16: live length dim. Slot 14 = R2 (the extrude length).
            // Phase 20: pick the perpendicular side that faces the camera. The dim endpoints sit
            //    on the body's footprint edge (Foot and Top in world space — the rectangle the
            //    extrude sweeps). The renderer lifts by 4 cm in Perp, so the line ends up just
            //    outside the body silhouette, on the camera-facing side.
            Vec3 AxisU = Figure.Blueprint.Axis.LengthSquared() > 1e-12 ? Figure.Blueprint.Axis.Normalised() : Vec3(0, 0, 1);
            Vec3 Perp = std::fabs(AxisU.Z) < 0.9 ? Vec3(0, 0, 1).Cross(AxisU).Normalised() : Vec3(1, 0, 0).Cross(AxisU).Normalised();
            // Find the corner of the footprint that is furthest in the camera direction — that's
            //    the corner closest to the viewer. The dim endpoints sit on the footprint at that
            //    edge, then the renderer lifts them in Perp.
            // For a simple extrude this is "Low + (extent in the Perp direction)"; for a more
            //    general extrude we'd want the actual footprint polygon. Keep it simple: just use
            //    the bbox corner.
            Vec3 Corner = B.Low + Perp * std::fabs((B.High - B.Low).Dot(Perp));
            Corner = B.Low + (Corner - B.Low) - AxisU * (Corner - B.Low).Dot(AxisU);     // project to footprint plane
            Vec3 DimStart = Corner;
            Vec3 DimEnd   = DimStart + AxisU * Figure.Blueprint.R2;
            // Flip Perp to face the camera.
            if (Perp.Dot(View.Forward()) < 0) Perp = -Perp;
            EmitBbox("length", DimStart, DimEnd, Figure.Blueprint.R2, Perp, 14);
        }
        else
        {
            // Generic fallback: bbox on X/Y/Z, all read-only (Slot = -1).
            const double Off = 0.05;
            EmitBbox("X", Vec3(B.Low.X, B.High.Y + Off, Centre.Z), Vec3(B.High.X, B.High.Y + Off, Centre.Z), B.High.X - B.Low.X, Vec3(0, 1, 0), -1);
            EmitBbox("Y", Vec3(B.High.X + Off, B.Low.Y, Centre.Z), Vec3(B.High.X + Off, B.High.Y, Centre.Z), B.High.Y - B.Low.Y, Vec3(1, 0, 0), -1);
            EmitBbox("Z", Vec3(B.High.X + Off, Centre.Y, B.Low.Z), Vec3(B.High.X + Off, Centre.Y, B.High.Z), B.High.Z - B.Low.Z, Vec3(1, 0, 0), -1);
        }
    }
}

bool ConsoleHost::ApplyLiveEdit(DimensionEntry& D, double NewValue) noexcept
{
    if (D.Slot < 0) return false;                                                     // not a live dim
    if (D.Anchor == 0) return false;                                                  // no figure anchor
    // Find the figure.
    SceneFigure* Fig = nullptr;
    for (auto& F : Scene.Figures()) { if (F.Identity == D.Anchor) { Fig = &F; break; } }
    if (Fig == nullptr) return false;
    SceneFigure::ParametricBlueprint& S = Fig->Blueprint;
    // Map slot index → field on the source, per form.
    using Form = SceneFigure::ParametricForm;
    auto SetVec3 = [&](Vec3& V, int Field)
    {
        if (Field == 0) V.X = NewValue; else if (Field == 1) V.Y = NewValue; else V.Z = NewValue;
    };
    // Slot mapping: each slot is encoded as (VecField, Component) or scalar.
    // We define an internal encoding: slot < 100 means a scalar slot (R0=12, R1=13, R2=14, R3=15, I0=16, I1=17).
    // slot >= 100 means a vec3 slot: 100 = A, 101 = B, 102 = C, 103 = Axis, 104 = Normal, 105 = MajorDirection.
    // The component (X/Y/Z) is stored in D.Slot mod 3: 100+X=0, 100+Y=1, 100+Z=2.
    // To keep the slot integer small and uniform, we re-encode: 0..2 = A.X/Y/Z, 3..5 = B.X/Y/Z,
    //    6..8 = C.X/Y/Z, 9..11 = Axis.X/Y/Z, 12 = R0, 13 = R1, 14 = R2, 15 = R3, 16 = I0, 17 = I1.
    // That's the encoding used in AutoEmitDimensions.
    if (D.Slot <= 2) SetVec3(S.A, D.Slot);
    else if (D.Slot <= 5) SetVec3(S.B, D.Slot - 3);
    else if (D.Slot <= 8) SetVec3(S.C, D.Slot - 6);
    else if (D.Slot <= 11) SetVec3(S.Axis, D.Slot - 9);
    else if (D.Slot == 12) S.R0 = NewValue;
    else if (D.Slot == 13) S.R1 = NewValue;
    else if (D.Slot == 14) S.R2 = NewValue;
    else if (D.Slot == 15) S.R3 = NewValue;
    else if (D.Slot == 16) S.I0 = int(std::lround(NewValue));
    else if (D.Slot == 17) S.I1 = int(std::lround(NewValue));
    else if (D.Slot >= 18)
    {
        // Phase 15: per-vertex polyline live edit. Slot 18+K*3 = PolylinePoints[K].X, +1 = Y, +2 = Z.
        int K = (D.Slot - 18) / 3;
        int Comp = (D.Slot - 18) % 3;
        if (K < 0 || K >= (int)S.PolylinePoints.size()) return false;
        if      (Comp == 0) S.PolylinePoints[K].X = NewValue;
        else if (Comp == 1) S.PolylinePoints[K].Y = NewValue;
        else if (Comp == 2) S.PolylinePoints[K].Z = NewValue;
        else return false;
    }
    else return false;
    // Rebuild the figure. The rebuild path is dispatched on S.Form.
    // We rebuild by replacing the figure's Body / Curve / Surface with the new geometry produced by the
    //    same primitive constructor (BrepBody::Box, BrepBody::Cylinder, etc.).
    bool Replaced = false;
    // Helper: take ownership of a Deliver<BrepBody> payload (refusal ⇒ return false).
    auto TakeBody = [&](Deliver<BrepBody> D) -> bool
    {
        if (!D) return false;
        Fig->Body = std::move(D.Payload);
        return true;
    };
    auto TakeCurve = [&](Deliver<NurbsCurve> D) -> bool
    {
        if (!D) return false;
        Fig->Curve = std::move(D.Payload);
        return true;
    };
    switch (S.Form)
    {
        case Form::Box:
            Replaced = TakeBody(BrepBody::Box(S.A, S.B));
            break;
        case Form::Sphere:
            Replaced = TakeBody(BrepBody::Sphere(S.A, S.R0));
            break;
        case Form::Cylinder:
        {
            Vec3 Axis = S.Axis.LengthSquared() > 1e-12 ? S.Axis.Normalised() : Vec3(0, 0, 1);
            Replaced = TakeBody(BrepBody::Cylinder(S.A, Axis, S.R0, S.R2));
            break;
        }
        case Form::Cone:
        {
            Vec3 Axis = S.Axis.LengthSquared() > 1e-12 ? S.Axis.Normalised() : Vec3(0, 0, 1);
            Replaced = TakeBody(BrepBody::Cone(S.A, Axis, S.R0, S.R1, S.R2));
            break;
        }
        case Form::Torus:
        {
            Vec3 Axis = S.Axis.LengthSquared() > 1e-12 ? S.Axis.Normalised() : Vec3(0, 0, 1);
            Replaced = TakeBody(BrepBody::Torus(S.A, Axis, S.R2, S.R3));
            break;
        }
        case Form::Line:
            Replaced = TakeCurve(NurbsCurve::Line(S.A, S.B));
            break;
        case Form::Circle:
        {
            Vec3 Nrm = S.Normal.LengthSquared() > 1e-12 ? S.Normal.Normalised() : Vec3(0, 0, 1);
            Replaced = TakeCurve(NurbsCurve::Circle(S.A, Nrm, S.R0));
            break;
        }
        case Form::Arc:
        {
            Vec3 Nrm = S.Normal.LengthSquared() > 1e-12 ? S.Normal.Normalised() : Vec3(0, 0, 1);
            // I0 = start angle (deg), R3 = sweep (deg). I0/R3 → degrees.
            double StartDeg = double(S.I0);
            double SweepDeg = S.R3;
            Replaced = TakeCurve(NurbsCurve::Arc(S.A, Nrm, S.R0, StartDeg, SweepDeg));
            break;
        }
        case Form::Ellipse:
        {
            Vec3 Nrm = S.Normal.LengthSquared() > 1e-12 ? S.Normal.Normalised() : Vec3(0, 0, 1);
            Vec3 XDir = S.MajorDirection.LengthSquared() > 1e-12 ? S.MajorDirection.Normalised() : Vec3(1, 0, 0);
            Replaced = TakeCurve(NurbsCurve::Ellipse(S.A, Nrm, XDir, S.R0, S.R1));
            break;
        }
        case Form::Polyline:
            Replaced = TakeCurve(NurbsCurve::Polyline(S.PolylinePoints, S.Closed));
            break;
        case Form::Spline:
            // I0 = spline degree; default 3 if the source is missing.
            Replaced = TakeCurve(NurbsCurve::Interpolate(S.PolylinePoints, S.I0 > 0 ? S.I0 : 3, S.Closed));
            break;
        case Form::Rectangle:
        {
            // Rectangle = closed polyline on the workplane (XY by default).
            std::vector<Vec3> Pts = { S.A, Vec3(S.B.X, S.A.Y, S.A.Z), S.B, Vec3(S.A.X, S.B.Y, S.A.Z) };
            Replaced = TakeCurve(NurbsCurve::Polyline(Pts, true));
            break;
        }
        case Form::Extrude:
        {
            // Rebuild by re-applying the recipe with the new extrude distance.
            Vec3 Axis = S.Axis.LengthSquared() > 1e-12 ? S.Axis.Normalised() : Vec3(0, 0, 1);
            Fig->Recipe.Length = S.R2;
            Fig->Recipe.Direction = Axis;
            Deliver<FigureRecipe::Product> P = Fig->Recipe.Produce(Scene, Plane);
            if (!P || !P.Payload.IsBody) return false;
            Fig->Body = std::move(P.Payload.Body);
            Replaced = true;
            break;
        }
        case Form::Revolve:
        {
            // Phase 16: live-edit the angle. The Blueprint stores the angle in radians (so the dim
            //    emit can convert to degrees with ScalarCriteria::Degrees); the user passes a value
            //    in degrees, so convert on edit and update S.R0 to match.
            S.R0 = ScalarCriteria::Radians(S.R0);
            Fig->Recipe.AxisOrigin = S.A;
            Fig->Recipe.Axis = (S.B.LengthSquared() > 1e-12 ? S.B.Normalised() : Vec3(0, 1, 0));
            Fig->Recipe.Angle = S.R0;
            Deliver<FigureRecipe::Product> P = Fig->Recipe.Produce(Scene, Plane);
            if (!P) return false;
            if (P.Payload.IsBody) Fig->Body = std::move(P.Payload.Body);
            else                  Fig->Surface = std::move(P.Payload.Sheet);
            Replaced = true;
            break;
        }
        case Form::Loft:
        {
            // Phase 16: live-edit the degree (rebuilds the loft's V-degree via I0).
            Fig->Recipe.Loft.DegreeV = std::clamp(S.I0, 1, 7);
            Deliver<FigureRecipe::Product> P = Fig->Recipe.Produce(Scene, Plane);
            if (!P) return false;
            if (P.Payload.IsBody) Fig->Body = std::move(P.Payload.Body);
            else                  Fig->Surface = std::move(P.Payload.Sheet);
            Replaced = true;
            break;
        }
        case Form::Sweep:
        {
            // Phase 16: live-edit ScaleEnd / TwistAngle / Stations. Twist is stored in radians
            //    (so the dim emit can convert to degrees with ScalarCriteria::Degrees); the user
            //    passes degrees, so convert on edit and update S.R1 to match.
            S.R1 = ScalarCriteria::Radians(S.R1);
            Fig->Recipe.Sweep.ScaleEnd = S.R0;
            Fig->Recipe.Sweep.TwistAngle = S.R1;
            Fig->Recipe.Sweep.Stations = std::max(0, S.I0);
            Deliver<FigureRecipe::Product> P = Fig->Recipe.Produce(Scene, Plane);
            if (!P) return false;
            if (P.Payload.IsBody) Fig->Body = std::move(P.Payload.Body);
            else                  Fig->Surface = std::move(P.Payload.Sheet);
            Replaced = true;
            break;
        }
        case Form::Pipe:
        {
            // Phase 16: live-edit the radius.
            Fig->Recipe.Radius = std::max(1e-6, S.R0);
            Deliver<FigureRecipe::Product> P = Fig->Recipe.Produce(Scene, Plane);
            if (!P) return false;
            if (P.Payload.IsBody) Fig->Body = std::move(P.Payload.Body);
            else                  Fig->Surface = std::move(P.Payload.Sheet);
            Replaced = true;
            break;
        }
        case Form::Boolean:
        {
            // Phase 16: no live slot — the boolean consumed its inputs. Refuse so the dim is read-only.
            return false;
        }
        case Form::ChamferEdge:
        {
            // Re-apply the chamfer to the pre-operation body (so re-editing doesn't chain chamfers
            //    and break the edge count).
            BrepBody NewBody = S.PreOpBody;                                            // start from a clean copy
            int EdgeIdx = S.I0;
            double Dist = S.R0;
            if (EdgeIdx >= 0 && EdgeIdx < (int)NewBody.Edges.size())
            {
                Deliver<BrepBody> R = NewBody.ChamferEdge(EdgeIdx, Dist);
                if (!R) return false;
                NewBody = std::move(R.Payload);
            }
            Fig->Body = std::move(NewBody);
            Replaced = true;
            break;
        }
        default:
            return false;
    }
    if (!Replaced) return false;
    // Re-emit dims with the new geometry. `Bounds()` is a fresh query, no cache to invalidate.
    D.Value = NewValue;
    AutoEmitDimensions(*Fig);
    return true;
}

Vec2 ConsoleHost::WorldToScreen(Vec3 P) const noexcept
{
    // Project a world point to NDC, then map to pixel coordinates.
    double Aspect = double(Surface->Width()) / double(Surface->Height());
    Mat4 Clip = View.ProjectionMatrix(Aspect) * View.ViewMatrix();
    Vec4 H = Clip * Vec4(P.X, P.Y, P.Z, 1.0);
    Vec2 S; if (std::fabs(H.W) < 1e-12) { S.X = -1e9; S.Y = -1e9; return S; }
    double NdcX = H.X / H.W, NdcY = H.Y / H.W;
    S.X = (NdcX * 0.5 + 0.5) * Surface->Width();
    S.Y = (NdcY * 0.5 + 0.5) * Surface->Height();                                    // Vulkan: +Y down
    return S;
}

void ConsoleHost::ReemitAllDimensions() noexcept
{
    // Phase 15: undo/redo swap the Scene but the Dimensions vector lives here. Re-emit auto dims
    //    for every figure so the dim tree tracks the rolled-back state. Preserves user-added dims
    //    (Auto == false) and the Hidden flag.
    // First, drop every auto dim whose anchor is still in the scene.
    std::vector<uint32_t> PresentAnchors;
    for (const SceneFigure& F : Scene.Figures()) PresentAnchors.push_back(F.Identity);
    Dimensions.erase(std::remove_if(Dimensions.begin(), Dimensions.end(),
        [&](const DimensionEntry& D)
        {
            if (!D.Auto) return false;                                                 // keep user dims
            for (uint32_t A : PresentAnchors) if (A == D.Anchor) return true;            // drop auto dims of present figures
            return false;                                                               // drop auto dims of figures that no longer exist
        }), Dimensions.end());
    // Now re-emit the auto set for every figure (covers the post-undo state and any newly-resurrected figures).
    for (const SceneFigure& F : Scene.Figures()) AutoEmitDimensions(F);
}

void ConsoleHost::DrawDimensions() noexcept
{
    if (!ShowDimensions) return;                                                       // dims hidden by default (Phase 14 polish pending)
    if (Dimensions.empty()) return;
    // Phase 13 redo: Plasticity-style dims — white colour, world-space offset tight against the model
    //    (≈ 0.04 m = 4 cm above the surface), drawn as an overlay. The lift vector is *world space* (so
    //    the dim line stays parallel to the model surface, not to the screen plane), and the dim line
    //    is the original feature's endpoints lifted in N by a constant world-space amount, then a
    //    straight segment between them — so the line sits on the body face, not floating 22 px above
    //    the silhouette.
    SegmentStream DimSegments;
    PointStream   DimPoints;
    Vec3 CamRight = View.Right();
    Vec3 CamUp    = View.Up();
    Vec3 Forward  = View.Forward();
    // World size of a single screen pixel at the pivot depth (so the label / ticks look the right size
    //    at the dim's depth).
    double PivotDepth = 0.0;
    {
        // Pick the depth of the first non-hidden dim's anchor midpoint as a representative.
        for (const DimensionEntry& D : Dimensions) { if (D.Hidden) continue; PivotDepth = ((D.A + D.B) * 0.5 - View.Eye()).Dot(Forward); break; }
        if (PivotDepth < 0.05) PivotDepth = 0.05;
    }
    double HalfHeight = View.Orthographic ? View.OrthographicHalfHeight() : PivotDepth * std::tan(View.FovY * 0.5);
    double WpPx = 2.0 * HalfHeight / double(Surface->Height());                       // [m/px] at pivot depth
    const double LabelHeightPx = 18.0;
    const double TickPx        = 4.0;
    const double OffM          = 0.04;                                                // [m] world-space lift: 4 cm above the surface
    double Lh = LabelHeightPx * WpPx;
    double Th = TickPx * WpPx;

    for (DimensionEntry& D : Dimensions)
    {
        if (D.Hidden) continue;
        if (D.Label.empty()) D.Label = FormatDimensionLabel(D);

        if (D.Leader)
        {
            // Phase 17: leader dim. A is the feature point (e.g. face centroid or hole edge),
            //    B is the label position (free-floating in world space). Draw a line from A
            //    to B, a small dot at A, and the label at B. No extension lines, no ticks.
            Vec3 AB = D.B - D.A;
            double LeaderLen = AB.Length();
            if (LeaderLen < 1e-9) continue;
            Vec3 ABu = AB / LeaderLen;
            // Leader line: full length, no break for the label.
            DimSegments.Append(D.A, D.B);
            // Dot at the feature point (a small "+" cross so the leader's anchor reads).
            {
                Vec3 Right = View.Right(), Up = View.Up();
                if (std::fabs(Right.Dot(ABu)) > 0.95) Right = Up.Cross(ABu).Normalised();
                else                                Right = Right.Cross(ABu).Normalised();
                Up = ABu.Cross(Right).Normalised();
                double R = Th * 0.8;
                DimSegments.Append(D.A - Right * R, D.A + Right * R);
                DimSegments.Append(D.A - Up * R,    D.A + Up * R);
            }
            // Label at the label position, offset slightly so it sits past the end of the line.
            Vec3 LabelOrigin = D.B + ABu * (Lh * 0.3);
            double CharW = Lh * 0.6;
            double CharSpacing = CharW * 1.1;
            double TotalW = CharSpacing * double(D.Label.size());
            Vec3 Origin = LabelOrigin - CamRight * (TotalW * 0.5) + CamUp * (Lh * 0.2);
            for (size_t I = 0; I < D.Label.size(); ++I)
            {
                const uint8_t* Bmp = Glyph(D.Label[I]);
                double Cx = Origin.X + CamRight.X * (CharSpacing * double(I));
                double Cy = Origin.Y + CamRight.Y * (CharSpacing * double(I));
                double Cz = Origin.Z + CamRight.Z * (CharSpacing * double(I));
                for (int Row = 0; Row < 7; ++Row)
                {
                    uint8_t RowBmp = Bmp[Row];
                    for (int Col = 0; Col < 5; ++Col)
                        if (RowBmp & (1u << (4 - Col)))
                        {
                            Vec3 P{
                                Cx + CamRight.X * (Col * CharW * 0.2) - CamUp.X * (Row * Lh / 7.0),
                                Cy + CamRight.Y * (Col * CharW * 0.2) - CamUp.Y * (Row * Lh / 7.0),
                                Cz + CamRight.Z * (Col * CharW * 0.2) - CamUp.Z * (Row * Lh / 7.0) };
                            DimPoints.Append(P, PointGlyph::Square);
                        }
                }
            }
            continue;
        }

        // Direction along the feature in world space (A → B)
        Vec3 AB = D.B - D.A;
        double FeatureLen = AB.Length();
        if (FeatureLen < 1e-9) continue;
        Vec3 ABu = AB / FeatureLen;
        // Lift direction: the dim's declared normal, in world space. For Plasticity-style placement this
        //    is the face normal (e.g. +Y for the top face of a box, so the X dim runs along the top
        //    face). If the dim is parallel to the lift, fall back to screen-up.
        Vec3 Lift = D.N;
        if (Lift.LengthSquared() < 1e-12) Lift = CamUp;
        // Project the lift into the screen plane so the dim line doesn't go behind the model.
        Vec3 ScreenLift = Lift - Forward * Lift.Dot(Forward);
        if (ScreenLift.LengthSquared() < 1e-12) ScreenLift = CamUp;
        ScreenLift = ScreenLift.Normalised();
        // Make the lift point "upward" (toward +screen-Y) so the dim sits above the model, not below.
        if (ScreenLift.Dot(CamUp) < 0) ScreenLift = ScreenLift * -1.0;
        if (ScreenLift.Dot(CamUp) < 0.1 && ScreenLift.Dot(CamRight) < 0) ScreenLift = ScreenLift * -1.0;

        // World-space dim endpoints: lift the original endpoints by a constant world-space amount
        //    (OffM = 4 cm) along the *world* lift vector — so the line sits ON the surface, offset
        //    by 4 cm in world space (not 22 px in screen space).
        Vec3 L0 = D.A + Lift * OffM;
        Vec3 L1 = D.B + Lift * OffM;
        // Extension lines (from the original surface points up to the dim line)
        DimSegments.Append(D.A, L0);
        DimSegments.Append(D.B, L1);
        // Main dim line
        DimSegments.Append(L0, L1);
        // Ticks (perpendicular to the dim line, in the *world-space* feature plane, so they look
        //    correct from any angle). We build the perpendicular by crossing the feature direction
        //    with the lift vector, so the tick lies in the surface plane.
        Vec3 Perp = Lift.Cross(ABu);
        if (Perp.LengthSquared() < 1e-12) Perp = View.Up().Cross(ABu);
        if (Perp.LengthSquared() < 1e-12) Perp = Vec3(0, 1, 0);
        Perp = Perp.Normalised();
        Vec3 Tick = Perp * Th;
        DimSegments.Append(L0 - Tick, L0 + Tick);
        DimSegments.Append(L1 - Tick, L1 + Tick);

        // Label — a row of 5x7 glyphs, centred above the dim line midpoint, billboarded to the camera.
        Vec3 Mid = (L0 + L1) * 0.5;
        double CharW = Lh * 0.6;
        double CharSpacing = CharW * 1.1;
        double TotalW = CharSpacing * double(D.Label.size());
        Vec3 Origin = Mid - CamRight * (TotalW * 0.5) + CamUp * (Lh * 1.2);
        for (size_t I = 0; I < D.Label.size(); ++I)
        {
            const uint8_t* Bmp = Glyph(D.Label[I]);
            double Cx = Origin.X + CamRight.X * (CharSpacing * double(I));
            double Cy = Origin.Y + CamRight.Y * (CharSpacing * double(I));
            double Cz = Origin.Z + CamRight.Z * (CharSpacing * double(I));
            for (int Row = 0; Row < 7; ++Row)
            {
                uint8_t RowBmp = Bmp[Row];
                for (int Col = 0; Col < 5; ++Col)
                    if (RowBmp & (1u << (4 - Col)))
                    {
                        Vec3 P{
                            Cx + CamRight.X * (Col * CharW * 0.2) - CamUp.X * (Row * Lh / 7.0),
                            Cy + CamRight.Y * (Col * CharW * 0.2) - CamUp.Y * (Row * Lh / 7.0),
                            Cz + CamRight.Z * (Col * CharW * 0.2) - CamUp.Z * (Row * Lh / 7.0) };
                        DimPoints.Append(P, PointGlyph::Square);
                    }
            }
        }
    }
    // Plasticity-style: WHITE dims (was yellow 1.0, 0.85, 0.10 before). A faint dark backing keeps
    //    the line readable over bright matcaps.
    DrawRecord DimStyle = ScenePresentation::Tinted(0.10f, 0.10f, 0.10f, 0.85f);
    DimStyle.LineWidth = 3.5f; DimStyle.PointSize = 2.4f;
    DimStyle.Emissive = 0.0f;
    Surface->DrawSegments(DimSegments, DimStyle);
    Surface->DrawPoints(DimPoints, DimStyle);
    DrawRecord Hot = ScenePresentation::Tinted(1.0f, 1.0f, 1.0f, 1.0f);
    Hot.LineWidth = 1.6f; Hot.PointSize = 1.8f; Hot.Emissive = 0.6f;
    Surface->DrawSegments(DimSegments, Hot);
    Surface->DrawPoints(DimPoints, Hot);
}

int32_t ConsoleHost::FindDimensionAtPixel(double X, double Y) const noexcept
{
    // Cheap O(N) scan: compare against each dim's main line. Tolerances are generous (12 px) so
    //    the user can click a few pixels away from the dim line itself.
    for (const DimensionEntry& D : Dimensions)
    {
        if (D.Hidden) continue;
        Vec2 P0 = WorldToScreen(D.A);
        Vec2 P1 = WorldToScreen(D.B);
        if (P0.X < -1e7 || P1.X < -1e7) continue;
        Vec2 Mid{ (P0.X + P1.X) * 0.5, (P0.Y + P1.Y) * 0.5 };
        Vec2 Dir = P1 - P0;
        double Len = Dir.Length();
        Vec2 Unit = Len > 1e-6 ? Dir / Len : Vec2{ 1.0, 0.0 };
        Vec2 Norm{ -Unit.Y, Unit.X };
        const double Off = 22.0;
        Vec2 L0 = P0 + Norm * Off;
        Vec2 L1 = P1 + Norm * Off;
        // Distance from (X, Y) to segment L0–L1
        Vec2 AB = L1 - L0; double L = AB.Length();
        if (L < 1e-6) continue;
        double T = std::clamp(((X - L0.X) * AB.X + (Y - L0.Y) * AB.Y) / (L * L), 0.0, 1.0);
        double Dx = X - (L0.X + AB.X * T), Dy = Y - (L0.Y + AB.Y * T);
        if (Dx * Dx + Dy * Dy < 144.0) return int32_t(D.Id);                          // 12 px
    }
    return 0;
}


bool ConsoleHost::AddBody(const CommandLine& C, const char* Stem, Deliver<BrepBody> Result) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneFigure& Figure = Scene.AddBody(C.SwitchText("name").value_or(Stem), std::move(Result.Payload));
    DescribeFigure(Figure);
    BodyReport R = Figure.Body.Validate();
    if (!R.Solid()) Row("  ⚠ open %d  non-manifold %d  misoriented %d", R.OpenEdges, R.NonManifoldEdges, R.MisorientedEdges);
    // Phase 13 dims are hidden by default while the renderer is being polished (Phase 14). Use
    //    `dim show` to make them visible, or pass `--no-dim` to suppress auto-emit entirely.
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    return true;
}

bool ConsoleHost::AddBody(const CommandLine& C, const char* Stem, Deliver<BrepBody> Result, SceneFigure::ParametricBlueprint Source) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneFigure& Figure = Scene.AddBody(C.SwitchText("name").value_or(Stem), std::move(Result.Payload));
    Figure.Blueprint = std::move(Source);
    DescribeFigure(Figure);
    BodyReport R = Figure.Body.Validate();
    if (!R.Solid()) Row("  ⚠ open %d  non-manifold %d  misoriented %d", R.OpenEdges, R.NonManifoldEdges, R.MisorientedEdges);
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    return true;
}

bool ConsoleHost::AddCurve(const CommandLine& C, const char* Stem, Deliver<NurbsCurve> Result, SceneFigure::ParametricBlueprint Source) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneFigure& Figure = Scene.AddCurve(C.SwitchText("name").value_or(Stem), std::move(Result.Payload));
    Figure.Construction = C.Switch("construction");
    Figure.Blueprint = std::move(Source);
    DescribeFigure(Figure);
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    return true;
}

bool ConsoleHost::AddCurve(const CommandLine& C, const char* Stem, Deliver<NurbsCurve> Result) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneFigure& Figure = Scene.AddCurve(C.SwitchText("name").value_or(Stem), std::move(Result.Payload));
    Figure.Construction = C.Switch("construction");
    DescribeFigure(Figure);
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    return true;
}

bool ConsoleHost::AddSurface(const CommandLine& C, const char* Stem, Deliver<NurbsSurface> Result) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneFigure& Figure = Scene.AddSurface(C.SwitchText("name").value_or(Stem), std::move(Result.Payload));
    DescribeFigure(Figure);
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    return true;
}

// Create a derived figure from a recipe: build once now, keep the sources (Plasticity leaves the sketch in place), and
//    let Regenerate() follow them from then on.
bool ConsoleHost::AddDerived(const CommandLine& C, const char* Stem, FigureRecipe Recipe) noexcept
{
    Deliver<FigureRecipe::Product> P = Recipe.Produce(Scene, Plane);
    if (!P) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(P.Denial.Reason), P.Denial.Detail);
    Recipe.InputFingerprint = Recipe.FingerprintInputs(Scene, Plane);
    SceneFigure& Figure = P.Payload.IsBody ? Scene.AddBody(C.SwitchText("name").value_or(Stem), std::move(P.Payload.Body))
                                           : Scene.AddSurface(C.SwitchText("name").value_or(Stem), std::move(P.Payload.Sheet));
    Figure.Recipe = std::move(Recipe);
    DescribeFigure(Figure);
    Row("  ↳ %s", Figure.Recipe.Summary(Scene).c_str());
    if (Figure.Classification == FigureClassification::Body) { BodyReport R = Figure.Body.Validate(); if (!R.Solid()) Row("  ⚠ open %d  non-manifold %d  misoriented %d", R.OpenEdges, R.NonManifoldEdges, R.MisorientedEdges); }
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    return true;
}

bool ConsoleHost::AddDerived(const CommandLine& C, const char* Stem, FigureRecipe Recipe, SceneFigure::ParametricBlueprint Source) noexcept
{
    // Phase 16: derived-op live edit. Produce the body, then attach the Blueprint that records the
    //    recipe's scalar/vector inputs. `dim edit` on a Blueprint dim mutates the Blueprint, then
    //    ApplyLiveEdit re-produces the body from the recipe with the new value.
    // Phase 20: remember which source figures were consumed (so we can hide their auto-dim set
    //    — the user wants the source curve's "radius / length / arc" dims to vanish once the
    //    curve is consumed by an extrude / revolve / pipe / sweep / loft / boolean / bridge).
    std::vector<uint32_t> SourceIds;
    for (const RecipeInput& In : Recipe.Sections) for (uint32_t Id : In.Figures) SourceIds.push_back(Id);
    for (uint32_t Id : Recipe.Path.Figures) SourceIds.push_back(Id);
    Deliver<FigureRecipe::Product> P = Recipe.Produce(Scene, Plane);
    if (!P) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(P.Denial.Reason), P.Denial.Detail);
    Recipe.InputFingerprint = Recipe.FingerprintInputs(Scene, Plane);
    SceneFigure& Figure = P.Payload.IsBody ? Scene.AddBody(C.SwitchText("name").value_or(Stem), std::move(P.Payload.Body))
                                           : Scene.AddSurface(C.SwitchText("name").value_or(Stem), std::move(P.Payload.Sheet));
    Figure.Recipe = std::move(Recipe);
    Figure.Blueprint = std::move(Source);                                            // Phase 16: live-edit the figure from the Blueprint
    DescribeFigure(Figure);
    Row("  ↳ %s", Figure.Recipe.Summary(Scene).c_str());
    if (Figure.Classification == FigureClassification::Body) { BodyReport R = Figure.Body.Validate(); if (!R.Solid()) Row("  ⚠ open %d  non-manifold %d  misoriented %d", R.OpenEdges, R.NonManifoldEdges, R.MisorientedEdges); }
    if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(Figure);
    // Phase 20: hide the source figures' auto dim sets (they're consumed by this op, and the user
    //    doesn't want to see "C1 radius 1.000" AND "E1 length 2.000" — only the result's dims).
    for (uint32_t Id : SourceIds) for (DimensionEntry& D : Dimensions) if (D.Anchor == Id && D.Auto) D.Hidden = true;
    return true;
}

SceneFigure* ConsoleHost::Resolve(const std::string& Token) noexcept
{
    if (!Token.empty() && Token[0] == '#')
        if (auto Id = CommandCodec::ParseNumber(Token.substr(1))) return Scene.Find(static_cast<uint32_t>(*Id));
    if (SceneFigure* I = Scene.Find(Token)) return I;
    if (auto Id = CommandCodec::ParseNumber(Token)) return Scene.Find(static_cast<uint32_t>(*Id));
    return nullptr;
}

std::vector<SceneFigure*> ConsoleHost::ResolveMany(const CommandLine& C, size_t FirstIndex) noexcept
{
    std::vector<SceneFigure*> Out;
    if (C.Count() <= FirstIndex || (C.Count() == FirstIndex + 1 && C.Arguments[FirstIndex] == "selected"))
    {
        for (SceneFigure& I : Scene.Figures()) if (I.Selected) Out.push_back(&I);
        return Out;
    }
    if (C.Count() == FirstIndex + 1 && C.Arguments[FirstIndex] == "all")
    {
        for (SceneFigure& I : Scene.Figures()) Out.push_back(&I);
        return Out;
    }
    for (size_t I = FirstIndex; I < C.Count(); ++I)
        if (SceneFigure* Figure = Resolve(C.Arguments[I])) Out.push_back(Figure);
        else { Refuse("no figure '%s'", C.Arguments[I].c_str()); Out.clear(); return Out; }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER
//------------------------------------------------------------------------------------------------------------------------

void ConsoleHost::Render() noexcept
{
    Surface->BeginTarget(Backdrop);
    Surface->BindView(View.ToViewRecord(Surface->Width(), Surface->Height(), 1.0));
    Surface->DrawLattice();

    for (const SceneFigure& Figure : Scene.Figures())
    {
        if (Figure.Hidden || Figure.Classification != FigureClassification::Body) continue;
        DrawBody(Figure);
    }
    for (const SceneFigure& Figure : Scene.Figures())
    {
        if (Figure.Hidden || Figure.Classification != FigureClassification::Surface) continue;
        DrawRecord D = ScenePresentation::Tinted(Figure.Tint[0], Figure.Tint[1], Figure.Tint[2]);
        D.PickIdentity = SceneDocument::PickOf(Figure.Identity);
        D.Highlight = Figure.Selected ? 2.0f : (SceneDocument::IdentityOf(HoverPick) == Figure.Identity ? 1.0f : 0.0f);
        D.Matcap = Figure.Matcap;
        D.Shading = static_cast<uint8_t>(Shading);
        Surface->DrawSurface(ScenePresentation::SurfaceTriangles(Figure.Surface, 2e-3), D);
        if (ShowIsoCurves)
        {
            DrawRecord Iso = ScenePresentation::Tinted(0.10f, 0.11f, 0.13f, 0.28f); Iso.LineWidth = 1.0f; Iso.PickIdentity = SceneDocument::PickOf(Figure.Identity);
            Surface->DrawSegments(ScenePresentation::SurfaceIsoCurves(Figure.Surface, 8, 8), Iso);
        }
        if (ShowControlCages || Figure.Selected || Mode == SelectMode::Control) DrawControlPoints(Figure);
    }
    DrawAreas();
    for (const SceneFigure& Figure : Scene.Figures())
    {
        if (Figure.Hidden || Figure.Classification != FigureClassification::Curve) continue;
        DrawRecord D = Figure.Selected ? ScenePresentation::Tinted(1.0f, 0.62f, 0.20f) : ScenePresentation::Tinted(0.92f, 0.94f, 0.97f);
        D.LineWidth = Figure.Selected ? 2.5f : 2.0f;
        D.Dashed = Figure.Construction;
        D.PickIdentity = SceneDocument::PickOf(Figure.Identity);
        D.Highlight = Figure.Selected ? 2.0f : (SceneDocument::IdentityOf(HoverPick) == Figure.Identity ? 1.0f : 0.0f);
        Surface->DrawSegments(ScenePresentation::CurveSegments(Figure.Curve), D);
        if (ShowControlCages || Figure.Selected || Mode == SelectMode::Control) DrawControlPoints(Figure);
    }
    for (const SceneFigure& Figure : Scene.Figures())
    {
        if (Figure.Hidden || Figure.Classification != FigureClassification::Empty) continue;
        // Phase 19: Empty is a transform handle. We draw a small triad at the Blueprint.A position
        //    so the user can see and select it. The triad is drawn in the main pass (not the overlay)
        //    so it occludes correctly with other geometry.
        ScenePresentation::DrawEmpty(*Surface, Figure.Blueprint.A, 0.15);
    }

    Surface->BeginOverlay();
    DrawToolPreview();
    DrawDimensions();
    if (GizmoShown && (Scene.SelectedCount() + Scene.SelectedPoleCount() + Scene.SelectedFaceCount() + Scene.SelectedEdgeCount() > 0) && !Tool.Active())
    {
        if (!GizmoRig.Dragging()) RefreshGizmoPivot();
        GizmoRig.AimAt(View);
        GizmoRig.Draw(*Surface, View, Surface->Width(), Surface->Height());
    }
    ScenePresentation::DrawTriad(*Surface, View.OrthographicHalfHeight() * 0.12);
    Surface->EndTarget();
}

void ConsoleHost::DrawAreas() noexcept
{
    // Filled sketch areas: translucent sheet in the workplane, ear-clipped with holes, picked by their own identity.
    for (const SketchArea& A : Scene.Areas())
    {
        if (!A.Filled && !A.Selected && SceneDocument::IdentityOf(HoverPick) != A.Identity) continue;
        std::vector<Vec3> Pts; std::vector<std::vector<uint32_t>> Rings;
        auto Ring = [&](const NurbsCurve& C)
        {
            std::vector<Vec3> P; C.Tessellate(P, nullptr, 2e-3);
            if (P.size() > 1 && P.back().Coincident(P.front(), 1e-9)) P.pop_back();
            std::vector<uint32_t> R; for (const Vec3& Q : P) { R.push_back(uint32_t(Pts.size())); Pts.push_back(Q); }
            Rings.push_back(std::move(R));
        };
        Ring(A.Cell.Outer); for (const NurbsCurve& Hh : A.Cell.Holes) Ring(Hh);
        std::vector<uint32_t> Tri = TriangulatePlanarPolygon(Pts, Rings, A.Normal);
        if (Tri.empty()) continue;
        SurfaceStream S;
        for (const Vec3& P : Pts) { S.Positions.insert(S.Positions.end(), { float(P.X), float(P.Y), float(P.Z) }); S.Normals.insert(S.Normals.end(), { float(A.Normal.X), float(A.Normal.Y), float(A.Normal.Z) }); S.Parameters.insert(S.Parameters.end(), { 0.f, 0.f }); }
        S.Triangles = Tri;
        const bool Hover = SceneDocument::IdentityOf(HoverPick) == A.Identity;
        DrawRecord D = A.Selected ? ScenePresentation::Tinted(1.0f, 0.62f, 0.20f, 0.45f)
                     : A.Filled  ? ScenePresentation::Tinted(0.55f, 0.72f, 0.95f, Hover ? 0.42f : 0.28f)
                                 : ScenePresentation::Tinted(0.95f, 0.95f, 0.95f, 0.10f);            // unfilled: ghost only while hovered
        D.PickIdentity = SceneDocument::PickOf(A.Identity);
        D.Highlight = A.Selected ? 2.0f : (Hover ? 1.0f : 0.0f);
        Surface->DrawSurface(S, D);
    }
}

void ConsoleHost::DrawBody(const SceneFigure& Figure) noexcept
{
    const BrepBody& B = Figure.Body;
    const bool FigureHover = SceneDocument::IdentityOf(HoverPick) == Figure.Identity;
    for (size_t F = 0; F < B.Faces.size(); ++F)
    {
        BrepBody::FaceTriangles T = B.TessellateFace(int(F), 2e-3);
        SurfaceStream S;
        for (size_t I = 0; I < T.Positions.size(); ++I)
        {
            S.Positions.push_back(float(T.Positions[I].X)); S.Positions.push_back(float(T.Positions[I].Y)); S.Positions.push_back(float(T.Positions[I].Z));
            S.Normals.push_back(float(T.Normals[I].X)); S.Normals.push_back(float(T.Normals[I].Y)); S.Normals.push_back(float(T.Normals[I].Z));
            S.Parameters.push_back(float(T.Parameters[I].X)); S.Parameters.push_back(float(T.Parameters[I].Y));
        }
        S.Triangles = T.Triangles;
        DrawRecord D = ScenePresentation::Tinted(Figure.Tint[0], Figure.Tint[1], Figure.Tint[2]);
        D.PickIdentity = SceneDocument::PickOf(Figure.Identity, SceneDocument::PickPart::Face, int(F));
        const bool FaceSel = Figure.FaceSelected(int(F));
        const bool FaceHover = FigureHover && (Mode == SelectMode::Face ? SceneDocument::FaceOf(HoverPick) == int(F) : Mode == SelectMode::Whole);
        D.Highlight = (Figure.Selected || FaceSel) ? 2.0f : (FaceHover ? 1.0f : 0.0f);
        D.Matcap = Figure.Matcap;
        D.Shading = static_cast<uint8_t>(Shading);
        Surface->DrawSurface(S, D);
    }
    for (size_t E = 0; E < B.Edges.size(); ++E)
    {
        std::vector<Vec3> P = B.EdgePolyline(int(E));
        SegmentStream Seg; for (size_t I = 0; I + 1 < P.size(); ++I) Seg.Append(P[I], P[I + 1]);
        const bool EdgeSel = Figure.EdgeSelected(int(E));
        const bool EdgeHover = FigureHover && Mode == SelectMode::Edge && SceneDocument::EdgeOf(HoverPick) == int(E);
        DrawRecord D = EdgeSel ? ScenePresentation::Tinted(1.0f, 0.62f, 0.20f) : ScenePresentation::Tinted(0.08f, 0.09f, 0.11f, 0.9f);
        D.LineWidth = EdgeSel ? 4.0f : (EdgeHover ? 3.0f : 1.5f);
        D.Highlight = EdgeSel ? 2.0f : (EdgeHover ? 1.0f : 0.0f);
        D.PickIdentity = SceneDocument::PickOf(Figure.Identity, SceneDocument::PickPart::Edge, int(E));
        Surface->DrawSegments(Seg, D);
    }
}

void ConsoleHost::DrawControlPoints(const SceneFigure& Figure) noexcept
{
    DrawRecord Cage = ScenePresentation::Tinted(0.95f, 0.80f, 0.30f, 0.9f); Cage.LineWidth = 1.0f; Cage.Dashed = true; Cage.PointSize = 7.0f;
    if (Figure.Classification == FigureClassification::Curve) Surface->DrawSegments(ScenePresentation::ControlPolygon(Figure.Curve), Cage);
    else Surface->DrawSegments(ScenePresentation::ControlNet(Figure.Surface), Cage);
    // One draw per pole so each carries its own pick identity and highlight (still a handful of quads).
    const int N = Figure.PoleCount();
    for (int P = 0; P < N; ++P)
    {
        PointStream One; One.Append(Figure.PolePosition(P), PointGlyph::Square);
        DrawRecord D = Cage;
        D.PickIdentity = SceneDocument::PickOf(Figure.Identity, P);
        const bool Sel = Figure.PoleSelected(P);
        D.Highlight = Sel ? 2.0f : (HoverPick == D.PickIdentity ? 1.0f : 0.0f);
        D.PointSize = Sel ? 9.0f : 7.0f;
        Surface->DrawPoints(One, D);
    }
}

Vec3 ConsoleHost::SelectionPivot() const noexcept
{
    if (Mode == SelectMode::Control && Scene.SelectedPoleCount() > 0)
    {
        Vec3 Sum; int N = 0;
        for (const SceneFigure& I : Scene.Figures()) for (int P : I.SelectedPoles) { Sum = Sum + I.PolePosition(P); ++N; }
        return Sum * (1.0 / N);
    }
    if (Scene.SelectedFaceCount() + Scene.SelectedEdgeCount() > 0)
    {
        Box3 B;
        for (const SceneFigure& I : Scene.Figures())
        {
            for (int F : I.SelectedFaces) B.Include(I.Body.Faces[F].Surface.Bounds());
            for (int E : I.SelectedEdges) B.Include(I.Body.Edges[E].Curve.Bounds());
        }
        if (!B.Empty()) return B.Centre();
    }
    return Scene.Bounds(true).Centre();
}

void ConsoleHost::RefreshGizmoPivot() noexcept
{
    TransformGizmo::PivotBasis F; F.Origin = SelectionPivot();
    GizmoRig.Anchor(F);
    GizmoRig.AimAt(View);
}

void ConsoleHost::ApplyDeltaToSelection(const Mat4& Delta) noexcept
{
    for (auto& [Id, Original] : GizmoOriginals)
        if (SceneFigure* I = Scene.Find(Id))
        {
            if (Mode == SelectMode::Control && !Original.SelectedPoles.empty())
            {
                for (int P : Original.SelectedPoles) I->MovePole(P, Delta.TransformPoint(Original.PolePosition(P)));
            }
            else { SceneFigure Fresh = Original; Fresh.Transform(Delta); I->Curve = std::move(Fresh.Curve); I->Surface = std::move(Fresh.Surface); I->Body = std::move(Fresh.Body); }
        }
}

void ConsoleHost::ApplyGizmoDelta(const Mat4& Delta) noexcept { ApplyDeltaToSelection(Delta); }

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMMANDS
//------------------------------------------------------------------------------------------------------------------------

//---- Phase 18: constraint graph helpers ---------------------------------------------------------------------------
// ParsePointRef takes a token like "L1.start", "C1.centre", "R0.corner2", "P0.vertex3" and resolves
//    it to a PointRef + the figure name + a (slot, sub, comp) tuple that names the Blueprint cell.
//    The (slot, sub, comp) tuple is the host's bookkeeping: which Blueprint field holds the (x, y) value
//    of this point. We use a uniform scheme: slot 0 = A, slot 1 = B, slot 2 = C, slot 3 = PolylinePoints[K]
//    (sub = K, comp = 0/1/2 for X/Y/Z), slot 4 = Rectangle corner K (sub = K, comp = 0/1/2). Circle centre
//    is slot 0; circle radius-point is slot 1 (we synthesise slot 1 = A + Normal*R0 if not present).
bool ConsoleHost::ParsePointRef(const std::string& Tok, PointRef& Out, std::string& Figure, int& Slot, int& SubIndex, int& Component) const noexcept
{
    auto Dot = Tok.find('.');
    if (Dot == std::string::npos) return false;
    Figure = Tok.substr(0, Dot);
    std::string Part = Tok.substr(Dot + 1);
    // Find the figure in the scene.
    const SceneFigure* F = nullptr;
    for (const auto& SF : Scene.Figures()) if (SF.Name == Figure) { F = &SF; break; }
    if (!F) return false;
    // Decode the part.
    if (Part == "start")
    {
        Out = {Figure, PointRefKind::LineStart, 0};
        Slot = 0; SubIndex = 0; Component = 0;
        return true;
    }
    if (Part == "end")
    {
        Out = {Figure, PointRefKind::LineEnd, 0};
        Slot = 1; SubIndex = 0; Component = 0;
        return true;
    }
    if (Part == "centre")
    {
        Out = {Figure, PointRefKind::CircleCentre, 0};
        Slot = 0; SubIndex = 0; Component = 0;
        return true;
    }
    if (Part == "point")
    {
        // The "point on circumference" of a circle. We use slot 1 = A + Normal*R0 (synthesised by ReadBlueprintPoint).
        Out = {Figure, PointRefKind::CircleRadiusPoint, 0};
        Slot = 1; SubIndex = 0; Component = 0;
        return true;
    }
    // vertexK — for polylines.
    if (Part.size() > 6 && Part.substr(0, 6) == "vertex")
    {
        int K = 0; try { K = std::stoi(Part.substr(6)); } catch (...) { return false; }
        Out = {Figure, PointRefKind::PolylineVertex, K};
        Slot = 3; SubIndex = K; Component = 0;
        return true;
    }
    // cornerK — for rectangles.
    if (Part.size() > 6 && Part.substr(0, 6) == "corner")
    {
        int K = 0; try { K = std::stoi(Part.substr(6)); } catch (...) { return false; }
        Out = {Figure, PointRefKind::RectCorner, K};
        Slot = 4; SubIndex = K; Component = 0;
        return true;
    }
    return false;
}

// ParseLineRef takes a single figure name (lines, polylines, circles-with-axis, etc. all qualify).
bool ConsoleHost::ParseLineRef(const std::string& Tok, std::string& Figure) const noexcept
{
    Figure = Tok;
    for (const auto& SF : Scene.Figures()) if (SF.Name == Figure) return true;
    return false;
}

// ReadBlueprintPoint returns the 3D world-space position of a point named by (slot, sub, comp).
//    The mapping is the inverse of the (slot, sub, comp) tuple produced by ParsePointRef.
Vec3 ConsoleHost::ReadBlueprintPoint(SceneFigure& F, int Slot, int SubIndex, int /*Component*/) const noexcept
{
    const auto& B = F.Blueprint;
    if (Slot == 0) return B.A;
    if (Slot == 1) return B.B;
    if (Slot == 2) return B.C;
    if (Slot == 3)
    {
        // PolylinePoints[SubIndex]
        if (SubIndex < 0 || SubIndex >= int(B.PolylinePoints.size())) return Vec3{};
        return B.PolylinePoints[SubIndex];
    }
    if (Slot == 4)
    {
        // Rectangle corner K: derived from A and B.
        // Corners are: A, (A.x, B.y), B, (B.x, A.y) for a non-rotated rect.
        // We don't have a rotation field; the user can use rotation via the Blueprint's R0 if needed.
        Vec3 C0 = B.A, C2 = B.B;
        switch (SubIndex)
        {
            case 0: return C0;
            case 1: return Vec3(C0.X, C2.Y, C0.Z);
            case 2: return C2;
            case 3: return Vec3(C2.X, C0.Y, C0.Z);
        }
        return Vec3{};
    }
    if (Slot == 1 && B.Form == SceneFigure::ParametricForm::Circle)
    {
        // Slot 1 for a Circle is the radius point. We synthesise A + Normal*R0.
        Vec3 N = B.Normal.LengthSquared() > 1e-12 ? B.Normal.Normalised() : Vec3::UnitZ();
        return B.A + N * B.R0;
    }
    return Vec3{};
}

// WriteBlueprintPoint writes a 3D world-space position back into the Blueprint cell named by
//    (slot, sub, comp). We compute the workplane projection of NewWorld → 2D → NewWorld' = Origin + u*x + v*y.
//    This keeps the point on the workplane after editing (so the constraint is actually satisfied in 3D too).
void ConsoleHost::WriteBlueprintPoint(SceneFigure& F, int Slot, int SubIndex, int /*Component*/, Vec3 NewWorld) noexcept
{
    auto& B = F.Blueprint;
    Vec2 P2 = Plane.ToLocal(NewWorld);
    Vec3 Clamped = Plane.ToWorld(P2);                                            // re-anchor on the plane
    if (Slot == 0) B.A = Clamped;
    else if (Slot == 1) B.B = Clamped;
    else if (Slot == 2) B.C = Clamped;
    else if (Slot == 3)
    {
        if (SubIndex >= 0 && SubIndex < int(B.PolylinePoints.size())) B.PolylinePoints[SubIndex] = Clamped;
    }
    // Slot 4 (rectangle corner) and slot 1 of Circle (radius point) are derived — we don't write back.
}

// SolveConstraintGraph materialises the persistent graph into a ConstraintSolver, solves, and writes
//    the solved (x, y) back into the figure Blueprints. The figures are then rebuilt (each Line / Circle
//    / Rectangle / Polyline is re-constructed from its Blueprint by the same code path that dim-edit uses).
bool ConsoleHost::SolveConstraintGraph() noexcept
{
    if (CGraph.ConstraintCount() == 0) return Refuse("constraint solve: graph is empty");
    ConstraintSolver S;
    // Materialise anchors: read each anchor's current 2D position from the figure, push as an unknown.
    for (const auto& A : CGraph.AllAnchors())
    {
        SceneFigure* F = nullptr;
        for (auto& SF : Scene.Figures()) if (SF.Name == A.Figure) { F = &SF; break; }
        if (!F) return Refuse("constraint solve: figure '%s' (anchor for constraint) not found", A.Figure.c_str());
        Vec3 W3 = ReadBlueprintPoint(*F, A.Slot, A.SubIndex, A.Component);
        Vec2 P2 = Plane.ToLocal(W3);
        SketchUnknown U; U.Ref = A.Ref; U.X = P2.X; U.Y = P2.Y; U.Fixed = A.Fixed;
        S.SetUnknown(U);
    }
    for (const auto& E : CGraph.AllConstraints()) S.AddConstraint(E.C);
    SolveReport R = S.Solve();
    if (!R.Converged) return Refuse("constraint solve: %s", R.Refusal.c_str());
    // Write the solved (x, y) back to the figure Blueprints.
    for (size_t I = 0; I < S.AllUnknowns().size(); ++I)
    {
        const auto& U = S.AllUnknowns()[I];
        if (U.Fixed) continue;
        // Find the anchor for this unknown.
        size_t AIdx = CGraph.AnchorIndex(U.Ref);
        if (AIdx == SIZE_MAX) continue;
        const auto& A = CGraph.AllAnchors()[AIdx];
        // Find the figure.
        SceneFigure* F = nullptr;
        for (auto& SF : Scene.Figures()) if (SF.Name == A.Figure) { F = &SF; break; }
        if (!F) continue;
        // Project the solved 2D back to 3D on the workplane, then write.
        Vec3 NewWorld = Plane.ToWorld(Vec2(U.X, U.Y));
        WriteBlueprintPoint(*F, A.Slot, A.SubIndex, A.Component, NewWorld);
    }
    // Rebuild the figures. We use the ApplyLiveEdit path indirectly: re-construct from the Blueprint.
    //    The easiest way is to call the per-Form rebuild inside the ApplyLiveEdit function — but that's
    //    private. Instead, we re-run the ApplyLiveEdit equivalent by invoking `rebuild <fig>` for each
    //    figure that has at least one anchor. For simplicity we delegate to a helper.
    for (auto& F : Scene.Figures())
    {
        bool Has = false;
        for (const auto& A : CGraph.AllAnchors()) if (A.Figure == F.Name) { Has = true; break; }
        if (Has) RebuildFigureFromBlueprint(F);
    }
    Row("constraint solve: converged=%s, iters=%d, residual=%.3e, dof=%d", R.Converged ? "true" : "false", R.Iterations, R.ResidualL2, R.DoF);
    return true;
}

void ConsoleHost::RebuildFigureFromBlueprint(SceneFigure& F) noexcept
{
    // We invoke the same primitive constructors that ApplyLiveEdit uses, but without a dim slot. The
    //    builders take the Blueprint A/B/C/Axis/Normal/R0..R3 and produce a new Curve or Body. We
    //    replace F.Curve or F.Body with the new one.
    using Form = SceneFigure::ParametricForm;
    auto& B = F.Blueprint;
    if (B.Form == Form::Line)
    {
        Deliver<NurbsCurve> D = NurbsCurve::Line(B.A, B.B);
        if (D) F.Curve = std::move(D.Payload);
    }
    else if (B.Form == Form::Polyline)
    {
        if (!B.PolylinePoints.empty())
        {
            Deliver<NurbsCurve> D = NurbsCurve::Polyline(B.PolylinePoints, B.Closed);
            if (D) F.Curve = std::move(D.Payload);
        }
    }
    else if (B.Form == Form::Circle)
    {
        Vec3 N = B.Normal.LengthSquared() > 1e-12 ? B.Normal.Normalised() : Vec3::UnitZ();
        Deliver<NurbsCurve> D = NurbsCurve::Circle(B.A, N, B.R0);
        if (D) F.Curve = std::move(D.Payload);
    }
    else if (B.Form == Form::Rectangle)
    {
        // Build a polyline with 4 corners.
        Vec3 A = B.A, B2 = B.B;
        std::vector<Vec3> Pts = { A, Vec3(A.X, B2.Y, A.Z), B2, Vec3(B2.X, A.Y, A.Z), A };
        Deliver<NurbsCurve> D = NurbsCurve::Polyline(Pts, true);
        if (D) F.Curve = std::move(D.Payload);
    }
    // Phase 19: extend to body forms so mirror / radial can rebuild bodies too.
    else if (B.Form == Form::Box)        { Deliver<BrepBody> D = BrepBody::Box(B.A, B.B); if (D) F.Body = std::move(D.Payload); }
    else if (B.Form == Form::Sphere)     { Deliver<BrepBody> D = BrepBody::Sphere(B.A, B.R0); if (D) F.Body = std::move(D.Payload); }
    else if (B.Form == Form::Cylinder)   { Vec3 Axis = B.Axis.LengthSquared() > 1e-12 ? B.Axis.Normalised() : Vec3(0, 0, 1); Deliver<BrepBody> D = BrepBody::Cylinder(B.A, Axis, B.R0, B.R2); if (D) F.Body = std::move(D.Payload); }
    else if (B.Form == Form::Cone)       { Vec3 Axis = B.Axis.LengthSquared() > 1e-12 ? B.Axis.Normalised() : Vec3(0, 0, 1); Deliver<BrepBody> D = BrepBody::Cone(B.A, Axis, B.R0, B.R1, B.R2); if (D) F.Body = std::move(D.Payload); }
    else if (B.Form == Form::Torus)      { Vec3 Axis = B.Axis.LengthSquared() > 1e-12 ? B.Axis.Normalised() : Vec3(0, 0, 1); Deliver<BrepBody> D = BrepBody::Torus(B.A, Axis, B.R2, B.R3); if (D) F.Body = std::move(D.Payload); }
    else if (B.Form == Form::Empty)
    {
        Deliver<NurbsCurve> D = NurbsCurve::Empty();
        if (D) F.Curve = std::move(D.Payload);
    }
}

//---- Phase 19: mirror + radial + empty helpers -------------------------------------------------------------------

// Parse a "plane or axis" specifier. Accepts:
//    "xy" / "xz" / "yz" — world workplane
//    a named workplane (e.g. "P_top") — looked up in NamedPlanes
//    "((ox,oy,oz),(dx,dy,dz))" — an axis (3D line through origin in direction)
//    "(nx,ny,nz)" — a plane through the origin with that normal
// On success, fills out a list of MirrorOps (one per parsed op) and returns true. For a single
//    workplane the result is one plane op. For a single axis spec it is one axis op. For two --across/--also
//    flags the caller appends; we return the new ops via a callback so the verb can chain.
//    MirrorSpec is a nested type of ConsoleHost; declared in ConsoleHost.h.
[[nodiscard]] bool ConsoleHost::ParseMirrorSpec(const std::string& Tok, MirrorSpec& Out) const noexcept
{
    Out = {};
    Out.Label = Tok;
    if (Tok == "xy" || Tok == "XY") { Out.Kind = MirrorSpec::Kind::Plane; Out.Origin = Vec3{}; Out.NormalOrDir = Vec3::UnitZ(); return true; }
    if (Tok == "xz" || Tok == "XZ") { Out.Kind = MirrorSpec::Kind::Plane; Out.Origin = Vec3{}; Out.NormalOrDir = Vec3::UnitY(); return true; }
    if (Tok == "yz" || Tok == "YZ") { Out.Kind = MirrorSpec::Kind::Plane; Out.Origin = Vec3{}; Out.NormalOrDir = Vec3::UnitX(); return true; }
    // Named workplane.
    auto It = NamedPlanes.find(Tok);
    if (It != NamedPlanes.end()) { Out.Kind = MirrorSpec::Kind::Plane; Out.Origin = It->second.Origin; Out.NormalOrDir = It->second.Normal(); return true; }
    // (a,b,c) — a plane through the origin with that normal.
    if (Tok.size() > 1 && Tok.front() == '(' && Tok.back() == ')')
    {
        std::string Inner = Tok.substr(1, Tok.size() - 2);
        // Try to split on "),(" for axis form, or just a point for plane form.
        auto P1 = CommandCodec::ParsePoint(Inner);
        if (P1)
        {
            // Check if the inner is a single point (plane normal) or an axis.
            //    An axis form is "(origin),(dir)" — look for "),(" inside.
            size_t Sep = Inner.find("),(");
            if (Sep != std::string::npos)
            {
                std::string OStr = Inner.substr(0, Sep + 1);
                std::string DStr = Inner.substr(Sep + 2);
                auto O = CommandCodec::ParsePoint(OStr);
                auto D = CommandCodec::ParsePoint(DStr);
                if (O && D) { Out.Kind = MirrorSpec::Kind::Axis; Out.Origin = *O; Out.NormalOrDir = (*D - *O).Normalised(); return true; }
            }
            // Plane normal through origin.
            Out.Kind = MirrorSpec::Kind::Plane; Out.Origin = Vec3{}; Out.NormalOrDir = P1->Normalised(); return true;
        }
    }
    return false;
}

// Apply a list of mirror specs to a single 3D point. Composes the operations in order.
[[nodiscard]] Vec3 ConsoleHost::ApplyMirrors(Vec3 P, const std::vector<MirrorSpec>& Specs) const noexcept
{
    for (const ConsoleHost::MirrorSpec& S : Specs)
    {
        if (S.Kind == ConsoleHost::MirrorSpec::Kind::Plane) P = ReflectAcrossPlane(P, S.Origin, S.NormalOrDir);
        else                                                 P = ReflectAcrossAxis(P, { S.Origin, S.NormalOrDir });
    }
    return P;
}

// Reflect every position-bearing Blueprint cell of a figure. Returns true if the figure is one we
//    know how to reflect; false if the form is not yet supported.
[[nodiscard]] bool ConsoleHost::ReflectBlueprint(SceneFigure& F, const std::vector<MirrorSpec>& Specs) const noexcept
{
    auto Apply = [&](Vec3 P) { return ApplyMirrors(P, Specs); };
    auto& B = F.Blueprint;
    B.A = Apply(B.A);
    B.B = Apply(B.B);
    B.C = Apply(B.C);
    B.Axis = Apply(B.Axis);
    B.Normal = Apply(B.Normal);                                              // normal flips sign, not a real position
    for (auto& P : B.PolylinePoints) P = Apply(P);
    return true;
}

// Rotate every position-bearing Blueprint cell around an axis by θ radians. Mirrors the same set
//    of cells as ReflectBlueprint.
[[nodiscard]] bool ConsoleHost::RotateBlueprint(SceneFigure& F, MirrorAxis Axis, double ThetaRadians) const noexcept
{
    auto Apply = [&](Vec3 P) { return RotateAroundAxis(P, Axis, ThetaRadians); };
    auto& B = F.Blueprint;
    B.A = Apply(B.A);
    B.B = Apply(B.B);
    B.C = Apply(B.C);
    B.Axis = Apply(B.Axis);
    B.Normal = Apply(B.Normal);
    for (auto& P : B.PolylinePoints) P = Apply(P);
    return true;
}

// Create a copy of a figure, with every Blueprint cell reflected, and rebuild its geometry. Returns
//    the new figure by reference (added to the scene) or refuses.
[[nodiscard]] SceneFigure& ConsoleHost::MirrorFigureCopy(const SceneFigure& Source, const std::vector<ConsoleHost::MirrorSpec>& Specs, const std::string& NewName) noexcept
{
    // Deep-clone the source via Scene.Duplicate, then reflect + rebuild.
    SceneFigure& New = Scene.Duplicate(Source);
    New.Name = NewName;
    (void)ReflectBlueprint(New, Specs);
    RebuildFigureFromBlueprint(New);
    AutoEmitDimensions(New);
    return New;
}

// Rotate a copy of a figure around an axis. Same pattern as MirrorFigureCopy.
[[nodiscard]] SceneFigure& ConsoleHost::RadialFigureCopy(const SceneFigure& Source, MirrorAxis Axis, double ThetaRadians, const std::string& NewName) noexcept
{
    SceneFigure& New = Scene.Duplicate(Source);
    New.Name = NewName;
    (void)RotateBlueprint(New, Axis, ThetaRadians);
    RebuildFigureFromBlueprint(New);
    AutoEmitDimensions(New);
    return New;
}

// Add an Empty figure to the scene. The position is stored in Blueprint.A; the Curve is the
//    zero-length placeholder from NurbsCurve::Empty().
[[nodiscard]] SceneFigure& ConsoleHost::AddEmpty(Vec3 Position, const std::string& Name) noexcept
{
    Deliver<NurbsCurve> D = NurbsCurve::Empty();
    if (!D) return *Scene.Figures().begin();                                   // shouldn't happen, but be safe
    SceneFigure& E = Scene.AddCurve(Name, std::move(D.Payload));
    E.Classification = FigureClassification::Empty;
    E.Blueprint.Form = SceneFigure::ParametricForm::Empty;
    E.Blueprint.A = Position;
    return E;
}

void ConsoleHost::Register() noexcept
{
    auto Add = [&](const char* Verb, const char* Help, Command Fn) { Commands[Verb] = std::move(Fn); Usage[Verb] = Help; };
    auto Need = [&](const CommandLine& C, size_t N, const char* Verb) -> bool
    {
        if (C.Count() >= N) return true;
        return Refuse("%s: expected %zu argument(s) — usage: %s", Verb, N, Usage[Verb].c_str());
    };
    auto PointArg = [&](const CommandLine& C, size_t I, Vec3& Out, const char* Verb) -> bool
    {
        auto P = C.Point(I);
        if (!P) return Refuse("%s: argument %zu must be a point (x,y[,z])", Verb, I + 1);
        Out = *P; return true;
    };
    auto NumberArg = [&](const CommandLine& C, size_t I, double& Out, const char* Verb) -> bool
    {
        auto N = C.Number(I);
        if (!N) return Refuse("%s: argument %zu must be a number", Verb, I + 1);
        Out = *N; return true;
    };
    // Points given as (x,y) are lifted onto the active workplane; (x,y,z) are world.
    auto Lift = [&](const CommandLine& C, size_t I, Vec3& Out) -> bool
    {
        auto P = C.Point(I); if (!P) return false;
        bool Planar = C.Arguments[I].find(',') == C.Arguments[I].rfind(',');
        Out = Planar ? Plane.ToWorld({ P->X, P->Y }) : *P;
        return true;
    };

    //---------------------------------------------- sketch curves ----------------------------------------------
    Add("line", "line (x,y[,z]) (x,y[,z]) [--name=N] [--construction]", [=, this](const CommandLine& C)
    {
        Vec3 A, B; if (!Need(C, 2, "line") || !Lift(C, 0, A) || !Lift(C, 1, B)) return Refuse("line: two points required");
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Line; S.A = A; S.B = B;
        return AddCurve(C, "Line", NurbsCurve::Line(A, B), S);
    });
    Add("polyline", "polyline (x,y) (x,y) ... [--closed]", [=, this](const CommandLine& C)
    {
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 0; I < C.Count(); ++I) { if (!Lift(C, I, P)) return Refuse("polyline: argument %zu is not a point", I + 1); Pts.push_back(P); }
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Polyline; S.PolylinePoints = Pts; S.Closed = C.Switch("closed");
        if (Pts.size() >= 2) { S.A = Pts.front(); S.B = Pts.back(); }
        return AddCurve(C, "Polyline", NurbsCurve::Polyline(Pts, C.Switch("closed")), S);
    });
    Add("rect", "rect (x,y) (x,y) [--radius=R] [--center]", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "rect")) return false;
        auto A = C.Point2(0), B = C.Point2(1); if (!A || !B) return Refuse("rect: two planar points required");
        if (C.Switch("center")) { Vec2 Half = *B; B = *A + Half; A = *A - Half; }
        Vec3 A3 = Plane.ToWorld(*A), B3 = Plane.ToWorld(*B);
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Rectangle; S.A = A3; S.B = B3;
        return AddCurve(C, "Rectangle", NurbsCurve::Rectangle(Plane, *A, *B, C.SwitchNumber("radius").value_or(0.0)), S);
    });
    Add("polygon", "polygon (cx,cy) radius sides [--rotation=deg] [--circumscribed]", [=, this](const CommandLine& C)
    {
        double R = 0, N = 0; if (!Need(C, 3, "polygon") || !NumberArg(C, 1, R, "polygon") || !NumberArg(C, 2, N, "polygon")) return false;
        auto Ctr = C.Point2(0); if (!Ctr) return Refuse("polygon: centre must be a planar point");
        return AddCurve(C, "Polygon", NurbsCurve::Polygon(Plane, *Ctr, R, static_cast<int>(N), ScalarCriteria::Radians(C.SwitchNumber("rotation").value_or(0.0)), !C.Switch("circumscribed")));
    });
    Add("slot", "slot (ax,ay) (bx,by) radius", [=, this](const CommandLine& C)
    {
        double R = 0; if (!Need(C, 3, "slot") || !NumberArg(C, 2, R, "slot")) return false;
        auto A = C.Point2(0), B = C.Point2(1); if (!A || !B) return Refuse("slot: two planar centres required");
        return AddCurve(C, "Slot", NurbsCurve::Slot(Plane, *A, *B, R));
    });
    Add("circle", "circle (cx,cy[,cz]) radius [--normal=(x,y,z)]", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double R = 0; if (!Need(C, 2, "circle") || !Lift(C, 0, Ctr) || !NumberArg(C, 1, R, "circle")) return Refuse("circle: centre and radius required");
        Vec3 N = Plane.Normal(); if (auto F = C.SwitchText("normal")) if (auto V = CommandCodec::ParsePoint(*F)) N = *V;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Circle; S.A = Ctr; S.Normal = N; S.R0 = R;
        return AddCurve(C, "Circle", NurbsCurve::Circle(Ctr, N, R), S);
    });
    Add("arc", "arc (cx,cy) radius startDeg sweepDeg  |  arc --three (a) (b) (c)", [=, this](const CommandLine& C)
    {
        if (C.Switch("three"))
        {
            Vec3 A, B, D; if (!Need(C, 3, "arc") || !Lift(C, 0, A) || !Lift(C, 1, B) || !Lift(C, 2, D)) return Refuse("arc --three: three points required");
            // For ArcThreePoints, the analytic centre+radius is recoverable at the source level; we leave Form=None
            //    (those arcs are not yet live-editable through the centre+radius path; the user can re-arc them).
            return AddCurve(C, "Arc", NurbsCurve::ArcThreePoints(A, B, D));
        }
        Vec3 Ctr; double R = 0, S0 = 0, Sw = 0;
        if (!Need(C, 4, "arc") || !Lift(C, 0, Ctr) || !NumberArg(C, 1, R, "arc") || !NumberArg(C, 2, S0, "arc") || !NumberArg(C, 3, Sw, "arc")) return false;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Arc; S.A = Ctr; S.Normal = Plane.Normal(); S.R0 = R; S.I0 = int(std::lround(S0)); S.R3 = Sw;
        return AddCurve(C, "Arc", NurbsCurve::Arc(Ctr, Plane.Normal(), R, ScalarCriteria::Radians(S0), ScalarCriteria::Radians(Sw)), S);
    });
    Add("ellipse", "ellipse (cx,cy) radiusMajor radiusMinor [--rotation=deg]", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double A = 0, B = 0; if (!Need(C, 3, "ellipse") || !Lift(C, 0, Ctr) || !NumberArg(C, 1, A, "ellipse") || !NumberArg(C, 2, B, "ellipse")) return false;
        double Rot = ScalarCriteria::Radians(C.SwitchNumber("rotation").value_or(0.0));
        Vec3 Major = Plane.AxisX * std::cos(Rot) + Plane.AxisY * std::sin(Rot);
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Ellipse; S.A = Ctr; S.Normal = Plane.Normal(); S.MajorDirection = Major; S.R0 = A; S.R1 = B;
        return AddCurve(C, "Ellipse", NurbsCurve::Ellipse(Ctr, Plane.Normal(), Major, A, B), S);
    });
    Add("spline", "spline (p) (p) (p) ... [--degree=3] [--closed]   interpolating", [=, this](const CommandLine& C)
    {
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 0; I < C.Count(); ++I) { if (!Lift(C, I, P)) return Refuse("spline: argument %zu is not a point", I + 1); Pts.push_back(P); }
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Spline; S.PolylinePoints = Pts; S.Closed = C.Switch("closed"); S.I0 = static_cast<int>(C.SwitchNumber("degree").value_or(3));
        if (Pts.size() >= 2) { S.A = Pts.front(); S.B = Pts.back(); }
        return AddCurve(C, "Spline", NurbsCurve::Interpolate(Pts, S.I0, S.Closed), S);
    });
    Add("cpcurve", "cpcurve (p) (p) (p) ... [--degree=3] [--periodic]   control-point curve", [=, this](const CommandLine& C)
    {
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 0; I < C.Count(); ++I) { if (!Lift(C, I, P)) return Refuse("cpcurve: argument %zu is not a point", I + 1); Pts.push_back(P); }
        return AddCurve(C, "ControlCurve", NurbsCurve::ControlPoints(static_cast<int>(C.SwitchNumber("degree").value_or(3)), Pts, C.Switch("periodic")));
    });

    //---------------------------------------------- primitive surfaces ----------------------------------------------
    Add("box", "box (cornerA) (cornerB)  ·  box (corner) dx dy dz — solid body", [=, this](const CommandLine& C)
    {
        Vec3 A, B; if (!Need(C, 2, "box") || !PointArg(C, 0, A, "box")) return false;
        if (C.Count() >= 4) { double Dx = 0, Dy = 0, Dz = 0; if (!NumberArg(C, 1, Dx, "box") || !NumberArg(C, 2, Dy, "box") || !NumberArg(C, 3, Dz, "box")) return false; B = A + Vec3{ Dx, Dy, Dz }; }
        else if (!PointArg(C, 1, B, "box")) return false;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Box; S.A = A; S.B = B;
        return AddBody(C, "Box", BrepBody::Box(A, B), S);
    });
    Add("sphere", "sphere (cx,cy,cz) radius [--sheet]", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double R = 0; if (!Need(C, 2, "sphere") || !PointArg(C, 0, Ctr, "sphere") || !NumberArg(C, 1, R, "sphere")) return false;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Sphere; S.A = Ctr; S.R0 = R;
        if (C.Switch("sheet")) return AddSurface(C, "Sphere", NurbsSurface::Sphere(Ctr, R));
        return AddBody(C, "Sphere", BrepBody::Sphere(Ctr, R), S);
    });
    Add("cylinder", "cylinder (foot) radius height [--axis=(x,y,z)] [--sheet]", [=, this](const CommandLine& C)
    {
        Vec3 F; double R = 0, H = 0; if (!Need(C, 3, "cylinder") || !PointArg(C, 0, F, "cylinder") || !NumberArg(C, 1, R, "cylinder") || !NumberArg(C, 2, H, "cylinder")) return false;
        Vec3 Axis = Plane.Normal(); if (auto A = C.SwitchText("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Cylinder; S.A = F; S.Axis = Axis; S.R0 = R; S.R2 = H;
        if (C.Switch("sheet")) return AddSurface(C, "Cylinder", NurbsSurface::Cylinder(F, Axis, R, H));
        return AddBody(C, "Cylinder", BrepBody::Cylinder(F, Axis, R, H), S);
    });
    Add("cone", "cone (foot) radiusFoot radiusTop height [--axis=(x,y,z)] [--sheet]", [=, this](const CommandLine& C)
    {
        Vec3 F; double R0 = 0, R1 = 0, H = 0;
        if (!Need(C, 4, "cone") || !PointArg(C, 0, F, "cone") || !NumberArg(C, 1, R0, "cone") || !NumberArg(C, 2, R1, "cone") || !NumberArg(C, 3, H, "cone")) return false;
        Vec3 Axis = Plane.Normal(); if (auto A = C.SwitchText("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Cone; S.A = F; S.Axis = Axis; S.R0 = R0; S.R1 = R1; S.R2 = H;
        if (C.Switch("sheet")) return AddSurface(C, "Cone", NurbsSurface::Cone(F, Axis, R0, R1, H));
        return AddBody(C, "Cone", BrepBody::Cone(F, Axis, R0, R1, H), S);
    });
    Add("torus", "torus (centre) radiusMajor radiusMinor [--axis=(x,y,z)] [--sheet]", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double R0 = 0, R1 = 0; if (!Need(C, 3, "torus") || !PointArg(C, 0, Ctr, "torus") || !NumberArg(C, 1, R0, "torus") || !NumberArg(C, 2, R1, "torus")) return false;
        Vec3 Axis = Plane.Normal(); if (auto A = C.SwitchText("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::Torus; S.A = Ctr; S.Axis = Axis; S.R2 = R1; S.R3 = R0;
        if (C.Switch("sheet")) return AddSurface(C, "Torus", NurbsSurface::Torus(Ctr, Axis, R0, R1));
        return AddBody(C, "Torus", BrepBody::Torus(Ctr, Axis, R0, R1), S);
    });
    Add("topology", "topology <body> — vertices, edges (with coedge senses), loops, faces, validation", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "topology")) return false;
        SceneFigure* I = Resolve(C.Arguments[0]); if (!I) return Refuse("no figure '%s'", C.Arguments[0].c_str());
        if (I->Classification != FigureClassification::Body) return Refuse("topology: '%s' is not a body", I->Name.c_str());
        const BrepBody& B = I->Body; BodyReport R = B.Validate();
        Row("%s %s  V%d E%d F%d L%d  hulls %d  χ=%d genus %d  closed %s manifold %s oriented %s  volume %.6f  area %.6f", I->Name.c_str(), Describe(B.Classification()),
            R.Vertices, R.Edges, R.Faces, R.Loops, R.Hulls, R.EulerCharacteristic, R.Genus, R.Closed ? "yes" : "no", R.Manifold ? "yes" : "no", R.Oriented ? "yes" : "no", R.Volume, R.Area);
        for (size_t V = 0; V < B.Vertices.size(); ++V) Row("  v%-3zu (%9.4f %9.4f %9.4f)", V, B.Vertices[V].Point.X, B.Vertices[V].Point.Y, B.Vertices[V].Point.Z);
        for (size_t E = 0; E < B.Edges.size(); ++E)
        {
            const BrepEdge& Ed = B.Edges[E];
            std::string Users; for (int Ce : Ed.Coedges) Users += " f" + std::to_string(B.Coedges[Ce].Face) + (B.Coedges[Ce].Reversed ? "-" : "+");
            Row("  e%-3zu v%d→v%d  %s deg %d  len %.4f  coedges[%s ]%s", E, Ed.VertexStart, Ed.VertexEnd, Describe(Ed.Curve.Classification), Ed.Curve.Degree, Ed.Curve.Length(), Users.c_str(),
                Ed.Coedges.size() == 1 ? "  OPEN" : Ed.Coedges.size() > 2 ? "  NON-MANIFOLD" : "");
        }
        for (size_t F = 0; F < B.Faces.size(); ++F)
        {
            const BrepFace& Fa = B.Faces[F];
            std::string Loops;
            for (int L : Fa.Loops) { Loops += B.Loops[L].Outer ? "  outer[" : "  hole["; for (int Ce : B.Loops[L].Coedges) Loops += " e" + std::to_string(B.Coedges[Ce].Edge) + (B.Coedges[Ce].Reversed ? "-" : "+"); Loops += " ]"; }
            Vec3 N = B.FaceNormal(int(F), 0.5 * (Fa.Surface.DomainStartU() + Fa.Surface.DomainEndU()), 0.5 * (Fa.Surface.DomainStartV() + Fa.Surface.DomainEndV()));
            Row("  f%-3zu %-10s %s%s  n(%.2f %.2f %.2f)%s", F, Describe(Fa.Surface.Classification), Fa.Reversed ? "reversed " : "", Fa.Natural ? "natural" : "trimmed", N.X, N.Y, N.Z, Loops.c_str());
        }
        return true;
    });
    Add("areas", "areas — list the closed sketch areas of the workplane (aN), their fill, holes and bounding curves", [=, this](const CommandLine&)
    {
        Scene.RebuildAreas(Plane);
        if (Scene.Areas().empty()) { Row("no closed areas on the workplane"); return true; }
        for (const SketchArea& A : Scene.Areas())
        {
            std::string Src; for (uint32_t Id : A.BoundingIdentities) if (SceneFigure* F = Scene.Find(Id)) Src += " " + F->Name;
            Row("  a%-3u %-8s depth %d  area %.6f  holes %zu  centroid (%.3f %.3f %.3f)%s  bounded by%s", A.Identity - SceneDocument::AreaIdentityBase, A.Filled ? "filled" : "empty", A.Cell.Depth, A.Cell.Area, A.Cell.Holes.size(), A.Centroid.X, A.Centroid.Y, A.Centroid.Z, A.Selected ? "  [selected]" : "", Src.c_str());
        }
        return true;
    });
    Add("fill", "fill on|off|toggle <aN...> | all | none | selected  ·  fill at (x,y[,z]) [on|off] — bucket-fill: choose which closed areas are material (solid on extrude) and which stay empty (sheet)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "fill")) return false;
        Scene.RebuildAreas(Plane);
        const std::string& A0 = C.Arguments[0];
        auto Apply = [&](SketchArea& A, int Want) { A.Filled = Want < 0 ? !A.Filled : Want > 0; Row("  a%u %s  (area %.4f, %zu hole(s))", A.Identity - SceneDocument::AreaIdentityBase, A.Filled ? "filled" : "emptied", A.Cell.Area, A.Cell.Holes.size()); };
        if (A0 == "at")
        {
            Vec3 P; if (!Need(C, 2, "fill at") || !PointArg(C, 1, P, "fill")) return false;
            int Want = -1; if (C.Count() >= 3) Want = C.Arguments[2] == "on" ? 1 : C.Arguments[2] == "off" ? 0 : -1;
            SketchArea* A = Scene.AreaAt(P); if (!A) return Refuse("fill at: no closed area under (%.3f %.3f %.3f)", P.X, P.Y, P.Z);
            Apply(*A, Want); return true;
        }
        if (A0 == "all" || A0 == "none") { for (SketchArea& A : Scene.Areas()) Apply(A, A0 == "all"); return true; }
        int Want = A0 == "on" ? 1 : A0 == "off" ? 0 : A0 == "toggle" ? -1 : -2;
        if (Want == -2) return Refuse("fill: on|off|toggle|all|none|at expected, got '%s'", A0.c_str());
        int Done = 0;
        if (C.Count() == 1 || C.Arguments[1] == "selected") { for (SketchArea& A : Scene.Areas()) if (A.Selected) { Apply(A, Want); ++Done; } if (!Done) return Refuse("fill: no areas selected"); return true; }
        for (size_t I = 1; I < C.Count(); ++I)
        {
            const std::string& T = C.Arguments[I];
            if (T == "all") { for (SketchArea& A : Scene.Areas()) { Apply(A, Want); ++Done; } continue; }
            const char* Digits = T.c_str(); if (*Digits == 'a' || *Digits == 'A') ++Digits;
            SketchArea* A = Scene.FindArea(SceneDocument::AreaIdentityBase + uint32_t(std::atoi(Digits)));
            if (!A) return Refuse("fill: no area '%s' (see `areas`)", T.c_str());
            Apply(*A, Want); ++Done;
        }
        return Done > 0;
    });
    Add("sew", "sew <surface...> — stitch sheet surfaces into one body, cap planar openings, orient", [=, this](const CommandLine& C)
    {
        std::vector<NurbsSurface> S; std::vector<uint32_t> Ids;
        for (SceneFigure* I : ResolveMany(C, 0)) { if (I->Classification == FigureClassification::Surface) { S.push_back(I->Surface); Ids.push_back(I->Identity); } else if (I->Classification == FigureClassification::Body) { for (const BrepFace& F : I->Body.Faces) S.push_back(F.Surface); Ids.push_back(I->Identity); } }
        if (S.empty()) return Refuse("sew: no surfaces");
        if (!AddBody(C, "Sewn", BrepBody::Sew(S))) return false;
        if (!C.Switch("keep")) for (uint32_t Id : Ids) Scene.Remove(Id);
        return true;
    });
    Add("solidify", "solidify <figure...> thickness — turn a sheet (single NURBS surface) into a solid slab of the given total thickness; the face is duplicated and translated along its normal by ± t/2, then the side wall is added as a ruled surface and the slab is re-sewn", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "solidify")) return false;
        double T = 0; if (!NumberArg(C, C.Count() - 1, T, "solidify")) return false;
        CommandLine Sub = C; Sub.Arguments.pop_back();
        if (T <= 0) return Refuse("solidify: thickness must be positive");
        int Done = 0;
        for (SceneFigure* I : ResolveMany(Sub, 0))
        {
            BrepBody Shell;
            if (I->Classification == FigureClassification::Surface) Shell = BrepBody::FromSurface(I->Surface);
            else { Refuse("solidify: '%s' must be a sheet (NURBS surface), not a body or curve", I->Name.c_str()); continue; }
            Deliver<BrepBody> R = BrepBody::Solidify(Shell, T * 0.5);
            if (!R) { Refuse("solidify %s: %s", I->Name.c_str(), R.Denial.Detail); continue; }
            std::string Name = I->Name; uint32_t Id = I->Identity; bool Sel = I->Selected;
            Scene.Remove(Id);
            SceneFigure& F = Scene.AddBody(C.SwitchText("name").value_or(Name + ".Solid"), std::move(R.Payload));
            F.Selected = Sel; ++Done;
            Row("solidify %s → %s  thickness %.4f", Name.c_str(), F.Name.c_str(), T);
        }
        return Done > 0;
    });
    Add("plane", "plane (origin) lengthU lengthV [--u=(x,y,z)] [--v=(x,y,z)] [--name=N]  ·  plane --from=<figure> --name=N — make a plane primitive (default), or save the current workplane as a named plane (with --name= alone), or save the plane implied by a figure's bounding face (with --from= and --name=).", [=, this](const CommandLine& C)
    {
        if (C.Switch("name") && !C.Switch("from") && C.Count() == 0)
        {
            // Just save the current workplane under a name.
            std::string N = *C.SwitchText("name");
            if (N.empty()) return Refuse("plane --name=: name is empty");
            NamedPlanes[N] = Plane;
            Row("plane %s saved  origin (%.3f %.3f %.3f) normal (%.3f %.3f %.3f)", N.c_str(), Plane.Origin.X, Plane.Origin.Y, Plane.Origin.Z, Plane.Normal().X, Plane.Normal().Y, Plane.Normal().Z);
            return true;
        }
        if (auto From = C.SwitchText("from"))
        {
            // Save a plane derived from a figure's natural face.
            SceneFigure* F = Resolve(*From);
            if (!F) return Refuse("plane --from=: no figure '%s'", From->c_str());
            std::string N = *C.SwitchText("name");
            if (N.empty()) return Refuse("plane --from=: --name= is required");
            Workplane P;
            if (F->Classification == FigureClassification::Surface) P = Workplane::FromNormal(F->Surface.Origin, F->Surface.Normal(0.5, 0.5));
            else if (F->Classification == FigureClassification::Body)
            {
                if (F->Body.Faces.empty()) return Refuse("plane --from=: body has no faces");
                const auto& Face = F->Body.Faces.front();
                Vec3 Nv = Face.Surface.Normal(0.5, 0.5);
                if (Face.Reversed) Nv = -Nv;
                P = Workplane::FromNormal(Face.Surface.Origin, Nv);
            }
            else return Refuse("plane --from=: figure '%s' must be a surface or body (not a curve)", From->c_str());
            NamedPlanes[N] = P;
            Row("plane %s saved from %s  origin (%.3f %.3f %.3f) normal (%.3f %.3f %.3f)", N.c_str(), From->c_str(), P.Origin.X, P.Origin.Y, P.Origin.Z, P.Normal().X, P.Normal().Y, P.Normal().Z);
            return true;
        }
        // Default: build a plane primitive (the original behaviour).
        Vec3 O; double LU = 0, LV = 0; if (!Need(C, 3, "plane") || !PointArg(C, 0, O, "plane") || !NumberArg(C, 1, LU, "plane") || !NumberArg(C, 2, LV, "plane")) return false;
        Vec3 U = Plane.AxisX, V = Plane.AxisY;
        if (auto A = C.SwitchText("u")) if (auto W = CommandCodec::ParsePoint(*A)) U = *W;
        if (auto A = C.SwitchText("v")) if (auto W = CommandCodec::ParsePoint(*A)) V = *W;
        return AddSurface(C, "Plane", NurbsSurface::Plane(O, U, V, LU, LV));
    });
    Add("patch", "patch countU countV (p00) (p01) ... row-major [--degree=3]   B-spline patch", [=, this](const CommandLine& C)
    {
        double CU = 0, CV = 0; if (!Need(C, 2, "patch") || !NumberArg(C, 0, CU, "patch") || !NumberArg(C, 1, CV, "patch")) return false;
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 2; I < C.Count(); ++I) { if (!PointArg(C, I, P, "patch")) return false; Pts.push_back(P); }
        int Deg = static_cast<int>(C.SwitchNumber("degree").value_or(3));
        return AddSurface(C, "Patch", NurbsSurface::Patch(std::min(Deg, int(CU) - 1), std::min(Deg, int(CV) - 1), int(CU), int(CV), Pts));
    });

    //---------------------------------------------- derived surfaces ----------------------------------------------
    // What an extrude / revolve operates on: a sketch area (by name "area N" / "aN", or the area a curve bounds when it is
    //    filled), else the bare curve. Filled area → solid with through-holes; unfilled area or open curve → sheet(s).
    struct SweepSource { std::vector<NurbsCurve> Loops; bool Solid = false; std::string Label; RecipeInput Input; };
    auto ResolveSweep = [this](const CommandLine& C, size_t Index, const char* Verb, SweepSource& Out) -> bool
    {
        const std::string& Tok = C.Arguments[Index];
        SketchArea* Area = nullptr;
        if (Tok.size() > 1 && (Tok[0] == 'a' || Tok[0] == 'A') && std::isdigit(static_cast<unsigned char>(Tok[1]))) Area = Scene.FindArea(SceneDocument::AreaIdentityBase + uint32_t(std::atoi(Tok.c_str() + 1)));
        if (!Area && Tok == "area" && Index + 1 < C.Count()) Area = Scene.FindArea(SceneDocument::AreaIdentityBase + uint32_t(std::atoi(C.Arguments[Index + 1].c_str())));
        if (Area)
        {
            Out.Input.Shape = RecipeInput::Form::Area; Out.Input.Figures = Area->BoundingIdentities; Out.Input.Centroid = Area->Centroid;
            Out.Loops = Area->Loops(); Out.Solid = Area->Filled && !C.Switch("sheet");
            Out.Label = "area " + std::to_string(Area->Identity - SceneDocument::AreaIdentityBase) + (Area->Filled ? " (filled)" : " (unfilled)");
            return true;
        }
        // body edge: Name:eN
        if (size_t Colon = Tok.find(":e"); Colon != std::string::npos)
        {
            SceneFigure* Owner = Resolve(Tok.substr(0, Colon)); int E = std::atoi(Tok.c_str() + Colon + 2);
            if (!Owner || Owner->Classification != FigureClassification::Body || E < 0 || E >= int(Owner->Body.Edges.size())) return Refuse("%s: '%s' is not an edge of a body (Name:eN)", Verb, Tok.c_str());
            Out.Input.Shape = RecipeInput::Form::Edge; Out.Input.Figures = { Owner->Identity }; Out.Input.Edge = E;
            Out.Loops = { Owner->Body.Edges[E].Curve }; Out.Solid = false; Out.Label = Tok;
            return true;
        }
        SceneFigure* Figure = Resolve(Tok);
        if (!Figure || Figure->Classification != FigureClassification::Curve) return Refuse("%s: '%s' is neither a curve, an area (a0, a1 … see `areas`) nor an edge (Name:eN)", Verb, Tok.c_str());
        Out.Input.Shape = RecipeInput::Form::Curve; Out.Input.Figures = { Figure->Identity };
        // a closed curve that bounds exactly one filled area whose outer loop is this curve → that area (carries its holes)
        if (Figure->Curve.Closed() && !C.Switch("sheet"))
        {
            for (SketchArea* A : Scene.AreasOf(Figure->Identity))
            {
                double D = 0; (void)A->Cell.Outer.ClosestParameter(Figure->Curve.Sample(0.37 * (Figure->Curve.DomainStart() + Figure->Curve.DomainEnd()) + 0.63 * Figure->Curve.DomainStart()), &D);
                bool SameOuter = D < ScalarCriteria::MergeTolerance && std::fabs(std::fabs(ProfileSolver::SignedArea(Figure->Curve, A->Normal)) - (A->Cell.Area + [&] { double S = 0; for (const NurbsCurve& Hh : A->Cell.Holes) S += std::fabs(ProfileSolver::SignedArea(Hh, A->Normal)); return S; }())) < 1e-6;
                if (!SameOuter) continue;
                Out.Input.Shape = RecipeInput::Form::Area; Out.Input.Figures = A->BoundingIdentities; Out.Input.Centroid = A->Centroid;
                Out.Loops = A->Loops(); Out.Solid = A->Filled;
                Out.Label = Figure->Name + " → area " + std::to_string(A->Identity - SceneDocument::AreaIdentityBase) + (A->Filled ? " (filled, " + std::to_string(A->Cell.Holes.size()) + " hole(s))" : " (unfilled → sheet)");
                return true;
            }
        }
        // closed curve off the workplane (or bounding no area): material by default; unfilled areas were handled above
        Out.Loops = { Figure->Curve }; Out.Solid = Figure->Curve.Closed() && !C.Switch("sheet");
        Out.Label = Figure->Name + (Figure->Curve.Closed() ? (Out.Solid ? " (closed → solid)" : " (closed → sheet)") : " (open → sheet)");
        return true;
    };
    Add("extrude", "extrude <curve | aN> length [--direction=(x,y,z)] [--sheet] — filled area → solid with through-holes; unfilled / open → sheet", [=, this](const CommandLine& C)
    {
        double L = 0; if (!Need(C, 2, "extrude") || !NumberArg(C, C.Count() - 1, L, "extrude")) return false;
        SweepSource S; if (!ResolveSweep(C, 0, "extrude", S)) return false;
        Vec3 Dir = Plane.Normal(); if (auto A = C.SwitchText("direction")) if (auto V = CommandCodec::ParsePoint(*A)) Dir = *V;
        Row("extrude %s", S.Label.c_str());
        FigureRecipe R; R.Operation = RecipeOperation::Extrude; R.Sections = { S.Input }; R.Direction = Dir; R.Length = L; R.Sheet = !S.Solid;
        // Phase 16: explicit Blueprint for extrude. (The existing live-edit path already works via the
        //    Recipe, but a Blueprint makes the dim tree emit a clean "length" / "direction" dim set.)
        SceneFigure::ParametricBlueprint BP;
        BP.Form = SceneFigure::ParametricForm::Extrude;
        BP.Axis = Dir; BP.R2 = L;                                                            // Phase 13 convention: extrude length is R2
        return AddDerived(C, "Extrusion", R, BP);
    });
    Add("revolve", "revolve <curve> angleDeg [--origin=(x,y,z)] [--axis=(x,y,z)] [--sheet]", [=, this](const CommandLine& C)
    {
        double Angle = 0; if (!Need(C, 2, "revolve") || !NumberArg(C, C.Count() - 1, Angle, "revolve")) return false;
        SweepSource S; if (!ResolveSweep(C, 0, "revolve", S)) return false;
        Vec3 O = Plane.Origin, Axis = Plane.AxisY;
        if (auto A = C.SwitchText("origin")) if (auto V = CommandCodec::ParsePoint(*A)) O = *V;
        if (auto A = C.SwitchText("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        Row("revolve %s", S.Label.c_str());
        FigureRecipe R; R.Operation = RecipeOperation::Revolve; R.Sections = { S.Input }; R.AxisOrigin = O; R.Axis = Axis; R.Angle = ScalarCriteria::Radians(Angle); R.Sheet = !S.Solid;
        // Phase 16: live-edit Blueprint. Slots 0..2 = AxisOrigin.X/Y/Z, 3..5 = Axis.X/Y/Z, 12 = Angle (radians).
        SceneFigure::ParametricBlueprint BP;
        BP.Form = SceneFigure::ParametricForm::Revolve;
        BP.A = O; BP.B = Axis; BP.R0 = ScalarCriteria::Radians(Angle);
        return AddDerived(C, "Revolution", R, BP);
    });
    //------------------------------------------------ Phase 7: planar profile algebra -------------------------------------------------
    auto ProfileOf = [this](const std::vector<SceneFigure*>& Figures, const char* Verb, Profile& Out) -> bool
    {
        std::vector<NurbsCurve> Loops;
        for (SceneFigure* F : Figures)
        {
            if (F->Classification != FigureClassification::Curve) return Refuse("%s: '%s' is not a curve", Verb, F->Name.c_str());
            if (!F->Curve.Closed()) return Refuse("%s: '%s' is not closed", Verb, F->Name.c_str());
            Loops.push_back(F->Curve);
        }
        if (Loops.empty()) return Refuse("%s: no closed curves", Verb);
        Deliver<Profile> P = ProfileSolver::Assemble(Loops, Plane.Normal());
        if (!P) return Refuse("%s: %s", Verb, P.Denial.Detail);
        Out = std::move(P.Payload);
        return true;
    };
    auto EmitProfile = [this](const CommandLine& C, const char* Stem, const Profile& P, ProfileOperation Op)
    {
        Row("%s → %zu loop(s), area %.6f", Describe(Op), P.Loops.size(), P.Area());
        std::string Stem2 = C.SwitchText("name").value_or(Stem);
        for (size_t I = 0; I < P.Loops.size(); ++I)
        {
            const ProfileLoop& L = P.Loops[I];
            SceneFigure& F = Scene.AddCurve(Stem2, L.Curve);
            Row("  · #%-3u %-18s %s  depth %d  area %+.6f  %s", F.Identity, F.Name.c_str(), L.SignedArea > 0 ? "ccw" : "cw ", L.Depth, L.SignedArea, L.Depth % 2 ? "hole" : "outer");
            F.Selected = true;
        }
    };
    Add("boolean", "boolean union|subtract|intersect <A...> -- <B...> [--keep] [--name=] [--verbose]   ·   bodies: true NURBS surface–surface-intersection boolean; sketch curves: 2D profile boolean (A may hold holes)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "boolean")) return false;
        ProfileOperation Op;
        const std::string& O = C.Arguments[0];
        if (O == "union" || O == "add" || O == "u") Op = ProfileOperation::Union;
        else if (O == "subtract" || O == "cut" || O == "difference" || O == "s") Op = ProfileOperation::Subtract;
        else if (O == "intersect" || O == "common" || O == "i") Op = ProfileOperation::Intersect;
        else return Refuse("boolean: unknown operation '%s' (union | subtract | intersect)", O.c_str());
        std::vector<SceneFigure*> A, B; bool Right = false;
        if (C.Count() == 2 && C.Arguments[1] == "selected") { for (SceneFigure& F : Scene.Figures()) if (F.Selected) A.push_back(&F); }
        else for (size_t I = 1; I < C.Count(); ++I)
        {
            if (C.Arguments[I] == "--") { Right = true; continue; }
            SceneFigure* F = Resolve(C.Arguments[I]); if (!F) return Refuse("no figure '%s'", C.Arguments[I].c_str());
            (Right ? B : A).push_back(F);
        }
        if (!Right && A.size() >= 2) { B.push_back(A.back()); A.pop_back(); }                 // "boolean union A B" shorthand
        if (A.empty() || B.empty()) return Refuse("boolean: need figures on both sides (use -- to separate A from B)");
        bool Bodies = A.front()->Classification == FigureClassification::Body;
        if (Bodies)
        {
            //------------------------------------------------ 3D: SSI boolean of two solids -------------------------------------------------
            if (A.size() != 1 || B.size() != 1 || B.front()->Classification != FigureClassification::Body) return Refuse("boolean: body booleans take exactly one body on each side");
            BodyOperation Op3 = Op == ProfileOperation::Union ? BodyOperation::Union : Op == ProfileOperation::Subtract ? BodyOperation::Subtract : BodyOperation::Intersect;
            std::string NameA = A.front()->Name, NameB = B.front()->Name;
            uint32_t IdA = A.front()->Identity, IdB = B.front()->Identity;
            IntersectionSolver::Verbose = C.Switch("verbose");
            auto T0 = std::chrono::steady_clock::now();
            BooleanReport Rep;
            Deliver<BrepBody> R = IntersectionSolver::Combine(A.front()->Body, B.front()->Body, Op3, &Rep);
            IntersectionSolver::Verbose = false;
            double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
            if (!R) return Refuse("boolean %s: %s — %s", Describe(Op3), Refusal::Describe(R.Denial.Reason), R.Denial.Detail);
            Row("boolean %s %s %s  ·  %d intersection curve(s), pieces %d + %d, kept %d + %d (inside %d / %d)  ·  %.3f s", Describe(Op3), NameA.c_str(), NameB.c_str(), Rep.Curves, Rep.PiecesA, Rep.PiecesB, Rep.KeptA, Rep.KeptB, Rep.InsideA, Rep.InsideB, Seconds);
            Scene.ClearSelection();
            if (!C.Switch("keep")) { Scene.Remove(IdA); Scene.Remove(IdB); }              // before adding: pointers die with the erase
            const char* Stem = Op3 == BodyOperation::Union ? "Union" : Op3 == BodyOperation::Subtract ? "Difference" : "Common";
            // Phase 16: stamp a Boolean Blueprint so the dim tree shows a header dim. No live slot
            //    (the inputs were consumed; re-running boolean needs the original bodies).
            SceneFigure::ParametricBlueprint BP;
            BP.Form = SceneFigure::ParametricForm::Boolean;
            BP.R0 = Op3 == BodyOperation::Union ? 0.0 : Op3 == BodyOperation::Subtract ? 1.0 : 2.0;     // op encoding
            BP.Axis = Vec3(double(IdA), double(IdB), 0.0);                                                // record the source ids
            return AddBody(C, Stem, std::move(R), BP);
        }
        //------------------------------------------------ 2D: planar profile boolean -------------------------------------------------
        Profile Pa, Pb; if (!ProfileOf(A, "boolean", Pa) || !ProfileOf(B, "boolean", Pb)) return false;
        std::vector<uint32_t> Consumed; for (SceneFigure* F : A) Consumed.push_back(F->Identity); for (SceneFigure* F : B) Consumed.push_back(F->Identity);
        Deliver<Profile> R = ProfileSolver::Combine(Pa, Pb, Op);
        if (!R) return Refuse("boolean: %s", R.Denial.Detail);
        Scene.ClearSelection();
        if (!C.Switch("keep")) for (uint32_t Id : Consumed) Scene.Remove(Id);              // before adding: pointers die with the erase
        EmitProfile(C, Op == ProfileOperation::Union ? "Union" : Op == ProfileOperation::Subtract ? "Difference" : "Common", R.Payload, Op);
        return true;
    });
    Add("profile", "profile <closed curves...> — signed areas, enclosure depth, winding orientation and combined area", [=, this](const CommandLine& C)
    {
        Profile P; if (!ProfileOf(ResolveMany(C, 0), "profile", P)) return false;
        Row("profile of %zu loop(s): area %.6f  normal (%.2f %.2f %.2f)", P.Loops.size(), P.Area(), P.Normal.X, P.Normal.Y, P.Normal.Z);
        for (const ProfileLoop& L : P.Loops) Row("  %s  depth %d  area %+.6f  %s  length %.4f", L.SignedArea > 0 ? "ccw" : "cw ", L.Depth, L.SignedArea, (L.SignedArea > 0) == (L.Depth % 2 == 0) ? "winding agrees with depth" : "WINDING INVERTED for its depth", L.Curve.Length());
        return true;
    });
    Add("intersections", "intersections <curve> <curve> | <body> <body> [--curves] — exact curve–curve crossings, or SSI curves between two solids (--curves adds them to the scene)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "intersections")) return false;
        SceneFigure* A = Resolve(C.Arguments[0]); SceneFigure* B = Resolve(C.Arguments[1]);
        if (!A || !B) return Refuse("intersections: unknown figure");
        if (A->Classification == FigureClassification::Body && B->Classification == FigureClassification::Body)
        {
            // surface–surface intersection curves become sketch curves in the scene (Plasticity: intersect → curves)
            std::vector<IntersectionCurve> X = IntersectionSolver::Intersect(A->Body, B->Body);
            Row("%zu intersection curve piece(s) between %s and %s", X.size(), A->Name.c_str(), B->Name.c_str());
            std::string NameA = A->Name, NameB = B->Name; int Index = 0;
            for (const IntersectionCurve& K : X)
            {
                Row("  %s:f%d ∩ %s:f%d  %zu points  %s  deviation %.1e  length %.4f", NameA.c_str(), K.FaceA, NameB.c_str(), K.FaceB, K.Points.size(), K.Closed ? "closed" : "open", K.Deviation, K.Curve.Length());
                if (C.Switch("curves") && K.Curve.PoleCount() >= 2) { SceneFigure& F = Scene.AddCurve(std::string("Intersection.") + std::to_string(++Index), K.Curve); F.Selected = true; }
            }
            return true;
        }
        if (A->Classification != FigureClassification::Curve || B->Classification != FigureClassification::Curve) return Refuse("intersections: both must be curves or both bodies");
        std::vector<CurveCrossing> X = A == B ? ProfileSolver::SelfIntersections(A->Curve) : ProfileSolver::Intersect(A->Curve, B->Curve);
        Row("%zu crossing(s) between %s and %s", X.size(), A->Name.c_str(), B->Name.c_str());
        for (const CurveCrossing& K : X) Row("  (%.6f %.6f %.6f)  tA %.6f  tB %.6f%s", K.Point.X, K.Point.Y, K.Point.Z, K.ParameterA, K.ParameterB, K.Tangent ? "  tangent" : "");
        return true;
    });
    Add("fillet", "fillet <curve...> radius [--corners=i,j,…] — round the corners of a polyline / polygon / rectangle (Plasticity B)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "fillet")) return false;
        double R = 0; if (!NumberArg(C, C.Count() - 1, R, "fillet")) return false;
        CommandLine Sub = C; Sub.Arguments.pop_back();
        std::vector<int> Corners; bool Some = false;
        if (auto T = C.SwitchText("corners")) { Some = true; size_t P = 0; while (P < T->size()) { size_t Q = T->find(',', P); Corners.push_back(std::atoi(T->substr(P, Q == std::string::npos ? std::string::npos : Q - P).c_str())); if (Q == std::string::npos) break; P = Q + 1; } }
        int Done = 0;
        for (SceneFigure* F : ResolveMany(Sub, 0))
        {
            if (F->Classification != FigureClassification::Curve) continue;
            Deliver<NurbsCurve> N = ProfileSolver::Filleted(F->Curve, R, Some ? &Corners : nullptr);
            if (!N) { Refuse("fillet %s: %s", F->Name.c_str(), N.Denial.Detail); continue; }
            F->Curve = std::move(N.Payload); DescribeFigure(*F); ++Done;
        }
        return Done > 0;
    });
    Add("chamfer", "chamfer <figure...> setback [--corners=i,j,…]  or  --edges=i --name=…  — bevel the corners of a polyline / polygon / rectangle, or planar-setback chamfer a body edge (rolling-ball fillet is Phase 11b). The two switches are mutually exclusive: --corners is the curve mode, --edges is the body mode.", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "chamfer")) return false;
        double D = 0; if (!NumberArg(C, C.Count() - 1, D, "chamfer")) return false;
        CommandLine Sub = C; Sub.Arguments.pop_back();
        if (D <= 0) return Refuse("chamfer: setback must be positive");
        bool BodyMode = C.Switch("edges") || C.Switch("name");                          // --edges or --name imply body chamfer
        int Done = 0;
        if (BodyMode)
        {
            // Parse --edges=i (single edge for the body verb).
            std::vector<int> EdgeList; bool Some = false;
            if (auto T = C.SwitchText("edges")) { Some = true; size_t P = 0; while (P < T->size()) { size_t Q = T->find(',', P); EdgeList.push_back(std::atoi(T->substr(P, Q == std::string::npos ? std::string::npos : Q - P).c_str())); if (Q == std::string::npos) break; P = Q + 1; } }
            for (SceneFigure* I : ResolveMany(Sub, 0))
            {
                if (I->Classification != FigureClassification::Body) { Refuse("chamfer: '%s' is not a body", I->Name.c_str()); continue; }
                BrepBody Working = I->Body;
                std::vector<int> Targets;
                if (Some) { if (EdgeList.empty()) { Refuse("chamfer: --edges= is empty"); continue; } Targets = { EdgeList.front() }; }
                else { for (size_t E = 0; E < Working.Edges.size(); ++E) if (Working.Edges[E].Coedges.size() == 2) Targets.push_back(int(E)); }
                int EdgesChamfered = 0;
                std::string FailureDetail;
                for (int E : Targets)
                {
                    Deliver<BrepBody> R = Working.ChamferEdge(E, D);
                    if (!R) { FailureDetail = R.Denial.Detail; break; }
                    Working = std::move(R.Payload);
                    ++EdgesChamfered;
                }
                if (EdgesChamfered == 0) { Refuse("chamfer %s: %s", I->Name.c_str(), FailureDetail.c_str()); continue; }
                std::string Name = I->Name; uint32_t Id = I->Identity; bool Sel = I->Selected;
                BrepBody PreOp = I->Body;                                              // capture the pre-chamfer body BEFORE the remove
                Scene.Remove(Id);
                SceneFigure& F = Scene.AddBody(C.SwitchText("name").value_or(Name + ".Chamfered"), std::move(Working));
                F.Selected = Sel;
                // Record the parametric source so a live dim can re-derive the chamfer with a new distance.
                //    PreOpBody = the body before the chamfer, so a live edit re-applies the operation
                //    to a clean copy (otherwise the edge count grows and the second chamfer fails).
                if (!Targets.empty())
                {
                    SceneFigure::ParametricBlueprint S; S.Form = SceneFigure::ParametricForm::ChamferEdge;
                    S.I0 = Targets.front();
                    S.R0 = D;
                    S.PreOpBody = std::move(PreOp);
                    if (S.PreOpBody.Edges.size() > size_t(Targets.front()))
                    {
                        const BrepEdge& E = S.PreOpBody.Edges[Targets.front()];
                        Vec3 Lo = E.Curve.Sample(E.Curve.DomainStart());
                        Vec3 Hi = E.Curve.Sample(E.Curve.DomainEnd());
                        S.A = (Lo + Hi) * 0.5;
                        S.Axis = (Hi - Lo).Normalised();
                    }
                    F.Blueprint = std::move(S);
                }
                if (!C.Switch("no-dim") && ShowDimensions) AutoEmitDimensions(F);
                ++Done;
                Row("chamfer %s → %s  setback %.4f  edges %d/%d", Name.c_str(), F.Name.c_str(), D, EdgesChamfered, int(Targets.size()));
            }
        }
        else
        {
            std::vector<int> Corners; bool Some = false;
            if (auto T = C.SwitchText("corners")) { Some = true; size_t P = 0; while (P < T->size()) { size_t Q = T->find(',', P); Corners.push_back(std::atoi(T->substr(P, Q == std::string::npos ? std::string::npos : Q - P).c_str())); if (Q == std::string::npos) break; P = Q + 1; } }
            for (SceneFigure* F : ResolveMany(Sub, 0))
            {
                if (F->Classification != FigureClassification::Curve) { Refuse("chamfer: '%s' is not a curve (use --edges= to chamfer a body)", F->Name.c_str()); continue; }
                Deliver<NurbsCurve> N = ProfileSolver::Chamfered(F->Curve, D, Some ? &Corners : nullptr);
                if (!N) { Refuse("chamfer %s: %s", F->Name.c_str(), N.Denial.Detail); continue; }
                F->Curve = std::move(N.Payload); DescribeFigure(*F); ++Done;
            }
        }
        return Done > 0;
    });
    Add("offset", "offset <curve...> distance [--copy] — parallel curve; + is left of travel (inward for ccw loops), lines and arcs stay exact (Plasticity O)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "offset")) return false;
        double D = 0; if (!NumberArg(C, C.Count() - 1, D, "offset")) return false;
        CommandLine Sub = C; Sub.Arguments.pop_back();
        int Done = 0;
        std::vector<SceneFigure*> Targets = ResolveMany(Sub, 0);
        std::vector<uint32_t> Ids; for (SceneFigure* F : Targets) Ids.push_back(F->Identity);
        for (uint32_t Id : Ids)
        {
            SceneFigure* F = Scene.Find(Id); if (!F || F->Classification != FigureClassification::Curve) continue;
            Deliver<NurbsCurve> N = ProfileSolver::Offset(F->Curve, D, Plane.Normal());
            if (!N) { Refuse("offset %s: %s", F->Name.c_str(), N.Denial.Detail); continue; }
            if (C.Switch("copy")) { SceneFigure& G = Scene.AddCurve(F->Name + ".offset", std::move(N.Payload)); DescribeFigure(G); }
            else { F->Curve = std::move(N.Payload); DescribeFigure(*F); }
            ++Done;
        }
        return Done > 0;
    });
    Add("trim", "trim <curve> (near point) [--by=<cutter,...>] — remove the piece of the curve nearest the point between crossings with the cutters (default: every other curve) (Plasticity T)", [=, this](const CommandLine& C)
    {
        Vec3 Near; if (!Need(C, 2, "trim") || !PointArg(C, 1, Near, "trim")) return false;
        SceneFigure* F = Resolve(C.Arguments[0]); if (!F || F->Classification != FigureClassification::Curve) return Refuse("trim: '%s' is not a curve", C.Arguments[0].c_str());
        std::vector<NurbsCurve> Cutters;
        if (auto T = C.SwitchText("by"))
        {
            size_t P = 0; while (P <= T->size()) { size_t Q = T->find(',', P); std::string Tok = T->substr(P, Q == std::string::npos ? std::string::npos : Q - P); if (SceneFigure* K = Resolve(Tok)) if (K->Classification == FigureClassification::Curve) Cutters.push_back(K->Curve); if (Q == std::string::npos) break; P = Q + 1; }
        }
        else for (SceneFigure& K : Scene.Figures()) if (&K != F && K.Classification == FigureClassification::Curve && !K.Hidden) Cutters.push_back(K.Curve);
        Deliver<std::vector<NurbsCurve>> R = ProfileSolver::Trimmed(F->Curve, Cutters, Near);
        if (!R) return Refuse("trim: %s", R.Denial.Detail);
        std::string Name = F->Name; uint32_t Id = F->Identity; bool Sel = F->Selected;
        Scene.Remove(Id);
        Row("trim %s → %zu piece(s)", Name.c_str(), R.Payload.size());
        for (NurbsCurve& P : R.Payload) { SceneFigure& G = Scene.AddCurve(Name, std::move(P)); G.Selected = Sel; DescribeFigure(G); }
        return true;
    });
    Add("join", "join <curve...> | selected — chain curves that meet end to end into one (Plasticity J)", [=, this](const CommandLine& C)
    {
        std::vector<SceneFigure*> Fs = ResolveMany(C, 0);
        std::vector<NurbsCurve> Pieces; std::vector<uint32_t> Ids; std::string Name;
        for (SceneFigure* F : Fs) if (F->Classification == FigureClassification::Curve) { Pieces.push_back(F->Curve); Ids.push_back(F->Identity); if (Name.empty()) Name = F->Name; }
        if (Pieces.size() < 2) return Refuse("join: need at least two curves");
        Deliver<NurbsCurve> J = ProfileSolver::Joined(std::move(Pieces));
        if (!J) return Refuse("join: %s", J.Denial.Detail);
        for (uint32_t Id : Ids) Scene.Remove(Id);
        SceneFigure& G = Scene.AddCurve(C.SwitchText("name").value_or("Joined"), std::move(J.Payload)); G.Selected = true;
        DescribeFigure(G);
        return true;
    });
    Add("explode", "explode <curve...> | selected — split a curve at its tangent kinks into separate curves (Plasticity Alt+J)", [=, this](const CommandLine& C)
    {
        std::vector<SceneFigure*> Fs = ResolveMany(C, 0);
        std::vector<uint32_t> Ids; for (SceneFigure* F : Fs) if (F->Classification == FigureClassification::Curve) Ids.push_back(F->Identity);
        if (Ids.empty()) return Refuse("explode: no curves");
        for (uint32_t Id : Ids)
        {
            SceneFigure* F = Scene.Find(Id); std::string Name = F->Name; bool Sel = F->Selected;
            std::vector<NurbsCurve> Pieces = SplitAtKinks(F->Curve);
            if (Pieces.size() < 2) { Row("%s has no kinks — left alone", Name.c_str()); continue; }
            Scene.Remove(Id);
            Row("explode %s → %zu piece(s)", Name.c_str(), Pieces.size());
            for (NurbsCurve& P : Pieces) { SceneFigure& G = Scene.AddCurve(Name, std::move(P)); G.Selected = Sel; }
        }
        return true;
    });
    // Sections for loft / sweep / patch: every positional argument is a curve, area (aN), or edge (Body:eN); `selected`
    //    takes the selection in order (areas first, then curves, then selected body edges).
    auto CollectSections = [this, ResolveSweep](const CommandLine& C, size_t First, const char* Verb, std::vector<SweepSource>& Out) -> bool
    {
        if (C.Count() <= First || (C.Count() == First + 1 && C.Arguments[First] == "selected"))
        {
            for (SketchArea& A : Scene.Areas()) if (A.Selected) { SweepSource S; S.Input.Shape = RecipeInput::Form::Area; S.Input.Figures = A.BoundingIdentities; S.Input.Centroid = A.Centroid; S.Loops = A.Loops(); S.Solid = A.Filled; S.Label = "a" + std::to_string(A.Identity - SceneDocument::AreaIdentityBase); Out.push_back(S); }
            for (SceneFigure& F : Scene.Figures())
            {
                if (!F.Selected && F.SelectedEdges.empty()) continue;
                if (F.Classification == FigureClassification::Curve && F.Selected) { SweepSource S; S.Input.Shape = RecipeInput::Form::Curve; S.Input.Figures = { F.Identity }; S.Loops = { F.Curve }; S.Solid = F.Curve.Closed(); S.Label = F.Name; Out.push_back(S); }
                if (F.Classification == FigureClassification::Body) for (int E : F.SelectedEdges) { SweepSource S; S.Input.Shape = RecipeInput::Form::Edge; S.Input.Figures = { F.Identity }; S.Input.Edge = E; S.Loops = { F.Body.Edges[E].Curve }; S.Label = F.Name + ":e" + std::to_string(E); Out.push_back(S); }
            }
            if (Out.empty()) return Refuse("%s: nothing selected", Verb);
            return true;
        }
        for (size_t I = First; I < C.Count(); ++I)
        {
            // "Outer+Hole+Hole2" → one station made of several closed loops (outer first)
            const std::string& Tok = C.Arguments[I];
            if (Tok.find('+') != std::string::npos)
            {
                SweepSource Station; Station.Input.Shape = RecipeInput::Form::Curve; Station.Solid = true;
                size_t Start = 0;
                while (Start <= Tok.size())
                {
                    size_t Plus = Tok.find('+', Start); std::string Name = Tok.substr(Start, Plus == std::string::npos ? std::string::npos : Plus - Start);
                    SceneFigure* F = Resolve(Name);
                    if (!F || F->Classification != FigureClassification::Curve || !F->Curve.Closed()) return Refuse("%s: '%s' in '%s' must be a closed curve", Verb, Name.c_str(), Tok.c_str());
                    Station.Input.Figures.push_back(F->Identity); Station.Loops.push_back(F->Curve);
                    Station.Label += (Station.Label.empty() ? "" : "+") + F->Name;
                    if (Plus == std::string::npos) break;
                    Start = Plus + 1;
                }
                Out.push_back(Station); continue;
            }
            SweepSource S; if (!ResolveSweep(C, I, Verb, S)) return false; Out.push_back(S);
        }
        return true;
    };
    Add("loft", "loft <sections...>|selected [--degree=3] [--loop] [--sheet] [--no-align] [--guides=a,b] [--guide-weight=w] [--guide-rounds=n] — sections are curves, areas (aN), body edges (Body:eN) or Outer+Hole groups in flow order; closed sections → solid (areas with holes → through-holes); --guides bends the sheet through each named curve", [=, this](const CommandLine& C)
    {
        std::vector<SweepSource> Sections; if (!CollectSections(C, 0, "loft", Sections)) return false;
        if (Sections.size() < 2) return Refuse("loft: at least two sections");
        FigureRecipe R; R.Operation = RecipeOperation::Loft;
        for (const SweepSource& S : Sections) R.Sections.push_back(S.Input);
        R.Loft.DegreeV = int(C.SwitchNumber("degree").value_or(3)); R.Loft.Loop = C.Switch("loop"); R.Loft.AlignSeams = R.Loft.AlignSense = !C.Switch("no-align");
        R.Sheet = C.Switch("sheet");
        if (auto G = C.SwitchText("guides"))
        {
            std::string Tok; for (char Ch : *G + ",") { if (Ch == ',') { if (!Tok.empty()) { SceneFigure* F = Resolve(Tok); if (!F || F->Classification != FigureClassification::Curve) return Refuse("loft: guide '%s' is not a curve", Tok.c_str()); RecipeInput In; In.Shape = RecipeInput::Form::Curve; In.Figures = { F->Identity }; R.LoftGuideInputs.push_back(In); } Tok.clear(); } else Tok += Ch; }
        }
        R.LoftGuides.Weight = C.SwitchNumber("guide-weight").value_or(R.LoftGuides.Weight);
        R.LoftGuides.Rounds = int(C.SwitchNumber("guide-rounds").value_or(R.LoftGuides.Rounds));
        R.LoftGuides.SamplesPerGuide = int(C.SwitchNumber("guide-samples").value_or(R.LoftGuides.SamplesPerGuide));
        std::string Names; for (const SweepSource& S : Sections) Names += " " + S.Label;
        Row("loft%s%s", Names.c_str(), R.LoftGuideInputs.empty() ? "" : " (guides)");
        // Phase 16: live-edit Blueprint. Slot 16 = Loft.DegreeV (I0).
        SceneFigure::ParametricBlueprint BP;
        BP.Form = SceneFigure::ParametricForm::Loft;
        BP.I0 = R.Loft.DegreeV;
        return AddDerived(C, "Loft", R, BP);
    });
    Add("sweep", "sweep <profile> <path> [--bases=minimal|frenet|fixed] [--scale=s] [--twist=deg] [--stations=n] [--sheet] — carry a profile (curve / area / edge) along a path curve or edge", [=, this](const CommandLine& C)
    {
        SweepSource P, Path;
        if (C.Count() == 1 && C.Arguments[0] == "selected")
        {
            std::vector<SweepSource> Sel; if (!CollectSections(C, 0, "sweep", Sel)) return false;
            if (Sel.size() != 2) return Refuse("sweep selected: select exactly the profile and then the path (%zu selected)", Sel.size());
            P = Sel[0]; Path = Sel[1];
        }
        else { if (!Need(C, 2, "sweep")) return false; if (!ResolveSweep(C, 0, "sweep", P) || !ResolveSweep(C, 1, "sweep", Path)) return false; }
        FigureRecipe R; R.Operation = RecipeOperation::Sweep; R.Sections = { P.Input }; R.Path = Path.Input; R.Sheet = C.Switch("sheet") || !P.Solid;
        if (auto F = C.SwitchText("bases")) R.Sweep.Bases = *F == "frenet" ? SweepBases::Frenet : *F == "fixed" ? SweepBases::Fixed : SweepBases::RotationMinimising;
        R.Sweep.ScaleEnd = C.SwitchNumber("scale").value_or(1.0); R.Sweep.TwistAngle = ScalarCriteria::Radians(C.SwitchNumber("twist").value_or(0.0)); R.Sweep.Stations = int(C.SwitchNumber("stations").value_or(0));
        Row("sweep %s along %s", P.Label.c_str(), Path.Label.c_str());
        // Phase 16: live-edit Blueprint. Slot 12 = ScaleEnd, 13 = TwistAngle, 16 = Stations.
        SceneFigure::ParametricBlueprint BP;
        BP.Form = SceneFigure::ParametricForm::Sweep;
        BP.R0 = R.Sweep.ScaleEnd;
        BP.R1 = R.Sweep.TwistAngle;
        BP.I0 = R.Sweep.Stations;
        return AddDerived(C, "Sweep", R, BP);
    });
    Add("pipe", "pipe <path> radius [--sheet] — circular tube along a curve or edge", [=, this](const CommandLine& C)
    {
        double Radius = 0; if (!Need(C, 2, "pipe") || !NumberArg(C, 1, Radius, "pipe")) return false;
        SweepSource Path;
        if (C.Arguments[0] == "selected") { std::vector<SweepSource> Sel; if (!CollectSections(CommandLine{ C.Verb, { "selected" }, C.Flags }, 0, "pipe", Sel)) return false; if (Sel.size() != 1) return Refuse("pipe selected: select exactly one path"); Path = Sel[0]; }
        else if (!ResolveSweep(C, 0, "pipe", Path)) return false;
        FigureRecipe R; R.Operation = RecipeOperation::Pipe; R.Path = Path.Input; R.Radius = Radius; R.Sheet = C.Switch("sheet");
        // Phase 16: live-edit Blueprint. Slot 12 = Radius.
        SceneFigure::ParametricBlueprint BP;
        BP.Form = SceneFigure::ParametricForm::Pipe;
        BP.R0 = Radius;
        return AddDerived(C, "Pipe", R, BP);
    });
    Add("fillpatch", "fillpatch <boundaries...>|selected — Coons sheet over 3–4 boundary curves / edges, N-sided fill over more, or one closed curve", [=, this](const CommandLine& C)
    {
        std::vector<SweepSource> B; if (!CollectSections(C, 0, "fillpatch", B)) return false;
        FigureRecipe R; R.Operation = RecipeOperation::Patch; for (const SweepSource& S : B) R.Sections.push_back(S.Input);
        return AddDerived(C, "Patch", R);
    });
    Add("fairpatch", "fairpatch <rim[@g0|@g1|@g2][@tN][@fN]...>|selected [--g1|--g2] [--tension=t] [--on=surface] [--guides=a,b] [--star] [--spans=n] [--fairness=f] — energy-fair fill; G1/G2 rims follow the adjacent body face (or --on)", [=, this](const CommandLine& C)
    {
        // rim tokens may carry per-rim suffixes: Body:e3@g2@t0.5 ; the switches give the defaults
        RimContinuity Default = C.Switch("g2") ? RimContinuity::Curvature : C.Switch("g1") ? RimContinuity::Tangent : RimContinuity::Position;
        double DefaultTension = C.SwitchNumber("tension").value_or(1.0);
        uint32_t Support = 0;
        if (auto On = C.SwitchText("on")) { SceneFigure* F = Resolve(*On); if (!F || F->Classification == FigureClassification::Curve) return Refuse("fairpatch: --on needs a surface or body"); Support = F->Identity; }
        CommandLine Bare = C; std::vector<std::pair<std::pair<RimContinuity, double>, int>> PerRim;
        for (std::string& Tok : Bare.Arguments)
        {
            RimContinuity Cont = Default; double Tension = DefaultTension; int Face = -1;
            size_t At;
            while ((At = Tok.rfind('@')) != std::string::npos)
            {
                std::string Tag = Tok.substr(At + 1); Tok.erase(At);
                if (Tag == "g0") Cont = RimContinuity::Position; else if (Tag == "g1") Cont = RimContinuity::Tangent; else if (Tag == "g2") Cont = RimContinuity::Curvature;
                else if (!Tag.empty() && Tag[0] == 't') Tension = std::atof(Tag.c_str() + 1);
                else if (!Tag.empty() && Tag[0] == 'f') Face = std::atoi(Tag.c_str() + 1);
                else return Refuse("fairpatch: unknown rim tag '@%s' (use @g0 @g1 @g2 @t<tension> @f<face>)", Tag.c_str());
            }
            PerRim.push_back({ { Cont, Tension }, Face });
        }
        std::vector<SweepSource> B; if (!CollectSections(Bare, 0, "fairpatch", B)) return false;
        FigureRecipe R; R.Operation = RecipeOperation::FairPatch;
        bool Selected = Bare.Count() == 0 || (Bare.Count() == 1 && Bare.Arguments[0] == "selected");
        for (size_t I = 0; I < B.size(); ++I)
        {
            RecipeInput In = B[I].Input;
            In.Continuity = Selected || I >= PerRim.size() ? Default : PerRim[I].first.first;
            In.Tension = Selected || I >= PerRim.size() ? DefaultTension : PerRim[I].first.second;
            In.Face = Selected || I >= PerRim.size() ? -1 : PerRim[I].second;
            In.Support = In.Shape == RecipeInput::Form::Edge ? 0 : Support;
            if (In.Continuity != RimContinuity::Position && In.Shape != RecipeInput::Form::Edge && !Support) Row("  ⚠ %s: G1/G2 on a curve needs --on=<surface|body>; it will be G0", B[I].Label.c_str());
            R.Sections.push_back(In);
        }
        if (auto G = C.SwitchText("guides"))
        {
            std::string Tok; for (char Ch : *G + ",") { if (Ch == ',') { if (!Tok.empty()) { SceneFigure* F = Resolve(Tok); if (!F || F->Classification != FigureClassification::Curve) return Refuse("fairpatch: guide '%s' is not a curve", Tok.c_str()); RecipeInput In; In.Shape = RecipeInput::Form::Curve; In.Figures = { F->Identity }; R.Guides.push_back(In); } Tok.clear(); } else Tok += Ch; }
        }
        R.Fair.Star = C.Switch("star"); R.Fair.Spans = int(C.SwitchNumber("spans").value_or(10)); R.Fair.Fairness = C.SwitchNumber("fairness").value_or(1.0); R.Fair.Rounds = int(C.SwitchNumber("rounds").value_or(3));
        FairPatchReport Rep;
        Deliver<FigureRecipe::Product> Trial = R.Produce(Scene, Plane, &Rep);
        if (!Trial) return Refuse("FairPatch refused: %s — %s", Refusal::Describe(Trial.Denial.Reason), Trial.Denial.Detail);
        if (!AddDerived(C, "FairPatch", R)) return false;
        Row("  quads %d  unknowns %d  rim break G1 %.3f°  G2 %.4f 1/m  seam break %.3f°  guide deviation %.5f  bending energy %.4f (Coons %.4f)%s",
            Rep.Quads, Rep.Unknowns, ScalarCriteria::Degrees(Rep.TangentBreak), Rep.CurvatureBreak, ScalarCriteria::Degrees(Rep.SeamBreak), Rep.GuideDeviation, Rep.Energy, Rep.CoonsEnergy, Rep.UnsupportedRims ? "  ⚠ unsupported rims fell back to G0" : "");
        return true;
    });
    Add("bridge", "bridge <curve> <curve> [--degree=3] [--no-align] — surface that connects two open curves end-to-end (a 2-section loft; --sheet implied because the two curves are open). This is a thin convenience over `loft` with two sections, named for Plasticity parity.", [=, this](const CommandLine& C)
    {
        std::vector<SweepSource> Sections; if (!CollectSections(C, 0, "bridge", Sections)) return false;
        if (Sections.size() != 2) return Refuse("bridge: exactly two sections (got %zu)", Sections.size());
        if (Sections[0].Loops.size() != 1 || Sections[1].Loops.size() != 1) return Refuse("bridge: each section must be a single open curve (not a closed outer + holes)");
        FigureRecipe R; R.Operation = RecipeOperation::Loft;
        for (const SweepSource& S : Sections) R.Sections.push_back(S.Input);
        R.Loft.DegreeV = int(C.SwitchNumber("degree").value_or(3));
        R.Loft.AlignSeams = R.Loft.AlignSense = !C.Switch("no-align");
        R.Sheet = true;                                                                   // bridge is always a sheet (open curves → open body)
        Row("bridge %s → %s", Sections[0].Label.c_str(), Sections[1].Label.c_str());
        return AddDerived(C, "Bridge", R);
    });
    Add("array", "array <figure...> --count=N --step=(dx,dy,dz)  or  --axis=(ox,oy,oz),(dx,dy,dz) [--angle=deg] [--scale=s] --name=… — linear or radial array of N copies of the figure (a copy at the source, plus N-1 transforms). Linear: --count and --step. Radial: --count, --axis (origin, direction), --angle (total sweep in degrees, default 360). --scale tapers the last copy (1.0 = no taper).", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "array")) return false;
        int Count = int(C.SwitchNumber("count").value_or(0));
        if (Count < 2) return Refuse("array: --count must be ≥ 2 (got %d)", Count);
        bool Radial = C.Switch("axis");
        if (!Radial && !C.Switch("step")) return Refuse("array: specify either --step=(dx,dy,dz) (linear) or --axis=(ox,oy,oz),(dx,dy,dz) (radial)");
        Vec3 Step{ 0, 0, 0 }; Vec3 Origin{ 0, 0, 0 }; Vec3 Axis{ 0, 0, 1 }; double TotalAngle = 360.0; double ScaleEnd = 1.0;
        if (auto S = C.SwitchText("step")) { auto V = CommandCodec::ParsePoint(*S); if (!V) return Refuse("array: --step must be a point"); Step = *V; }
        if (auto S = C.SwitchText("axis"))
        {
            // --axis=(ox,oy,oz),(dx,dy,dz) — split on the first comma between the two triples.
            std::string Tok = *S; size_t Comma = Tok.find("),("); if (Comma == std::string::npos) return Refuse("array: --axis must be '(ox,oy,oz),(dx,dy,dz)'");
            std::string A = Tok.substr(0, Comma + 1); std::string B = Tok.substr(Comma + 2);
            auto P1 = CommandCodec::ParsePoint(A); auto P2 = CommandCodec::ParsePoint(B);
            if (!P1 || !P2) return Refuse("array: --axis must be '(ox,oy,oz),(dx,dy,dz)'");
            Origin = *P1; Axis = *P2;
            if (Axis.Length() <= ScalarCriteria::KernelTolerance) return Refuse("array: --axis direction is zero");
            Axis = Axis.Normalised();
        }
        if (auto A = C.SwitchNumber("angle")) TotalAngle = *A;
        if (auto A = C.SwitchNumber("scale")) ScaleEnd = *A;
        if (ScaleEnd <= 0) return Refuse("array: --scale must be positive");

        int TotalCreated = 0;
        // Resolve first; copy out the data we need so subsequent AddBody calls (which can reallocate Entries and
        //    invalidate the SceneFigure* pointer) don't break the loop.
        struct Seed { FigureClassification Class; std::string Name; NurbsCurve Curve; NurbsSurface Surface; BrepBody Body; };
        std::vector<Seed> Seeds;
        for (SceneFigure* I : ResolveMany(C, 0)) Seeds.push_back({ I->Classification, I->Name, I->Curve, I->Surface, I->Body });
        for (const Seed& S : Seeds)
        {
            std::string BaseName = C.SwitchText("name").value_or(S.Name + ".Array");
            for (int K = 1; K < Count; ++K)                                                // K=0 is the seed itself, no transform needed
            {
                double T = double(K) / double(Count - 1);                                 // 0..1 across the array
                Mat4 M;
                if (Radial)
                {
                    double A = TotalAngle * T * (ScalarCriteria::Pi / 180.0);
                    M = Mat4::Translation(Origin) * Mat4::Rotation(Axis, A) * Mat4::Translation(-Origin);
                    if (ScaleEnd != 1.0) { double S = 1.0 + (ScaleEnd - 1.0) * T; M = Mat4::Translation(Origin) * Mat4::Scaling(Vec3{ S, S, S }) * Mat4::Translation(-Origin) * M; }
                }
                else
                {
                    M = Mat4::Translation(Step * T);
                }
                std::string Name = BaseName + "." + std::to_string(K);
                if (S.Class == FigureClassification::Curve)        (void)Scene.AddCurve(Name, S.Curve.Transformed(M));
                else if (S.Class == FigureClassification::Surface) (void)Scene.AddSurface(Name, S.Surface.Transformed(M));
                else                                               (void)Scene.AddBody(Name, S.Body.Transformed(M));
                ++TotalCreated;
            }
        }
        if (TotalCreated == 0) return Refuse("array: no copies created (no figures matched)");
        Row("array → %d copies%s", TotalCreated, Radial ? "  (radial)" : "  (linear)");
        return true;
    });
    Add("mirror", "mirror <fig...|selected> [--across=xy|xz|yz|<name>|(nx,ny,nz)|((ox,oy,oz),(dx,dy,dz))] [--also=<spec>]... [--in-place|--copy] [--name=<stem>]  ·  default --copy, default --across=xy. The --also flag adds a second (or third) mirror op; with 2 perpendicular planes you get 4 copies, with 3 you get 8.", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "mirror")) return false;
        // Collect target figures.
        std::vector<std::string> Targets;
        bool UseSelected = false;
        for (size_t I = 0; I < C.Count(); ++I)
        {
            const std::string& Tok = C.Arguments[I];
            if (Tok == "selected") UseSelected = true;
            else Targets.push_back(Tok);
        }
        if (UseSelected) for (const auto& F : Scene.Figures()) if (F.Selected) Targets.push_back(F.Name);
        if (Targets.empty()) return Refuse("mirror: at least one figure or `selected` required");
        // Collect mirror specs.
        std::vector<ConsoleHost::MirrorSpec> Specs;
        ConsoleHost::MirrorSpec DefaultSpec; DefaultSpec.Label = "xy";
        Specs.push_back(DefaultSpec);
        if (auto Across = C.SwitchText("across"))
        {
            Specs.clear();
            ConsoleHost::MirrorSpec S;
            if (!ParseMirrorSpec(*Across, S)) return Refuse("mirror: bad --across spec '%s'", Across->c_str());
            Specs.push_back(S);
        }
        // --also=<spec> can be repeated.
        for (size_t I = 0; I < 8; ++I)
        {
            char Key[16]; std::snprintf(Key, sizeof Key, "also%zu", I);
            if (auto Also = C.SwitchText(Key))
            {
                ConsoleHost::MirrorSpec S;
                if (!ParseMirrorSpec(*Also, S)) return Refuse("mirror: bad --also%zu spec '%s'", I, (*Also).c_str());
                Specs.push_back(S);
            }
        }
        // --also (shorthand for --also0) for the common two-plane case.
        if (auto Also = C.SwitchText("also"))
        {
            ConsoleHost::MirrorSpec S;
            if (!ParseMirrorSpec(*Also, S)) return Refuse("mirror: bad --also spec '%s'", Also->c_str());
            Specs.push_back(S);
        }
        bool InPlace = C.Switch("in-place");
        std::string NameStem = C.SwitchText("name").value_or("Mirror");
        // For each target figure, generate 2^N copies (one for each combination of reflect/no-reflect across each spec).
        // We do this by binary enumeration: bit I = 1 means we apply Specs[I] to the source.
        int N = (int)Specs.size();
        if (N > 6) return Refuse("mirror: too many --also specs (max 6)");
        size_t TotalCopies = 0;
        for (const std::string& Target : Targets)
        {
            // Capture by value — Scene.Duplicate may reallocate Entries and invalidate raw pointers.
            const SceneFigure* SrcPtr = Resolve(Target);
            if (!SrcPtr) return Refuse("mirror: no figure '%s'", Target.c_str());
            SceneFigure Src = *SrcPtr;
            if (InPlace)
            {
                // Mutate the source by applying all specs once. We don't enumerate — in-place is a single edit.
                SceneFigure* Mutable = Resolve(Target);
                if (!Mutable) return Refuse("mirror: figure '%s' disappeared after in-place edit", Target.c_str());
                (void)ReflectBlueprint(*Mutable, Specs);
                RebuildFigureFromBlueprint(*Mutable);
                AutoEmitDimensions(*Mutable);
                Row("mirror: '%s' reflected in-place (%zu ops)", Target.c_str(), Specs.size());
                continue;
            }
            for (int Bits = 0; Bits < (1 << N); ++Bits)
            {
                if (Bits == 0) continue;                                          // skip the all-zero case (no transformation = source itself)
                std::vector<ConsoleHost::MirrorSpec> Active;
                for (int I = 0; I < N; ++I) if (Bits & (1 << I)) Active.push_back(Specs[I]);
                std::string NewName = NameStem + "." + Target;
                if ((1 << N) > 2) NewName += "." + std::to_string(Bits);
                (void)MirrorFigureCopy(Src, Active, NewName);
                ++TotalCopies;
            }
        }
        Row("mirror: %zu copies from %zu figures × %d axes", TotalCopies, Targets.size(), (int)Specs.size());
        return true;
    });
    Add("radial", "radial <fig...|selected> --count=N --axis=((ox,oy,oz),(dx,dy,dz)) [--angle=deg=360] [--name=<stem>]  ·  source + (N-1) copies around the axis at evenly-spaced angles. Default --angle=360 (full circle), default --count=2 (source + 1 copy).", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "radial")) return false;
        int Count = int(C.SwitchNumber("count").value_or(2.0));
        if (Count < 2) return Refuse("radial: --count must be ≥ 2");
        double AngleDeg = C.SwitchNumber("angle").value_or(360.0);
        double Step = (3.14159265358979323846 / 180.0) * AngleDeg / Count;
        auto AxisTok = C.SwitchText("axis");
        if (!AxisTok) return Refuse("radial: --axis=(ox,oy,oz),(dx,dy,dz) required");
        // Parse the axis: a 3D line "(origin),(dir)" — same format as `array --axis=...`.
        MirrorAxis Axis;
        size_t Sep = AxisTok->find("),(");
        if (Sep == std::string::npos) return Refuse("radial: bad --axis spec '%s' (expected (ox,oy,oz),(dx,dy,dz))", AxisTok->c_str());
        std::string OStr = AxisTok->substr(0, Sep + 1);
        std::string DStr = AxisTok->substr(Sep + 2);
        auto O = CommandCodec::ParsePoint(OStr);
        auto D = CommandCodec::ParsePoint(DStr);
        if (!O || !D) return Refuse("radial: bad --axis points (got '%s' / '%s')", OStr.c_str(), DStr.c_str());
        Axis.Origin = *O; Axis.Direction = (*D - *O).Normalised();
        if (Axis.Direction.LengthSquared() < 1e-12) return Refuse("radial: axis direction is zero");
        std::string NameStem = C.SwitchText("name").value_or("Radial");
        // Collect target figures.
        std::vector<std::string> Targets;
        bool UseSelected = false;
        for (size_t I = 0; I < C.Count(); ++I)
        {
            const std::string& Tok = C.Arguments[I];
            if (Tok == "selected") UseSelected = true;
            else Targets.push_back(Tok);
        }
        if (UseSelected) for (const auto& F : Scene.Figures()) if (F.Selected) Targets.push_back(F.Name);
        if (Targets.empty()) return Refuse("radial: at least one figure or `selected` required");
        size_t TotalCopies = 0;
        for (const std::string& Target : Targets)
        {
            // Capture by value — Scene.Duplicate may reallocate Entries and invalidate raw pointers.
            const SceneFigure* SrcPtr = Resolve(Target);
            if (!SrcPtr) return Refuse("radial: no figure '%s'", Target.c_str());
            SceneFigure Src = *SrcPtr;
            for (int K = 1; K < Count; ++K)                                      // K=0 is the source itself
            {
                std::string NewName = NameStem + "." + Target + "." + std::to_string(K);
                (void)RadialFigureCopy(Src, Axis, K * Step, NewName);
                ++TotalCopies;
            }
        }
        Row("radial: %zu copies from %zu figures × %d angles (step %.2f°)", TotalCopies, Targets.size(), Count - 1, Step * 180.0 / 3.14159265358979323846);
        return true;
    });
    Add("empty", "empty --name=E --at=(x,y,z)  ·  create a transform handle (no geometry) at the given position. Empties can be mirrored and used as radial array centres.", [=, this](const CommandLine& C)
    {
        std::string Name = C.SwitchText("name").value_or("Empty");
        auto At = C.SwitchText("at");
        if (!At) return Refuse("empty: --at=(x,y,z) required");
        auto P = CommandCodec::ParsePoint(*At);
        if (!P) return Refuse("empty: bad --at point '%s'", At->c_str());
        (void)AddEmpty(*P, Name);
        Row("empty: created '%s' at (%.3f, %.3f, %.3f)", Name.c_str(), P->X, P->Y, P->Z);
        return true;
    });
    Add("recipe", "recipe [figure...] — how derived figures are built (sources, options, complaints)  ·  recipe bake <figure...> detaches them", [=, this](const CommandLine& C)
    {
        if (C.Count() >= 1 && C.Arguments[0] == "bake")
        {
            CommandLine Sub = C; Sub.Arguments.erase(Sub.Arguments.begin());
            int N = 0; for (SceneFigure* F : ResolveMany(Sub, 0)) if (F->Recipe.Live()) { F->Recipe = FigureRecipe(); ++N; Row("#%u %s baked — now authored geometry", F->Identity, F->Name.c_str()); }
            if (!N) return Refuse("recipe bake: no derived figures given");
            return true;
        }
        int N = 0;
        auto Show = [&](const SceneFigure& F)
        {
            if (!F.Recipe.Live()) { if (C.Count()) Row("#%u %-14s authored", F.Identity, F.Name.c_str()); return; }
            ++N; Row("#%u %-14s %s", F.Identity, F.Name.c_str(), F.Recipe.Summary(Scene).c_str());
        };
        if (C.Count() == 0) { for (const SceneFigure& F : Scene.Figures()) Show(F); if (!N) Row("no derived figures"); return true; }
        for (SceneFigure* F : ResolveMany(C, 0)) Show(*F);
        return true;
    });
    Add("dependents", "dependents <figure> — derived figures that follow this one", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "dependents")) return false;
        SceneFigure* F = Resolve(C.Arguments[0]); if (!F) return Refuse("no figure '%s'", C.Arguments[0].c_str());
        std::vector<const SceneFigure*> D = Scene.DerivedFrom(F->Identity);
        if (D.empty()) { Row("#%u %s: nothing depends on it", F->Identity, F->Name.c_str()); return true; }
        for (const SceneFigure* G : D) Row("  #%u %s  (%s)", G->Identity, G->Name.c_str(), Describe(G->Recipe.Operation));
        return true;
    });
    Add("ruled", "ruled <curveA> <curveB>", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "ruled")) return false;
        SceneFigure* A = Resolve(C.Arguments[0]); SceneFigure* B = Resolve(C.Arguments[1]);
        if (!A || !B || A->Classification != FigureClassification::Curve || B->Classification != FigureClassification::Curve) return Refuse("ruled: two curves required");
        return AddSurface(C, "Ruled", NurbsSurface::Ruled(A->Curve, B->Curve));
    });

    //---------------------------------------------- scene ----------------------------------------------
    Add("reset", "reset — clear the scene back to empty (and the workplane to XY)", [=, this](const CommandLine&)
    {
        Scene.Clear();
        Undo = UndoSequence();
        Plane = Workplane::XY();
        CGraph.Clear();                                                         // Phase 18: drop the constraint graph too
        // Dimensions is NOT cleared on reset (Phase 13 scripts depend on dim ids accumulating).
        //    Phase 18's constraint graph references figure names, so it must be cleared; dim ids
        //    can be reused safely because Phase 18's `dim edit` looks up dims by id+anchor.
        //    But Phase 20: hide any auto dim whose anchor figure no longer exists (otherwise the
        //    dim is orphaned and keeps showing on the next render).
        std::vector<uint32_t> LiveIds; for (const SceneFigure& F : Scene.Figures()) LiveIds.push_back(F.Identity);
        for (DimensionEntry& D : Dimensions)
        {
            if (!D.Auto) continue;
            bool Found = false;
            for (uint32_t Id : LiveIds) if (Id == D.Anchor) { Found = true; break; }
            if (!Found) D.Hidden = true;
        }
        Row("scene reset (empty, workplane xy)");
        return true;
    });
    Add("list", "list — every figure with its measurements  ·  list empty — only Empty figures (transform handles)", [=, this](const CommandLine& C)
    {
        if (C.Count() >= 1 && C.Arguments[0] == "empty")
        {
            size_t N = 0;
            for (const auto& F : Scene.Figures())
            {
                if (F.Classification != FigureClassification::Empty) continue;
                Row("empty #%u  '%s'  at (%.3f, %.3f, %.3f)", F.Identity, F.Name.c_str(), F.Blueprint.A.X, F.Blueprint.A.Y, F.Blueprint.A.Z);
                ++N;
            }
            if (N == 0) Row("(no empties)");
            return true;
        }
        if (Scene.Figures().empty()) Row("(empty scene)");
        for (const SceneFigure& I : Scene.Figures()) DescribeFigure(I);
        return true;
    });
    // ── Dimensions (Phase 13) ──────────────────────────────────────────────────────────────
    Add("dim", "dim list  ·  dim <figure> --along=X|Y|Z [--name=label]  ·  dim <figure> <p1> <p2>  ·  dim <figure> face <F> [--leader=(x,y,z)]  ·  dim <figure> edge <E> [--leader=(x,y,z)]  ·  dim sub <figure>  ·  dim leader <figure> (x,y,z) [text]  ·  dim edit <id> <value>  ·  dim hide|show|delete <id|all>  ·  dims are auto-emitted on every primitive and body", [=, this](const CommandLine& C)
    {
        if (C.Count() == 0) return Refuse("dim: try `dim list`, `dim <figure> --along=X`, `dim edit <id> <value>`, or `dim hide|show|delete <id|all>`");
        const std::string& Sub = C.Arguments[0];
        // Phase 15: parse a dim identifier. Accepts a numeric id ("12"), the keyword "all" (returns -1),
        //    or a name like "B Y" / "P Y2" — matched against AnchorName. Returns the matching dim's
        //    Id, or 0 on no match.
        auto IdArg = [&](size_t I) -> int32_t
        {
            if (I >= C.Count()) return 0;
            const std::string& Tok = C.Arguments[I];
            if (auto N = CommandCodec::ParseNumber(Tok)) return int32_t(*N);
            if (Tok == "all") return -1;
            // Try matching as a dim name. Allow two-token form: "B Y" → C.Arguments[I] + " " + C.Arguments[I+1].
            std::string Name = Tok;
            if (I + 1 < C.Count())
            {
                std::string Two = Tok + " " + C.Arguments[I + 1];
                for (const DimensionEntry& D : Dimensions) if (D.AnchorName == Two) return int32_t(D.Id);
            }
            for (const DimensionEntry& D : Dimensions) if (D.AnchorName == Name) return int32_t(D.Id);
            return 0;
        };
        if (Sub == "list")
        {
            if (Dimensions.empty()) { Row("(no dimensions)"); return true; }
            for (const DimensionEntry& D : Dimensions)
            {
                const char* K = "?";
                switch (D.Form)
                {
                    case DimensionForm::Linear:    K = "linear";    break;
                    case DimensionForm::Angle:     K = "angle";     break;
                    case DimensionForm::Radius:    K = "radius";    break;
                    case DimensionForm::Diameter:  K = "diameter";  break;
                    case DimensionForm::ArcLength: K = "length";    break;
                    case DimensionForm::Bbox:      K = "bbox";      break;
                }
                Row("#%-3u %-8s %-24s  value %.4f  %s%s", D.Id, K, D.AnchorName.c_str(), D.Value, D.Hidden ? "  [hidden]" : "", D.Auto ? "  [auto]" : "  [user]");
            }
            return true;
        }
        if (Sub == "edit")
        {
            int32_t Id = IdArg(1); if (Id <= 0) return Refuse("dim edit: a numeric dim id or a dim name like 'B Y2' required");
            // The value is the LAST token. If IdArg matched a 2-token name (e.g. "B Y2"), then the
            //    value is at index 3; otherwise at index 2.
            size_t ValIdx = 2;
            if (C.Count() >= 3)
            {
                std::string Two = std::string(C.Arguments[1]) + " " + C.Arguments[2];
                for (const DimensionEntry& D : Dimensions) if (D.AnchorName == Two) { ValIdx = 3; break; }
            }
            double NewVal = 0; if (!NumberArg(C, ValIdx, NewVal, "dim edit")) return false;
            for (DimensionEntry& D : Dimensions) if (int32_t(D.Id) == Id)
            {
                // Try a live edit first (Phase 13 redo: rebuilds the figure from its source).
                if (D.Slot >= 0)
                {
                    // Phase 18 bug: ApplyLiveEdit calls AutoEmitDimensions which can re-allocate the
                    //    Dimensions vector (DeleteAutoDimensionsFor erases elements, AutoEmitDimensions
                    //    pushes new ones). Capturing values from D before the call and looking up the
                    //    new dim by id afterward is safer. We also capture the figure identity for
                    //    the re-solve hook.
                    uint32_t    CapturedId      = D.Id;
                    uint32_t    CapturedAnchor  = D.Anchor;
                    std::string CapturedAnchorName = D.AnchorName;
                    if (ApplyLiveEdit(D, NewVal))
                    {
                        // Find the new dim with the same anchor. ApplyLiveEdit's AutoEmitDimensions
                        //    re-issued dims for the same figure, with new ids.
                        DimensionEntry* NewD = nullptr;
                        for (auto& DN : Dimensions) if (DN.Anchor == CapturedAnchor && DN.AnchorName == CapturedAnchorName) { NewD = &DN; break; }
                        if (NewD) NewD->Label = "";
                        Row("dim #%u  value %.4f  (live edit, figure rebuilt)", CapturedId, NewVal);
                        // Phase 18: dim-edit re-solve hook. If the edited figure is referenced by any
                        //    constraint, re-solve the constraint graph. The new Blueprint value flows
                        //    through the constraint equations and other affected figures move to satisfy
                        //    them. We check the figure name (D.AnchorName often starts with the figure
                        //    name) — but a more robust check is the figure identity. Look up the figure
                        //    by its identity, get its name, then check the constraint graph.
                        if (CGraph.ConstraintCount() > 0)
                        {
                            // Find the figure name for this dim's anchor.
                            std::string EditedName;
                            for (const auto& F : Scene.Figures()) if (F.Identity == CapturedAnchor) { EditedName = F.Name; break; }
                            bool Referenced = false;
                            for (const auto& A : CGraph.AllAnchors()) if (A.Figure == EditedName) { Referenced = true; break; }
                            if (Referenced)
                            {
                                Row("dim #%u: figure '%s' is in the constraint graph — re-solving", CapturedId, EditedName.c_str());
                                if (!SolveConstraintGraph()) { Row("dim #%u: re-solve refused (constraint may be inconsistent with the new value)", CapturedId); }
                            }
                        }
                        return true;
                    }
                    return Refuse("dim #%u: live edit refused (slot %d on form %d)", D.Id, D.Slot, int(D.BlueprintForm));
                }
                // Free-form / non-live dim: just update the label.
                D.Value = NewVal; D.Label = ""; Row("dim #%u  value %.4f  (read-only label override)", D.Id, D.Value); return true;
            }
            return Refuse("dim edit: no dim with id %d", Id);
        }
        if (Sub == "hide" || Sub == "show" || Sub == "delete")
        {
            int32_t Id = IdArg(1); if (Id == 0) return Refuse("dim %s: a numeric dim id (or `all`) required", Sub.c_str());
            auto Match = [&](const DimensionEntry& D) { return Id < 0 || int32_t(D.Id) == Id; };
            if (Sub == "delete")
            {
                size_t Before = Dimensions.size();
                Dimensions.erase(std::remove_if(Dimensions.begin(), Dimensions.end(), Match), Dimensions.end());
                Row("dim delete: removed %zu", Before - Dimensions.size());
                return true;
            }
            for (DimensionEntry& D : Dimensions) if (Match(D)) D.Hidden = (Sub == "hide");
            Row("dim %s: %s %d", Sub.c_str(), Id < 0 ? "all dims" : "dim", Id);
            return true;
        }
        if (Sub == "all-auto" || Sub == "auto")
        {
            // Force re-emit of all auto dims (useful after `undelete` or when an existing figure was loaded).
            for (const SceneFigure& F : Scene.Figures()) AutoEmitDimensions(F);
            Row("dim auto: re-emitted (%zu total)", Dimensions.size());
            return true;
        }
        if (Sub == "on" || Sub == "off" || Sub == "show-all" || Sub == "hide-all")
        {
            // Phase 13: dims are hidden by default (renderer is being polished in Phase 14). `dim on`
            //    makes them visible, `dim off` hides them again. The dim data is preserved either way.
            bool Want = (Sub == "on" || Sub == "show-all");
            ShowDimensions = Want;
            // Also flip the Hidden flag on every dim so DrawDimensions skips them, since the overlay
            //    pass checks both: cheap and avoids the world-space overlay work when dims are off.
            for (DimensionEntry& D : Dimensions) D.Hidden = !Want;
            Row("dim display: %s (%zu dims %s)", Want ? "on" : "off", Dimensions.size(), Want ? "shown" : "hidden");
            return true;
        }
        // dim sub <figure>  — auto-emit a read-only dim on every face and every edge of the body.
        //    Useful for dense inspection. Each sub-entity gets one dim. Phase 17.
        if (Sub == "sub" && C.Count() >= 2)
        {
            SceneFigure* Fig = Resolve(C.Arguments[1]); if (!Fig) return Refuse("dim sub: no figure '%s'", C.Arguments[1].c_str());
            if (Fig->Classification != FigureClassification::Body) return Refuse("dim sub: '%s' is not a body (sub-entity dims are body-only)", Fig->Name.c_str());
            const BrepBody& B = Fig->Body;
            // Helper: face area via tessellation. Phase 17: cheap enough at script speed (2 mm chord).
            auto FaceArea = [&](int FaceIdx) -> double
            {
                BrepBody::FaceTriangles T = B.TessellateFace(FaceIdx, 2e-3);
                double Sum = 0;
                for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
                {
                    Vec3 A = T.Positions[T.Triangles[I + 0]];
                    Vec3 Bp = T.Positions[T.Triangles[I + 1]];
                    Vec3 Cp = T.Positions[T.Triangles[I + 2]];
                    Sum += 0.5 * (Bp - A).Cross(Cp - A).Length();
                }
                return Sum;
            };
            // Helper: face perimeter = sum of edge lengths around the outer + hole loops.
            auto FacePerimeter = [&](int FaceIdx) -> double
            {
                if (FaceIdx < 0 || FaceIdx >= (int)B.Faces.size()) return 0;
                const BrepFace& F = B.Faces[FaceIdx];
                double Sum = 0;
                auto WalkLoop = [&](int LoopIdx)
                {
                    if (LoopIdx < 0 || LoopIdx >= (int)B.Loops.size()) return;
                    for (int Ce : B.Loops[LoopIdx].Coedges)
                    {
                        if (Ce < 0 || Ce >= (int)B.Coedges.size()) continue;
                        int E = B.Coedges[Ce].Edge;
                        if (E < 0 || E >= (int)B.Edges.size()) continue;
                        Sum += B.Edges[E].Curve.Length();
                    }
                };
                for (int L : F.Loops) WalkLoop(L);
                return Sum;
            };
            int Emitted = 0;
            // Per-face: a length dim around the face perimeter, plus a "sub-face" area dim off to the side.
            for (int FaceIdx = 0; FaceIdx < (int)B.Faces.size(); ++FaceIdx)
            {
                if (B.Faces[FaceIdx].Surface.Poles.empty()) continue;
                Box3 Fb = B.Faces[FaceIdx].Surface.Bounds();
                Vec3 Lo = Vec3(Fb.Low.X, Fb.High.Y + 0.05, (Fb.Low.Z + Fb.High.Z) * 0.5);
                Vec3 Hi = Vec3(Fb.High.X, Fb.High.Y + 0.05, (Fb.Low.Z + Fb.High.Z) * 0.5);
                double Perim = FacePerimeter(FaceIdx);
                DimensionEntry D; D.Form = DimensionForm::Bbox; D.Anchor = Fig->Identity; D.AnchorName = Fig->Name + " face" + std::to_string(FaceIdx) + " perim";
                D.A = Lo; D.B = Hi; D.N = Vec3(0, 1, 0); D.Value = Perim; D.Slot = -1; D.AnchorFace = FaceIdx;
                D.Id = NextDimensionId++; D.Auto = true; Dimensions.push_back(std::move(D));
                // Area dim: placed a bit further out so it doesn't overlap the perim dim.
                double Area = FaceArea(FaceIdx);
                Lo = Vec3(Fb.Low.X, Fb.High.Y + 0.10, (Fb.Low.Z + Fb.High.Z) * 0.5);
                Hi = Vec3(Fb.High.X, Fb.High.Y + 0.10, (Fb.Low.Z + Fb.High.Z) * 0.5);
                DimensionEntry A; A.Form = DimensionForm::Bbox; A.Anchor = Fig->Identity; A.AnchorName = Fig->Name + " face" + std::to_string(FaceIdx) + " area";
                A.A = Lo; A.B = Hi; A.N = Vec3(0, 1, 0); A.Value = Area; A.Slot = -1; A.AnchorFace = FaceIdx;
                A.Id = NextDimensionId++; A.Auto = true; Dimensions.push_back(std::move(A));
                Emitted += 2;
            }
            // Per-edge: a length dim along the edge, lifted by 4 cm along the edge's outward normal (averaged from adjacent faces).
            for (int EdgeIdx = 0; EdgeIdx < (int)B.Edges.size(); ++EdgeIdx)
            {
                if (B.Edges[EdgeIdx].Curve.PoleCount() == 0) continue;
                Vec3 Lo = B.Edges[EdgeIdx].Curve.Sample(B.Edges[EdgeIdx].Curve.DomainStart());
                Vec3 Hi = B.Edges[EdgeIdx].Curve.Sample(B.Edges[EdgeIdx].Curve.DomainEnd());
                // Average the adjacent faces' outward normals so the lift points "out" of the body, not along one face.
                Vec3 N(0, 0, 0); int Cnt = 0;
                for (int Ce : B.Edges[EdgeIdx].Coedges)
                {
                    if (Ce < 0 || Ce >= (int)B.Coedges.size()) continue;
                    int FaceIdx = B.Coedges[Ce].Face;
                    if (FaceIdx < 0 || FaceIdx >= (int)B.Faces.size()) continue;
                    double Um = 0.5 * (B.Faces[FaceIdx].Surface.DomainStartU() + B.Faces[FaceIdx].Surface.DomainEndU());
                    double Vm = 0.5 * (B.Faces[FaceIdx].Surface.DomainStartV() + B.Faces[FaceIdx].Surface.DomainEndV());
                    Vec3 Ff = B.FaceNormal(FaceIdx, Um, Vm);
                    if (B.Coedges[Ce].Reversed) Ff = -Ff;
                    N = N + Ff; ++Cnt;
                }
                if (Cnt > 0) N = (N * (1.0 / Cnt)).Normalised(); else N = Vec3(0, 1, 0);
                if (N.LengthSquared() < 1e-12) N = Vec3(0, 1, 0);
                DimensionEntry D; D.Form = DimensionForm::Linear; D.Anchor = Fig->Identity; D.AnchorName = Fig->Name + " edge" + std::to_string(EdgeIdx) + " length";
                D.A = Lo; D.B = Hi; D.N = N; D.Value = B.Edges[EdgeIdx].Curve.Length(); D.Slot = -1; D.AnchorEdge = EdgeIdx;
                D.Id = NextDimensionId++; D.Auto = true; Dimensions.push_back(std::move(D));
                ++Emitted;
                // For circular / arc edges, also emit a radius dim from the edge's analytic centre so the
                //    user can see the radius without running a separate `dim edge` command.
                if (B.Edges[EdgeIdx].Curve.Classification == CurveClassification::Circle || B.Edges[EdgeIdx].Curve.Classification == CurveClassification::Arc)
                {
                    Vec3 Ctr = B.Edges[EdgeIdx].Curve.Centre;
                    DimensionEntry R; R.Form = DimensionForm::Radius; R.Anchor = Fig->Identity; R.AnchorName = Fig->Name + " edge" + std::to_string(EdgeIdx) + " radius";
                    R.A = Ctr; R.B = (Lo + Hi) * 0.5; R.N = N; R.Value = B.Edges[EdgeIdx].Curve.RadiusMajor; R.Slot = -1; R.AnchorEdge = EdgeIdx;
                    R.Id = NextDimensionId++; R.Auto = true; Dimensions.push_back(std::move(R));
                    ++Emitted;
                }
            }
            Row("dim sub: %s  ·  %d face dims, %d edge dims (%d total)", Fig->Name.c_str(), 2 * (int)B.Faces.size(), (int)B.Edges.size(), Emitted);
            return true;
        }
        // dim leader <figure> (x,y,z) [text...]  — a free-floating leader. Phase 17.
        if (Sub == "leader" && C.Count() >= 3)
        {
            SceneFigure* Fig = Resolve(C.Arguments[1]); if (!Fig) return Refuse("dim leader: no figure '%s'", C.Arguments[1].c_str());
            auto P = CommandCodec::ParsePoint(C.Arguments[2]); if (!P) return Refuse("dim leader: '%s' is not a point", C.Arguments[2].c_str());
            // Feature point: if the user has a single face/edge selected, snap to the face centroid / edge midpoint.
            //    Otherwise use the figure's bounding-box centre.
            Vec3 Feature;
            if (Fig->Classification == FigureClassification::Body && (Fig->SelectedFaces.size() == 1 || Fig->SelectedEdges.size() == 1))
            {
                const BrepBody& B = Fig->Body;
                if (Fig->SelectedFaces.size() == 1)
                {
                    int FaceIdx = Fig->SelectedFaces[0];
                    Box3 Bf = B.Faces[FaceIdx].Surface.Bounds();
                    Feature = (Bf.Low + Bf.High) * 0.5;
                }
                else
                {
                    int EdgeIdx = Fig->SelectedEdges[0];
                    const BrepEdge& Ed = B.Edges[EdgeIdx];
                    Feature = (Ed.Curve.Sample(Ed.Curve.DomainStart()) + Ed.Curve.Sample(Ed.Curve.DomainEnd())) * 0.5;
                }
            }
            else
            {
                Box3 Bf = Fig->Bounds();
                Feature = (Bf.Low + Bf.High) * 0.5;
            }
            // Optional label: any remaining arguments are joined with a space.
            std::string Text;
            for (size_t I = 3; I < C.Count(); ++I) { if (!Text.empty()) Text += " "; Text += C.Arguments[I]; }
            DimensionEntry D; D.Form = DimensionForm::Linear; D.Anchor = Fig->Identity; D.AnchorName = Fig->Name + " leader";
            D.A = Feature; D.B = *P; D.N = Vec3(0, 1, 0); D.Value = (*P - Feature).Length(); D.Slot = -1;
            D.Leader = true; D.Label = Text;                                                                    // empty Label → format D.Value
            D.Id = NextDimensionId++; D.Auto = false; Dimensions.push_back(std::move(D));
            Row("dim #%u  %s leader → (%.3f %.3f %.3f)  text '%s'", D.Id, Fig->Name.c_str(), P->X, P->Y, P->Z, Text.c_str());
            return true;
        }
        // dim <figure> [...]  — user-added linear dim. First figure is the anchor; the rest are switch values.
        SceneFigure* F = Resolve(Sub);
        if (!F) return Refuse("dim: no figure '%s' and not a recognised subcommand (try `dim list`)", Sub.c_str());
        // dim <figure> face <F> [--leader=(x,y,z)]  — emit a face-anchored dim (area + perimeter, or leader).
        if (C.Count() >= 3 && C.Arguments[1] == "face")
        {
            int FaceIdx; if (auto V = CommandCodec::ParseNumber(C.Arguments[2])) FaceIdx = int(*V); else return Refuse("dim <figure> face: expected a face index");
            if (F->Classification != FigureClassification::Body) return Refuse("dim <figure> face: '%s' is not a body", F->Name.c_str());
            if (FaceIdx < 0 || FaceIdx >= (int)F->Body.Faces.size()) return Refuse("dim <figure> face: index %d out of range 0..%zu", FaceIdx, F->Body.Faces.size() - 1);
            const BrepFace& Face = F->Body.Faces[FaceIdx];
            // Face area via tessellation.
            BrepBody::FaceTriangles T = F->Body.TessellateFace(FaceIdx, 2e-3);
            double Area = 0;
            for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
            {
                Vec3 A = T.Positions[T.Triangles[I + 0]];
                Vec3 Bp = T.Positions[T.Triangles[I + 1]];
                Vec3 Cp = T.Positions[T.Triangles[I + 2]];
                Area += 0.5 * (Bp - A).Cross(Cp - A).Length();
            }
            Box3 Fb = Face.Surface.Bounds();
            Vec3 Centroid = (Fb.Low + Fb.High) * 0.5;
            // Perimeter = sum of edge lengths around the face's outer + hole loops.
            double Perim = 0;
            auto WalkLoop = [&](int LoopIdx)
            {
                if (LoopIdx < 0 || LoopIdx >= (int)F->Body.Loops.size()) return;
                for (int Ce : F->Body.Loops[LoopIdx].Coedges)
                {
                    if (Ce < 0 || Ce >= (int)F->Body.Coedges.size()) continue;
                    int E = F->Body.Coedges[Ce].Edge;
                    if (E < 0 || E >= (int)F->Body.Edges.size()) continue;
                    Perim += F->Body.Edges[E].Curve.Length();
                }
            };
            for (int L : Face.Loops) WalkLoop(L);
            // Leader switch: --leader=(lx,ly,lz) places the label at that point.
            bool WantLeader = C.Switch("leader");
            Vec3 LabelPos = Centroid + Vec3(0, Fb.Diagonal() * 0.25, 0);
            if (auto L = C.SwitchText("leader")) if (auto V = CommandCodec::ParsePoint(*L)) LabelPos = *V;
            // Emit two dims: one for area (read-only), one for perimeter (read-only). Both face-anchored.
            DimensionEntry A; A.Form = DimensionForm::Bbox; A.Anchor = F->Identity; A.AnchorName = F->Name + " face" + std::to_string(FaceIdx) + " area";
            A.A = Centroid; A.B = LabelPos; A.N = Vec3(0, 1, 0); A.Value = Area; A.Slot = -1; A.AnchorFace = FaceIdx;
            A.Leader = WantLeader; A.Id = NextDimensionId++; A.Auto = false; Dimensions.push_back(std::move(A));
            DimensionEntry P; P.Form = DimensionForm::Bbox; P.Anchor = F->Identity; P.AnchorName = F->Name + " face" + std::to_string(FaceIdx) + " perim";
            P.A = Vec3(Fb.Low.X, Fb.High.Y + 0.05, (Fb.Low.Z + Fb.High.Z) * 0.5);
            P.B = Vec3(Fb.High.X, Fb.High.Y + 0.05, (Fb.Low.Z + Fb.High.Z) * 0.5);
            P.N = Vec3(0, 1, 0); P.Value = Perim; P.Slot = -1; P.AnchorFace = FaceIdx;
            P.Id = NextDimensionId++; P.Auto = false; Dimensions.push_back(std::move(P));
            Row("dim #%u  %s face %d area %.4f  perim %.4f%s", P.Id, F->Name.c_str(), FaceIdx, Area, Perim, WantLeader ? "  (leader)" : "");
            return true;
        }
        // dim <figure> edge <E> [--leader=(x,y,z)]  — emit an edge-anchored dim (length, plus radius for circle/arc edges).
        if (C.Count() >= 3 && C.Arguments[1] == "edge")
        {
            int EdgeIdx; if (auto V = CommandCodec::ParseNumber(C.Arguments[2])) EdgeIdx = int(*V); else return Refuse("dim <figure> edge: expected an edge index");
            if (F->Classification != FigureClassification::Body) return Refuse("dim <figure> edge: '%s' is not a body", F->Name.c_str());
            if (EdgeIdx < 0 || EdgeIdx >= (int)F->Body.Edges.size()) return Refuse("dim <figure> edge: index %d out of range 0..%zu", EdgeIdx, F->Body.Edges.size() - 1);
            const BrepEdge& Ed = F->Body.Edges[EdgeIdx];
            Vec3 Lo = Ed.Curve.Sample(Ed.Curve.DomainStart());
            Vec3 Hi = Ed.Curve.Sample(Ed.Curve.DomainEnd());
            // Lift direction: average the adjacent face normals (outward) so the dim line sits above the edge.
            Vec3 N(0, 0, 0); int Cnt = 0;
            for (int Ce : Ed.Coedges)
            {
                if (Ce < 0 || Ce >= (int)F->Body.Coedges.size()) continue;
                int Ff = F->Body.Coedges[Ce].Face;
                if (Ff < 0 || Ff >= (int)F->Body.Faces.size()) continue;
                double Um = 0.5 * (F->Body.Faces[Ff].Surface.DomainStartU() + F->Body.Faces[Ff].Surface.DomainEndU());
                double Vm = 0.5 * (F->Body.Faces[Ff].Surface.DomainStartV() + F->Body.Faces[Ff].Surface.DomainEndV());
                Vec3 Fn = F->Body.FaceNormal(Ff, Um, Vm);
                if (F->Body.Coedges[Ce].Reversed) Fn = -Fn;
                N = N + Fn; ++Cnt;
            }
            if (Cnt > 0) N = (N * (1.0 / Cnt)).Normalised(); else N = Vec3(0, 1, 0);
            if (N.LengthSquared() < 1e-12) N = Vec3(0, 1, 0);
            bool WantLeader = C.Switch("leader");
            Vec3 LabelPos = (Lo + Hi) * 0.5 + N * 0.3;
            if (auto L = C.SwitchText("leader")) if (auto V = CommandCodec::ParsePoint(*L)) LabelPos = *V;
            // Length dim.
            DimensionEntry D; D.Form = DimensionForm::Linear; D.Anchor = F->Identity; D.AnchorName = F->Name + " edge" + std::to_string(EdgeIdx) + " length";
            D.A = Lo; D.B = Hi; D.N = N; D.Value = Ed.Curve.Length(); D.Slot = -1; D.AnchorEdge = EdgeIdx;
            D.Leader = WantLeader; D.Id = NextDimensionId++; D.Auto = false; Dimensions.push_back(std::move(D));
            // If the edge is a circle or an arc, also emit a radius dim from the edge's analytic centre.
            if (Ed.Curve.Classification == CurveClassification::Circle || Ed.Curve.Classification == CurveClassification::Arc)
            {
                Vec3 Ctr = Ed.Curve.Centre;
                DimensionEntry R; R.Form = DimensionForm::Radius; R.Anchor = F->Identity; R.AnchorName = F->Name + " edge" + std::to_string(EdgeIdx) + " radius";
                R.A = Ctr; R.B = (Lo + Hi) * 0.5; R.N = Plane.Normal(); R.Value = Ed.Curve.RadiusMajor; R.Slot = -1; R.AnchorEdge = EdgeIdx;
                R.Id = NextDimensionId++; R.Auto = false; Dimensions.push_back(std::move(R));
            }
            Row("dim #%u  %s edge %d length %.4f%s", D.Id, F->Name.c_str(), EdgeIdx, Ed.Curve.Length(), WantLeader ? "  (leader)" : "");
            return true;
        }
        if (C.Switch("along"))
        {
            std::string A = *C.SwitchText("along");
            Box3 B = F->Bounds();
            double V = 0; Vec3 Lo{}, Hi{}, Lift(0, 1, 0);
            if (A == "X" || A == "x") { V = B.High.X - B.Low.X; Lo = Vec3(B.Low.X, B.High.Y, (B.Low.Z + B.High.Z) * 0.5); Hi = Vec3(B.High.X, B.High.Y, (B.Low.Z + B.High.Z) * 0.5); Lift = Vec3(0, 1, 0); }
            else if (A == "Y" || A == "y") { V = B.High.Y - B.Low.Y; Lo = Vec3(B.High.X, B.Low.Y, (B.Low.Z + B.High.Z) * 0.5); Hi = Vec3(B.High.X, B.High.Y, (B.Low.Z + B.High.Z) * 0.5); Lift = Vec3(1, 0, 0); }
            else if (A == "Z" || A == "z") { V = B.High.Z - B.Low.Z; Lo = Vec3((B.Low.X + B.High.X) * 0.5, (B.Low.Y + B.High.Y) * 0.5, B.Low.Z); Hi = Vec3((B.Low.X + B.High.X) * 0.5, (B.Low.Y + B.High.Y) * 0.5, B.High.Z); Lift = Vec3(1, 0, 0); }
            else return Refuse("dim --along=: expected X, Y, or Z (got '%s')", A.c_str());
            uint32_t NewId = EmitDimension(DimensionForm::Linear, F->Identity, F->Name + " " + A, Lo, Hi, Lift, V, false);
            Row("dim #%u  %s %.4f", NewId, (F->Name + " " + A).c_str(), V);
            return true;
        }
        // dim <figure> <p1> <p2>  — free-form linear dim between two world points.
        if (C.Count() >= 3)
        {
            auto P1 = CommandCodec::ParsePoint(C.Arguments[1]);
            auto P2 = CommandCodec::ParsePoint(C.Arguments[2]);
            if (!P1 || !P2) return Refuse("dim <figure> <p1> <p2>: two points required");
            double V = (*P2 - *P1).Length();
            uint32_t NewId = EmitDimension(DimensionForm::Linear, F->Identity, F->Name + " free", *P1, *P2, Vec3(0, 1, 0), V, false);
            Row("dim #%u  %s free %.4f", NewId, F->Name.c_str(), V);
            return true;
        }
        return Refuse("dim: need `--along=X|Y|Z` or two points");
    });
    Add("constraint", "constraint list | clear | dof | solve | pin <f.point>  ·  constraint distance <fA.p> <fB.p> = <v>  ·  constraint angle <lineA> <lineB> = <deg>  ·  constraint coincident <fA.p> <fB.p>  ·  constraint horizontal <fA.p> <fB.p>  ·  constraint vertical <fA.p> <fB.p>  ·  constraint parallel <lineA> <lineB>  ·  constraint perpendicular <lineA> <lineB>  ·  constraint equal <lineA> <lineB>  ·  constraint equal-radius <circleA> <circleB>  ·  constraint delete <id>  — 2D constraint graph (Newton solve, dim-edit re-solve hook)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "constraint")) return false;
        const std::string& Sub = C.Arguments[0];
        // ---- sub-verbs -------------------------------------------------------------------
        if (Sub == "list")
        {
            if (CGraph.ConstraintCount() == 0) { Row("constraint: graph is empty"); return true; }
            for (const auto& E : CGraph.AllConstraints())
            {
                Row("constraint #%u  %s  (active=%s)", E.Id, E.Note.c_str(), E.C.Active ? "true" : "false");
            }
            return true;
        }
        if (Sub == "clear")
        {
            size_t N = CGraph.ConstraintCount();
            CGraph.Clear();
            Row("constraint: cleared %zu constraints", N);
            return true;
        }
        if (Sub == "dof")
        {
            if (CGraph.ConstraintCount() == 0) { Row("constraint dof: graph is empty (no constraints)"); return true; }
            // Build a temporary solver to compute the dof.
            ConstraintSolver S;
            // Materialise anchors into the solver.
            for (const auto& A : CGraph.AllAnchors())
            {
                SketchUnknown U; U.Ref = A.Ref; U.Fixed = A.Fixed;
                SceneFigure* F = nullptr;
                for (auto& SF : Scene.Figures()) if (SF.Name == A.Figure) { F = &SF; break; }
                if (!F) continue;
                // Read the current 2D point from the figure's Blueprint (projected to the workplane).
                Vec3 W3 = ReadBlueprintPoint(*F, A.Slot, A.SubIndex, A.Component);
                Vec2 P2 = Plane.ToLocal(W3);
                U.X = P2.X; U.Y = P2.Y;
                S.SetUnknown(U);
            }
            for (const auto& E : CGraph.AllConstraints()) S.AddConstraint(E.C);
            int DoF = S.DoF();
            Row("constraint dof: %d (negative = over-constrained, 0 = well-determined, positive = under-determined)", DoF);
            return true;
        }
        if (Sub == "solve")
        {
            if (CGraph.ConstraintCount() == 0) return Refuse("constraint solve: graph is empty");
            return SolveConstraintGraph();
        }
        if (Sub == "delete")
        {
            if (!Need(C, 2, "constraint delete")) return false;
            double Did = 0; if (!NumberArg(C, 1, Did, "constraint delete")) return false;
            int Id = int(Did);
            CGraph.RemoveConstraint(uint32_t(Id));
            Row("constraint: deleted #%d", Id);
            return true;
        }
        if (Sub == "pin")
        {
            if (!Need(C, 2, "constraint pin")) return false;
            PointRef Ref; std::string Figure; int Slot, Sub, Comp;
            if (!ParsePointRef(C.Arguments[1], Ref, Figure, Slot, Sub, Comp)) return Refuse("constraint pin: bad point ref '%s'", C.Arguments[1].c_str());
            CGraph.AddAnchor(Ref, Figure, Slot, Sub, Comp);
            CGraph.SetFixed(Ref, true);
            Row("constraint: pinned %s", C.Arguments[1].c_str());
            return true;
        }
        // ---- constraint-creating sub-verbs ----------------------------------------------
        if (!Need(C, 2, "constraint")) return false;
        if (Sub == "distance")
        {
            // constraint distance <refA> <refB> = <value>
            // The verb is already stripped; the arguments are [distance, refA, refB, =, value]? No.
            // The outer verb is "constraint", and the parser strips only the verb. So:
            //   C.Arguments = ["distance", "refA", "refB", "=", "value"]
            if (C.Count() < 5) return Refuse("constraint distance: usage: constraint distance <refA> <refB> = <value>");
            const std::string& RefA = C.Arguments[1];
            const std::string& RefB = C.Arguments[2];
            if (std::string(C.Arguments[3]) != "=") return Refuse("constraint distance: expected '=' between refB and value");
            PointRef A, B; std::string FA, FB; int SA, SB, IA, IB, CA, CB;
            if (!ParsePointRef(RefA, A, FA, SA, IA, CA)) return Refuse("constraint distance: bad ref '%s'", RefA.c_str());
            if (!ParsePointRef(RefB, B, FB, SB, IB, CB)) return Refuse("constraint distance: bad ref '%s'", RefB.c_str());
            double V = 0; if (!NumberArg(C, 4, V, "constraint distance")) return false;
            Constraint K; K.Type = ConstraintType::Distance; K.P1 = A; K.P2 = B; K.Prescribed = V; K.Active = true;
            CGraph.AddAnchor(A, FA, SA, IA, CA);
            CGraph.AddAnchor(B, FB, SB, IB, CB);
            uint32_t Id = CGraph.AddConstraint(K, "distance " + RefA + " " + RefB + " = " + std::to_string(V));
            Row("constraint #%u  distance %s %s = %g  (added)", Id, RefA.c_str(), RefB.c_str(), V);
            return true;
        }
        if (Sub == "angle")
        {
            // constraint angle <lineA> <lineB> = <degrees>
            // C.Arguments = ["angle", "lineA", "lineB", "=", "deg"]
            if (C.Count() < 5) return Refuse("constraint angle: usage: constraint angle <lineA> <lineB> = <deg>");
            const std::string& LA = C.Arguments[1];
            const std::string& LB = C.Arguments[2];
            if (std::string(C.Arguments[3]) != "=") return Refuse("constraint angle: expected '=' between refs and value");
            double Deg = 0; if (!NumberArg(C, 4, Deg, "constraint angle")) return false;
            double Rad = Deg * 3.14159265358979323846 / 180.0;
            // The line L1 contributes two points: its start and end. We add all four as anchors.
            std::string A1Fig = LA; PointRefKind A1K = PointRefKind::LineStart; int A1Slot = 0;
            std::string A2Fig = LA; PointRefKind A2K = PointRefKind::LineEnd; int A2Slot = 1;
            std::string B1Fig = LB; PointRefKind B1K = PointRefKind::LineStart; int B1Slot = 0;
            std::string B2Fig = LB; PointRefKind B2K = PointRefKind::LineEnd; int B2Slot = 1;
            PointRef P1 {A1Fig, A1K, 0}, P2 {A2Fig, A2K, 0}, P3 {B1Fig, B1K, 0}, P4 {B2Fig, B2K, 0};
            Constraint K; K.Type = ConstraintType::Angle; K.P1 = P1; K.P2 = P2; K.P3 = P3; K.P4 = P4; K.Prescribed = Rad; K.Active = true;
            CGraph.AddAnchor(P1, A1Fig, A1Slot, 0, 0);
            CGraph.AddAnchor(P2, A2Fig, A2Slot, 0, 0);
            CGraph.AddAnchor(P3, B1Fig, B1Slot, 0, 0);
            CGraph.AddAnchor(P4, B2Fig, B2Slot, 0, 0);
            uint32_t Id = CGraph.AddConstraint(K, "angle " + LA + " " + LB + " = " + std::to_string(Deg) + "°");
            Row("constraint #%u  angle %s %s = %g°  (added)", Id, LA.c_str(), LB.c_str(), Deg);
            return true;
        }
        if (Sub == "coincident" || Sub == "horizontal" || Sub == "vertical" || Sub == "parallel" || Sub == "perpendicular" || Sub == "equal")
        {
            if (!Need(C, 3, Sub.c_str())) return false;
            // For coincident / horizontal / vertical: two point refs.
            // For parallel / perpendicular / equal: two line refs.
            ConstraintType CT = ConstraintType::Coincident;
            if (Sub == "horizontal") CT = ConstraintType::Horizontal;
            if (Sub == "vertical")   CT = ConstraintType::Vertical;
            if (Sub == "parallel")   CT = ConstraintType::Parallel;
            if (Sub == "perpendicular") CT = ConstraintType::Perpendicular;
            if (Sub == "equal")      CT = ConstraintType::EqualLength;
            Constraint K; K.Type = CT; K.Active = true;
            std::string Note = Sub + " ";
            if (CT == ConstraintType::Coincident || CT == ConstraintType::Horizontal || CT == ConstraintType::Vertical)
            {
                PointRef A, B; std::string FA, FB; int SA, SB, IA, IB, CA, CB;
                if (!ParsePointRef(C.Arguments[1], A, FA, SA, IA, CA)) return Refuse("constraint %s: bad ref '%s'", Sub.c_str(), C.Arguments[1].c_str());
                if (!ParsePointRef(C.Arguments[2], B, FB, SB, IB, CB)) return Refuse("constraint %s: bad ref '%s'", Sub.c_str(), C.Arguments[2].c_str());
                K.P1 = A; K.P2 = B;
                CGraph.AddAnchor(A, FA, SA, IA, CA);
                CGraph.AddAnchor(B, FB, SB, IB, CB);
                Note += std::string(C.Arguments[1]) + " " + std::string(C.Arguments[2]);
            }
            else
            {
                std::string LA, LB; if (!ParseLineRef(C.Arguments[1], LA)) return Refuse("constraint %s: bad line ref '%s'", Sub.c_str(), C.Arguments[1].c_str());
                if (!ParseLineRef(C.Arguments[2], LB)) return Refuse("constraint %s: bad line ref '%s'", Sub.c_str(), C.Arguments[2].c_str());
                PointRef P1 {LA, PointRefKind::LineStart, 0}, P2 {LA, PointRefKind::LineEnd, 0};
                PointRef P3 {LB, PointRefKind::LineStart, 0}, P4 {LB, PointRefKind::LineEnd, 0};
                K.P1 = P1; K.P2 = P2; K.P3 = P3; K.P4 = P4;
                CGraph.AddAnchor(P1, LA, 0, 0, 0);
                CGraph.AddAnchor(P2, LA, 1, 0, 0);
                CGraph.AddAnchor(P3, LB, 0, 0, 0);
                CGraph.AddAnchor(P4, LB, 1, 0, 0);
                Note += std::string(C.Arguments[1]) + " " + std::string(C.Arguments[2]);
            }
            uint32_t Id = CGraph.AddConstraint(K, Note);
            Row("constraint #%u  %s  (added)", Id, Note.c_str());
            return true;
        }
        if (Sub == "equal-radius")
        {
            if (!Need(C, 3, "constraint equal-radius")) return false;
            // Two circle refs (figure names). We use the centre + radius point of each as the unknowns.
            //    The EqualRadius residual is "distance(centre, point) of A - distance(centre, point) of B = 0".
            std::string FA = C.Arguments[1], FB = C.Arguments[2];
            PointRef A1 {FA, PointRefKind::CircleCentre, 0};
            PointRef A2 {FA, PointRefKind::CircleRadiusPoint, 0};
            PointRef B1 {FB, PointRefKind::CircleCentre, 0};
            PointRef B2 {FB, PointRefKind::CircleRadiusPoint, 0};
            Constraint K; K.Type = ConstraintType::EqualRadius; K.P1 = A1; K.P2 = A2; K.P3 = B1; K.P4 = B2; K.Active = true;
            // Slot indices for circles: A slot 0 = centre (A), A slot 1 = radius point. For our use, we
            //    treat A and B as Blueprints.A and a second ref. We approximate by using A as centre and
            //    A + Normal*R0 as the radius point (this is the conventional way circles are stored).
            //    For now, we anchor (A1, A2) to slots 0 and 1, (B1, B2) to slots 0 and 1.
            CGraph.AddAnchor(A1, FA, 0, 0, 0);
            CGraph.AddAnchor(A2, FA, 1, 0, 0);
            CGraph.AddAnchor(B1, FB, 0, 0, 0);
            CGraph.AddAnchor(B2, FB, 1, 0, 0);
            uint32_t Id = CGraph.AddConstraint(K, "equal-radius " + FA + " " + FB);
            Row("constraint #%u  equal-radius %s %s  (added)", Id, FA.c_str(), FB.c_str());
            return true;
        }
        return Refuse("constraint: unknown sub-verb '%s'", Sub.c_str());
    });
    Add("angle", "angle <polyline> [--at=K]  — interior angle at vertex K of a polyline (K defaults to the middle; the included angle between segments K-1,K and K,K+1 is reported in degrees)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "angle")) return false;
        SceneFigure* F = Resolve(C.Arguments[0]); if (!F) return Refuse("angle: no figure '%s'", C.Arguments[0].c_str());
        if (F->Classification != FigureClassification::Curve) return Refuse("angle: '%s' is not a curve (angle dims are for polylines)", C.Arguments[0].c_str());
        int K = -1;
        if (auto SK = C.SwitchNumber("at")) K = int(*SK);
        const NurbsCurve& Crv = F->Curve;
        // For a polyline we know the vertex list directly; for freeform curves we sample at 1/3, 1/2, 2/3.
        //    K (0-based) selects the vertex: P1 = vertex K, P0 = vertex K-1, P2 = vertex K+1.
        Vec3 P0, P1, P2;
        if (Crv.Classification == CurveClassification::Polyline || Crv.Classification == CurveClassification::Line)
        {
            // Poles are homogeneous (wx, wy, wz, w); for a polyline the first N poles are the vertices.
            // We have to divide by W to get world coords.
            int N = Crv.PoleCount();
            if (N < 3) return Refuse("angle: polyline '%s' has only %d vertices; need at least 3", C.Arguments[0].c_str(), N);
            if (K < 0) K = N / 2;
            if (K < 1 || K > N - 2) return Refuse("angle --at=K: K must be in [1, %d] (got %d)", N - 2, K);
            Vec4 A = Crv.Poles[K - 1], B = Crv.Poles[K], C = Crv.Poles[K + 1];
            P0 = A.Divide(); P1 = B.Divide(); P2 = C.Divide();
        }
        else
        {
            P0 = Crv.Sample(0.40);
            P1 = Crv.Sample(0.50);
            P2 = Crv.Sample(0.60);
        }
        Vec3 V1 = (P0 - P1); Vec3 V2 = (P2 - P1);
        double Len1 = V1.Length(), Len2 = V2.Length();
        if (Len1 < 1e-9 || Len2 < 1e-9) return Refuse("angle: degenerate vertex");
        double Cos = V1.Dot(V2) / (Len1 * Len2);
        Cos = std::clamp(Cos, -1.0, 1.0);
        double Rad = std::acos(Cos);
        uint32_t NewId = EmitDimension(DimensionForm::Angle, F->Identity, F->Name + " angle", P1, P1, Plane.Normal(), Rad, false);
        Row("dim #%u  %s angle %.2f°", NewId, F->Name.c_str(), Rad * 180.0 / 3.14159265358979323846);
        return true;
    });
    Add("describe", "describe <figure> — poles and knots", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "describe")) return false;
        SceneFigure* Figure = Resolve(C.Arguments[0]); if (!Figure) return Refuse("no figure '%s'", C.Arguments[0].c_str());
        DescribeFigure(*Figure);
        if (Figure->Classification == FigureClassification::Curve)
        {
            const NurbsCurve& K = Figure->Curve;
            std::printf("    knots:"); for (double T : K.Knots) std::printf(" %.4g", T); std::printf("\n");
            for (int I = 0; I < K.PoleCount(); ++I) { Vec3 P = K.Poles[I].Divide(); std::printf("    pole %-3d (%9.4f %9.4f %9.4f)  w %.4f\n", I, P.X, P.Y, P.Z, K.Poles[I].W); }
        }
        else if (Figure->Classification == FigureClassification::Body) return Execute("topology " + C.Arguments[0]);
        else
        {
            const NurbsSurface& S = Figure->Surface;
            std::printf("    knotsU:"); for (double T : S.KnotsU) std::printf(" %.4g", T); std::printf("\n    knotsV:"); for (double T : S.KnotsV) std::printf(" %.4g", T); std::printf("\n");
            for (int I = 0; I < S.CountU; ++I) for (int J = 0; J < S.CountV; ++J) { Vec3 P = S.Pole(I, J).Divide(); std::printf("    pole %2d,%-2d (%9.4f %9.4f %9.4f)  w %.4f\n", I, J, P.X, P.Y, P.Z, S.Pole(I, J).W); }
        }
        return true;
    });
    Add("rename", "rename <figure> <newName>", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "rename")) return false;
        SceneFigure* I = Resolve(C.Arguments[0]); if (!I) return Refuse("no figure '%s'", C.Arguments[0].c_str());
        I->Name = Scene.UniqueName(C.Arguments[1]); DescribeFigure(*I); return true;
    });
    Add("move", "move <figure...> (dx,dy,dz)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "move")) return false;
        Vec3 D; if (!PointArg(C, C.Count() - 1, D, "move")) return false;
        CommandLine Sub = C; Sub.Arguments.pop_back();
        Mat4 M = Mat4::Translation(D);
        for (SceneFigure* I : ResolveMany(Sub, 0)) { I->Transform(M); DescribeFigure(*I); }
        return true;
    });

    //---------------------------------------------- workplane & view ----------------------------------------------
    Add("workplane", "workplane xy|xz|yz [--origin=(x,y,z)]  ·  workplane <name>  — recall a named plane (see `plane --name=…`)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "workplane")) return false;
        const std::string& N = C.Arguments[0];
        if (N == "xy")      Plane = Workplane::XY();
        else if (N == "xz") Plane = Workplane::XZ();
        else if (N == "yz") Plane = Workplane::YZ();
        else
        {
            // Named-plane recall: workplane <name> pops a plane previously saved with `plane --name=…`.
            auto It = NamedPlanes.find(N);
            if (It == NamedPlanes.end()) return Refuse("workplane: unknown plane '%s' (use xy, xz, yz, or a named plane saved with `plane --name=…`)", N.c_str());
            Plane = It->second;
            Row("workplane %s origin (%.3f %.3f %.3f) normal (%.3f %.3f %.3f)  [named]", N.c_str(), Plane.Origin.X, Plane.Origin.Y, Plane.Origin.Z, Plane.Normal().X, Plane.Normal().Y, Plane.Normal().Z);
            return true;
        }
        if (auto O = C.SwitchText("origin")) if (auto V = CommandCodec::ParsePoint(*O)) Plane.Origin = *V;
        Row("workplane %s origin (%.3f %.3f %.3f) normal (%.0f %.0f %.0f)", N.c_str(), Plane.Origin.X, Plane.Origin.Y, Plane.Origin.Z, Plane.Normal().X, Plane.Normal().Y, Plane.Normal().Z);
        return true;
    });
    Add("plane", "plane (origin) lengthU lengthV [--u=(x,y,z)] [--v=(x,y,z)] [--name=N]  ·  plane --from=<figure> --name=N — make a plane primitive (default), or save the current workplane as a named plane (with --name= alone), or save the plane implied by a figure's bounding face (with --from= and --name=).", [=, this](const CommandLine& C)
    {
        if (C.Switch("name") && !C.Switch("from") && C.Count() == 0)
        {
            // Just save the current workplane under a name.
            std::string N = *C.SwitchText("name");
            if (N.empty()) return Refuse("plane --name=: name is empty");
            NamedPlanes[N] = Plane;
            Row("plane %s saved  origin (%.3f %.3f %.3f) normal (%.3f %.3f %.3f)", N.c_str(), Plane.Origin.X, Plane.Origin.Y, Plane.Origin.Z, Plane.Normal().X, Plane.Normal().Y, Plane.Normal().Z);
            return true;
        }
        if (auto From = C.SwitchText("from"))
        {
            // Save a plane derived from a figure's natural face.
            SceneFigure* F = Resolve(*From);
            if (!F) return Refuse("plane --from=: no figure '%s'", From->c_str());
            std::string N = *C.SwitchText("name");
            if (N.empty()) return Refuse("plane --from=: --name= is required");
            Workplane P;
            if (F->Classification == FigureClassification::Surface) P = Workplane::FromNormal(F->Surface.Origin, F->Surface.Normal(0.5, 0.5));
            else if (F->Classification == FigureClassification::Body)
            {
                if (F->Body.Faces.empty()) return Refuse("plane --from=: body has no faces");
                const auto& Face = F->Body.Faces.front();
                Vec3 Nv = Face.Surface.Normal(0.5, 0.5);
                if (Face.Reversed) Nv = -Nv;
                P = Workplane::FromNormal(Face.Surface.Origin, Nv);
            }
            else return Refuse("plane --from=: figure '%s' must be a surface or body (not a curve)", From->c_str());
            NamedPlanes[N] = P;
            Row("plane %s saved from %s  origin (%.3f %.3f %.3f) normal (%.3f %.3f %.3f)", N.c_str(), From->c_str(), P.Origin.X, P.Origin.Y, P.Origin.Z, P.Normal().X, P.Normal().Y, P.Normal().Z);
            return true;
        }
        // Default: build a plane primitive (the original behaviour).
        Vec3 O; double LU = 0, LV = 0; if (!Need(C, 3, "plane") || !PointArg(C, 0, O, "plane") || !NumberArg(C, 1, LU, "plane") || !NumberArg(C, 2, LV, "plane")) return false;
        Vec3 U = Plane.AxisX, V = Plane.AxisY;
        if (auto A = C.SwitchText("u")) if (auto W = CommandCodec::ParsePoint(*A)) U = *W;
        if (auto A = C.SwitchText("v")) if (auto W = CommandCodec::ParsePoint(*A)) V = *W;
        return AddSurface(C, "Plane", NurbsSurface::Plane(O, U, V, LU, LV));
    });
    Add("view", "view front|back|right|left|top|bottom|iso|persp|ortho  ·  view orbit yawDeg pitchDeg  ·  view fit [selected]  ·  view dolly steps", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "view")) return false;
        const std::string& N = C.Arguments[0];
        double Aspect = double(Surface->Width()) / Surface->Height();
        if (N == "front") View.Look(CanonicalView::Front);
        else if (N == "back") View.Look(CanonicalView::Back);
        else if (N == "right") View.Look(CanonicalView::Right);
        else if (N == "left") View.Look(CanonicalView::Left);
        else if (N == "top") View.Look(CanonicalView::Top);
        else if (N == "bottom") View.Look(CanonicalView::Bottom);
        else if (N == "iso") View.Look(CanonicalView::Isometric);
        else if (N == "persp") View.Orthographic = false;
        else if (N == "toggle") View.Orthographic = !View.Orthographic;
        else if (N == "ortho") View.Orthographic = true;
        else if (N == "orbit") { double Y = 0, P = 0; if (!NumberArg(C, 1, Y, "view") || !NumberArg(C, 2, P, "view")) return false; View.Orbit(ScalarCriteria::Radians(Y), ScalarCriteria::Radians(P)); }
        else if (N == "dolly") { double S = 0; if (!NumberArg(C, 1, S, "view")) return false; View.Dolly(S); }
        else if (N == "fit")
        {
            Box3 B = Scene.Bounds(C.Count() > 1 && C.Arguments[1] == "selected");
            if (B.Empty()) B.Include({ -5, -5, 0 }), B.Include({ 5, 5, 0 });
            View.Fit(B.Inflated(B.Diagonal() * 0.05), Aspect);
        }
        else return Refuse("view: unknown mode '%s'", N.c_str());
        Vec3 E = View.Eye();
        Row("view %s  eye (%.2f %.2f %.2f)  pivot (%.2f %.2f %.2f)  distance %.2f  yaw %.1f° pitch %.1f°  %s",
            N.c_str(), E.X, E.Y, E.Z, View.Pivot.X, View.Pivot.Y, View.Pivot.Z, View.Distance, ScalarCriteria::Degrees(View.Yaw), ScalarCriteria::Degrees(View.Pitch), View.Orthographic ? "ortho" : "persp");
        return true;
    });
    Add("matcap", "matcap <figure...> <name|index>  ·  matcap list — per-figure studio (steel chrome gold copper plastic-white plastic-red plastic-blue clay pearl carbon)", [=, this](const CommandLine& C)
    {
        if (C.Count() == 1 && C.Arguments[0] == "list") { for (int I = 0; I < MatcapCount(); ++I) Row("%d  %s", I, MatcapName(uint8_t(I))); return true; }
        if (C.Count() < 2) return Refuse("matcap: figure and a studio name required");
        const std::string& Name = C.Arguments.back();
        int Layer = -1;
        for (int I = 0; I < MatcapCount(); ++I) if (Name == MatcapName(uint8_t(I))) Layer = I;
        if (Layer < 0) if (auto N = CommandCodec::ParseNumber(Name)) Layer = int(*N);
        if (Layer < 0 || Layer >= MatcapCount()) return Refuse("matcap: unknown studio '%s' (try matcap list)", Name.c_str());
        CommandLine Sub = C; Sub.Arguments.pop_back();
        for (SceneFigure* I : ResolveMany(Sub, 0)) { I->Matcap = uint8_t(Layer); Row("#%u %s → %s", I->Identity, I->Name.c_str(), MatcapName(uint8_t(Layer))); }
        return true;
    });
    Add("tint", "tint <figure...> r g b — body colour 0..1", [=, this](const CommandLine& C)
    {
        if (C.Count() < 4) return Refuse("tint: figure and r g b required");
        double R = C.Number(C.Count() - 3).value_or(-1), G = C.Number(C.Count() - 2).value_or(-1), B = C.Number(C.Count() - 1).value_or(-1);
        if (R < 0 || G < 0 || B < 0) return Refuse("tint: r g b must be numbers 0..1");
        CommandLine Sub = C; Sub.Arguments.resize(C.Count() - 3);
        for (SceneFigure* I : ResolveMany(Sub, 0)) { I->Tint[0] = float(R); I->Tint[1] = float(G); I->Tint[2] = float(B); }
        return true;
    });
    Add("gizmo", "gizmo on|off  ·  gizmo combined|translate|rotate|scale  ·  gizmo size px  ·  gizmo status  ·  gizmo grips", [=, this](const CommandLine& C)
    {
        if (C.Count() < 1) return Refuse("gizmo: argument required");
        const std::string& A = C.Arguments[0];
        if (A == "on") GizmoShown = true; else if (A == "off") GizmoShown = false;
        else if (A == "combined") GizmoRig.Arrange(GizmoLayout::Combined); else if (A == "translate") GizmoRig.Arrange(GizmoLayout::Translate);
        else if (A == "rotate") GizmoRig.Arrange(GizmoLayout::Rotate); else if (A == "scale") GizmoRig.Arrange(GizmoLayout::Scale);
        else if (A == "size") { double S; if (!C.Number(1) || (S = *C.Number(1)) < 10) return Refuse("gizmo size: pixels ≥ 10 required"); GizmoRig.Resize(S); }
        else if (A == "grips")
        {
            RefreshGizmoPivot();
            for (int I = int(GizmoGrip::TranslateX); I <= int(GizmoGrip::RotateZ); ++I)
            {
                GizmoGrip H = static_cast<GizmoGrip>(I);
                Vec3 W = GizmoRig.GripAnchor(H, View, Surface->Height());
                double X = 0, Y = 0; bool On = View.WorldToPixel(W, Surface->Width(), Surface->Height(), X, Y);
                GizmoRig.AimAt(View);
                GizmoGrip Locate = On ? GizmoRig.Locate(X, Y, View, Surface->Width(), Surface->Height()) : GizmoGrip::None;
                if (!GizmoRig.Visible(H)) { Row("%-13s hidden (orthographic view along %c)", GizmoGripName(H), "XYZ"[std::clamp(GizmoRig.ViewAxis(), 0, 2)]); continue; }
                Row("%-13s pixel (%4d,%4d)  world (%.3f %.3f %.3f)  inspect → %s", GizmoGripName(H), int(X), int(Y), W.X, W.Y, W.Z, GizmoGripName(Locate));
            }
            return true;
        }
        else if (A != "status") return Refuse("gizmo: on|off|combined|translate|rotate|scale|size|status|grips");
        RefreshGizmoPivot();
        const char* Layouts[] = { "combined", "translate", "rotate", "scale" };
        Vec3 O = GizmoRig.CurrentPivot().Origin;
        const char* Aim[] = { "free", "along X (only YZ-plane move + X rotate)", "along Y (only XZ-plane move + Y rotate)", "along Z (only XY-plane move + Z rotate)" };
        Row("gizmo %s  layout %s  pivot (%.3f %.3f %.3f)  view %s  hover %s%s", GizmoShown ? "on" : "off", Layouts[int(GizmoRig.CurrentLayout())], O.X, O.Y, O.Z,
            Aim[GizmoRig.ViewAxis() + 1], GizmoGripName(GizmoRig.Hovered()), GizmoRig.Dragging() ? "  [dragging]" : "");
        return true;
    });
    RegisterSelection();
    Add("show", "show cages on|off  ·  show iso on|off  ·  show shading flat|plastic|matcap", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "show")) return false;
        bool On = C.Arguments[1] == "on";
        if (C.Arguments[0] == "cages") ShowControlCages = On; else if (C.Arguments[0] == "iso") ShowIsoCurves = On;
        else if (C.Arguments[0] == "shading")
        {
            const std::string& M = C.Arguments[1];
            if (M == "flat") Shading = SurfaceShading::Flat; else if (M == "plastic") Shading = SurfaceShading::Plastic; else if (M == "matcap") Shading = SurfaceShading::Matcap;
            else return Refuse("show shading: flat|plastic|matcap");
            Row("shading %s", M.c_str());
        }
        else return Refuse("show: cages|iso|shading");
        return true;
    });
    Add("render", "render <name> [--size=WxH] — writes Proofs/<name>.png  ·  render sheet <0|1|2|3> captures a tile; render sheet finalize <name> writes the 2x2 contact sheet", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "render")) return false;
        // Phase 10: contact-sheet sub-verb. `render sheet N` captures tile N (0..3) from the live raster, `render sheet
        //    finalize <name>` writes a 2x2 composite. See docs/CONTACT_SHEET.md.
        if (C.Arguments[0] == "sheet")
        {
            if (C.Count() < 2) return Refuse("render sheet: tile index 0..3 or 'finalize <name>' required");
            const std::string& Mode = C.Arguments[1];
            if (Mode == "finalize")
            {
                if (!Need(C, 3, "render sheet finalize")) return false;
                const std::string& Name = C.Arguments[2];
                uint32_t W = 0, H = 0;
                for (const Tile& T : SheetTiles) if (T.Captured) { W = std::max(W, T.W); H = std::max(H, T.H); }
                if (W == 0 || H == 0) return Refuse("render sheet finalize: no tiles captured (call `render sheet 0..3` first)");
                RasterImage Out; Out.Width = 2 * W; Out.Height = 2 * H; Out.Pixels.assign(size_t(Out.Width) * Out.Height * 4, uint8_t(0));
                for (size_t I = 0; I + 3 < Out.Pixels.size(); I += 4) { Out.Pixels[I + 0] = uint8_t(Backdrop[0] * 255); Out.Pixels[I + 1] = uint8_t(Backdrop[1] * 255); Out.Pixels[I + 2] = uint8_t(Backdrop[2] * 255); Out.Pixels[I + 3] = 255; }
                auto Stamp = [&](const Tile& T, uint32_t Ox, uint32_t Oy)
                {
                    if (!T.Captured) return;
                    for (uint32_t Y = 0; Y < T.H; ++Y) for (uint32_t X = 0; X < T.W; ++X)
                    {
                        const uint8_t* Src = T.Pixels.data() + (size_t(Y) * T.W + X) * 4;
                        uint8_t* Dst = Out.Pixels.data() + (size_t(Oy + Y) * Out.Width + Ox + X) * 4;
                        Dst[0] = Src[0]; Dst[1] = Src[1]; Dst[2] = Src[2]; Dst[3] = Src[3];
                    }
                    if (Ox > 0) for (uint32_t Y = 0; Y < T.H; ++Y) { uint8_t* D = Out.Pixels.data() + (size_t(Oy + Y) * Out.Width + Ox) * 4; D[0] = D[1] = D[2] = 12; }
                    if (Oy > 0) for (uint32_t X = 0; X < T.W; ++X) { uint8_t* D = Out.Pixels.data() + (size_t(Oy) * Out.Width + Ox + X) * 4; D[0] = D[1] = D[2] = 12; }
                };
                Stamp(SheetTiles[0], 0, 0); Stamp(SheetTiles[1], W, 0); Stamp(SheetTiles[2], 0, H); Stamp(SheetTiles[3], W, H);
                std::filesystem::create_directories(Proofs);
                std::string Path = (std::filesystem::path(Proofs) / (Name + ".png")).string();
                if (!WritePng(Path, Out)) return Refuse("render sheet finalize: cannot write %s", Path.c_str());
                for (Tile& T : SheetTiles) T = Tile();
                Row("render sheet  %s  %ux%u  (2x2 of %ux%u tiles)", Path.c_str(), Out.Width, Out.Height, W, H);
                return true;
            }
            int Index = std::atoi(Mode.c_str());
            if (Index < 0 || Index > 3) return Refuse("render sheet: tile index must be 0, 1, 2 or 3 (got '%s')", Mode.c_str());
            Render();
            Tile& T = SheetTiles[Index];
            T.Captured = true; T.W = Surface->Width(); T.H = Surface->Height();
            T.Pixels.assign((size_t)T.W * T.H * 4, 0);
            RasterImage Img = Surface->Readback();
            if (Img.Pixels.size() == T.Pixels.size()) T.Pixels = std::move(Img.Pixels);
            Row("render sheet %d  %ux%u  (tile captured; run `render sheet finalize <name>` to composite)", Index, T.W, T.H);
            return true;
        }
        if (auto S = C.SwitchText("size"))
        {
            size_t X = S->find('x');
            if (X != std::string::npos) Surface->Resize(uint32_t(std::atoi(S->substr(0, X).c_str())), uint32_t(std::atoi(S->substr(X + 1).c_str())));
        }
        auto T0 = std::chrono::steady_clock::now();
        Render();
        double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
        std::filesystem::create_directories(Proofs);
        std::string Path = (std::filesystem::path(Proofs) / (C.Arguments[0] + ".png")).string();
        if (!WritePng(Path, Surface->Readback())) return Refuse("render: cannot write %s", Path.c_str());
        RasterExchange::Tally T = Surface->QueryTally();
        Row("render %s  %ux%u  %.1f ms  %u tri  %u seg  %u pts  %u frag", Path.c_str(), Surface->Width(), Surface->Height(), Ms, T.Triangles, T.Segments, T.Points, T.Fragments);
        return true;
    });
    Add("pick", "pick x y — identity (and pole) under a pixel of the last render", [=, this](const CommandLine& C)
    {
        double X = 0, Y = 0; if (!Need(C, 2, "pick") || !NumberArg(C, 0, X, "pick") || !NumberArg(C, 1, Y, "pick")) return false;
        uint32_t Pick = Surface->Pick(uint32_t(X), uint32_t(Y));
        uint32_t Id = SceneDocument::IdentityOf(Pick); int Pole = SceneDocument::PoleOf(Pick);
        SceneFigure* Figure = Scene.Find(Id);
        if (Pole >= 0) Row("pixel (%d,%d) → #%u pole %d  depth %.5f", int(X), int(Y), Id, Pole, Surface->Depth(uint32_t(X), uint32_t(Y)));
        else Row("pixel (%d,%d) → %s%s  depth %.5f", int(X), int(Y), Id ? "#" : "nothing", Id ? std::to_string(Id).c_str() : "", Surface->Depth(uint32_t(X), uint32_t(Y)));
        if (Figure) DescribeFigure(*Figure);
        return true;
    });
    Add("echo", "echo text", [=, this](const CommandLine& C) { std::printf("  "); for (const auto& A : C.Arguments) std::printf("%s ", A.c_str()); std::printf("\n"); return true; });
    Add("help", "help [verb]", [=, this](const CommandLine& C)
    {
        if (C.Count() == 1) { auto It = Usage.find(C.Arguments[0]); if (It == Usage.end()) return Refuse("no command '%s'", C.Arguments[0].c_str()); Row("%s", It->second.c_str()); return true; }
        for (const auto& [Verb, Help] : Usage) std::printf("  %-10s %s\n", Verb.c_str(), Help.c_str());
        return true;
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EXECUTION
//------------------------------------------------------------------------------------------------------------------------

bool ConsoleHost::Execute(std::string_view Line) noexcept
{
    std::vector<CommandLine> Batch; std::string Error;
    if (!CommandCodec::Decode(Line, Batch, Error)) return Refuse("syntax: %s", Error.c_str());
    bool Ok = true;
    for (const CommandLine& C : Batch)
    {
        auto It = Commands.find(C.Verb);
        if (It == Commands.end()) { Ok = Refuse("unknown command '%s' (try help)", C.Verb.c_str()); continue; }
        std::printf("> %s", C.Verb.c_str());
        for (const auto& A : C.Arguments) std::printf(" %s", A.c_str());
        for (const auto& F : C.Flags) std::printf(" --%s%s%s", F.first.c_str(), F.second.empty() ? "" : "=", F.second.c_str());
        std::printf("\n");
        // One record entry per command — except a gizmo drag, which is recorded once from the grab to the release.
        const bool Stepper = C.Verb == "undo" || C.Verb == "redo" || C.Verb == "timeline";
        const bool Record = !Recording && !GizmoRig.Dragging() && !Stepper;
        std::string Label = C.Verb; for (const auto& A : C.Arguments) Label += " " + A;
        if (Record) { Recording = true; Undo.Record(Scene, Label); }
        if (Stepper && Recording) Undo.Abandon();                                    // `key ctrl+z` → the wrapper must not record the step
        const bool Done = It->second(C);
        Scene.RebuildAreas(Plane);
        for (const std::string& N : Scene.Regenerate(Plane)) Row("  ↻ %s rebuilt from its sources", N.c_str());
        if (Record)
        {
            Recording = false;
            if (GizmoRig.Dragging()) Undo.Relabel("gizmo drag " + std::string(GizmoGripName(GizmoRig.Drag().Grip)));   // keep pending until release
            else Undo.Settle(Scene);
        }
        if (!Done) Ok = false;
        else if (C.Verb != "repeat" && C.Verb != "render" && C.Verb != "list" && C.Verb != "hud" && C.Verb != "help")
        {
            LastCommand = C.Verb;
            for (const auto& A : C.Arguments) LastCommand += " " + A;
            for (const auto& F : C.Flags) LastCommand += " --" + F.first + (F.second.empty() ? "" : "=" + F.second);
        }
    }
    return Ok;
}

bool ConsoleHost::RunScript(const std::string& Path, bool ContinueOnRefusal) noexcept
{
    std::ifstream In(Path);
    if (!In) return Refuse("cannot open script %s", Path.c_str());
    std::printf("── script %s\n", Path.c_str());
    std::string Line; LineNumber = 0; bool Ok = true;
    while (std::getline(In, Line))
    {
        ++LineNumber;
        if (!Execute(Line))
        {
            std::printf("  (line %d)\n", LineNumber);
            Ok = false;
            if (!ContinueOnRefusal) break;
        }
    }
    std::printf("── %s: %d line(s), %d refusal(s)\n", Path.c_str(), LineNumber, Refusals);
    return Ok;
}

int ConsoleHost::RunInteractive(std::FILE* In) noexcept
{
    std::printf("SolidArc console — type help, quit to exit\n");
    char Line[4096];
    while (std::printf("solidarc> "), std::fflush(stdout), std::fgets(Line, sizeof Line, In))
    {
        std::string_view S(Line);
        while (!S.empty() && (S.back() == '\n' || S.back() == '\r')) S.remove_suffix(1);
        if (S == "quit" || S == "exit") break;
        Execute(S);
    }
    return Refusals == 0 ? 0 : 1;
}

} // namespace Frontier
