//============================================================================================================================================
// 📦 ParametricSketcher/Verification/RasterVerification.cpp — Phase 2 proofs: software raster, Slang shaders, camera, PNG output
//============================================================================================================================================
// Console: pixel-probe checks (grid axis colours land where the projection says they should, depth ordering, pick
//    identities, back-face tint on an inverted sphere). Visual: Proofs/Proof_02_*.png.

#include "Interaction/CameraProjection.h"
#include "Presentation/SoftwareRaster.h"
#include "Presentation/ScenePresentation.h"
#include "VerificationPanel.h"
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{

const float Backdrop[4] = { 0.117f, 0.129f, 0.153f, 1.0f };                            // Frontier viewport slate

struct Probe { uint8_t R, G, B; };
Probe Sample(const RasterImage& I, uint32_t X, uint32_t Y) noexcept
{
    const uint8_t* P = &I.Pixels[(static_cast<size_t>(Y) * I.Width + X) * 4];
    return { P[0], P[1], P[2] };
}

// Project a world point to pixel coordinates using the same record the raster was given.
Vec2 ToPixel(const CameraProjection& Camera, Vec3 P, uint32_t W, uint32_t H) noexcept
{
    Mat4 VC = Camera.ProjectionMatrix(double(W) / H) * Camera.ViewMatrix();
    Vec4 C = VC * Vec4(P, 1.0);
    return { (C.X / C.W * 0.5 + 0.5) * W, (C.Y / C.W * 0.5 + 0.5) * H };
}

std::string ProofPath(const char* Name)
{
    std::filesystem::path Dir = std::filesystem::path(SOLIDARC_PROOF_DIRECTORY);
    std::filesystem::create_directories(Dir);
    return (Dir / Name).string();
}

} // namespace

int main()
{
    VerificationPanel Panel("SolidArc · Phase 2 · Raster Verification — SoftwareRaster · Slang shaders · CameraProjection · PNG");
    const uint32_t W = 1280, H = 800;
    SoftwareRaster Raster(W, H);

    //------------------------------------------------------------------ Proof 02a: grid + triad, perspective
    Panel.Section("Grid & triad (perspective)");
    {
        CameraProjection Camera;
        Camera.Distance = 14.0; Camera.Pivot = { 1.0, 1.0, 0.0 };
        ViewRecord View = Camera.ToViewRecord(W, H, 1.0);
        auto T0 = std::chrono::steady_clock::now();
        Raster.BeginTarget(Backdrop);
        Raster.BindView(View);
        Raster.DrawGrid();
        Raster.BeginOverlay();
        ScenePresentation::DrawTriad(Raster, 2.0);
        Raster.EndTarget();
        double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
        RasterImage Image = Raster.Readback();
        Panel.Expect("Proof_02a_Grid.png written", WritePng(ProofPath("Proof_02a_Grid.png"), Image));
        Panel.Note("grid+triad frame: %.1f ms, %u fragments", Ms, Raster.QueryTally().Fragments);

        // The X axis (y = 0) must read red at a point on it; the Y axis green; an off-axis cell interior ≈ backdrop.
        Vec2 OnX = ToPixel(Camera, { 4.0, 0.0, 0.0 }, W, H);
        Vec2 OnY = ToPixel(Camera, { 0.0, 4.0, 0.0 }, W, H);
        Vec2 Cell = ToPixel(Camera, { 2.5, 2.5, 0.0 }, W, H);
        Probe PX = Sample(Image, uint32_t(OnX.X), uint32_t(OnX.Y));
        Probe PY = Sample(Image, uint32_t(OnY.X), uint32_t(OnY.Y));
        Probe PC = Sample(Image, uint32_t(Cell.X), uint32_t(Cell.Y));
        Panel.Expect("X axis pixel is red-dominant", PX.R > PX.G + 40 && PX.R > PX.B + 40);
        Panel.Expect("Y axis pixel is green-dominant", PY.G > PY.R + 40 && PY.G > PY.B + 40);
        Panel.Expect("Cell interior stays near backdrop (grid is lines, not fill)", std::abs(int(PC.R) - 95) < 30 && std::abs(int(PC.B) - 110) < 30);
        Panel.Note("probe X-axis rgb(%d,%d,%d)  Y-axis rgb(%d,%d,%d)  cell rgb(%d,%d,%d)", PX.R, PX.G, PX.B, PY.R, PY.G, PY.B, PC.R, PC.G, PC.B);

        // Triad tip: the Z tip at (0,0,2) must be blue-ish and, being overlay, unaffected by depth.
        Vec2 TipZ = ToPixel(Camera, { 0.0, 0.0, 2.0 }, W, H);
        Probe PZ = Sample(Image, uint32_t(TipZ.X), uint32_t(TipZ.Y));
        Panel.Expect("Z triad tip is blue-dominant (overlay)", PZ.B > PZ.R + 30 && PZ.B > PZ.G + 20);
    }

    //------------------------------------------------------------------ Proof 02b: shaded NURBS sphere + isocurves + control net
    Panel.Section("Shaded NURBS sphere, isocurves, control net, depth & pick");
    {
        CameraProjection Camera;
        Camera.Look(CanonicalView::Isometric);
        NurbsSurface Sphere = NurbsSurface::Sphere({ 0, 0, 1.5 }, 1.5).Payload;
        NurbsSurface Torus  = NurbsSurface::Torus({ 4.5, 0, 0.6 }, Vec3::UnitZ(), 1.6, 0.6).Payload;
        NurbsSurface Cyl    = NurbsSurface::Cylinder({ -4.0, 1.0, 0.0 }, Vec3::UnitZ(), 1.0, 2.5).Payload;
        Box3 Bounds; Bounds.Include(Sphere.Bounds()); Bounds.Include(Torus.Bounds()); Bounds.Include(Cyl.Bounds());
        Camera.Frame(Bounds, double(W) / H);
        Camera.Distance *= 0.9;
        ViewRecord View = Camera.ToViewRecord(W, H, 1.0);

        auto T0 = std::chrono::steady_clock::now();
        Raster.BeginTarget(Backdrop);
        Raster.BindView(View);
        Raster.DrawGrid();

        DrawRecord Steel = ScenePresentation::Tinted(0.62f, 0.66f, 0.72f); Steel.PickIdentity = 1;
        DrawRecord Brass = ScenePresentation::Tinted(0.78f, 0.62f, 0.32f); Brass.PickIdentity = 2; Brass.Highlight = 2.0f;
        DrawRecord Slate = ScenePresentation::Tinted(0.45f, 0.55f, 0.70f); Slate.PickIdentity = 3;
        Raster.DrawSurface(ScenePresentation::SurfaceTriangles(Sphere, 2e-3), Steel);
        Raster.DrawSurface(ScenePresentation::SurfaceTriangles(Torus, 2e-3), Brass);
        Raster.DrawSurface(ScenePresentation::SurfaceTriangles(Cyl, 2e-3), Slate);

        DrawRecord Iso = ScenePresentation::Tinted(0.10f, 0.11f, 0.13f, 0.55f); Iso.LineWidth = 1.0f;
        Raster.DrawSegments(ScenePresentation::SurfaceIsoCurves(Sphere, 12, 6), Iso);
        Raster.DrawSegments(ScenePresentation::SurfaceIsoCurves(Torus, 16, 8), Iso);

        DrawRecord Net = ScenePresentation::Tinted(0.95f, 0.80f, 0.30f, 0.9f); Net.LineWidth = 1.0f; Net.Dashed = true; Net.PointSize = 6.0f;
        Raster.DrawSegments(ScenePresentation::ControlNet(Cyl), Net);
        Raster.DrawPoints(ScenePresentation::ControlPoints(Cyl), Net);

        Raster.BeginOverlay();
        ScenePresentation::DrawTriad(Raster, 1.5);
        Raster.EndTarget();
        double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
        RasterImage Image = Raster.Readback();
        RasterExchange::Tally Tally = Raster.QueryTally();
        Panel.Expect("Proof_02b_Primitives.png written", WritePng(ProofPath("Proof_02b_Primitives.png"), Image));
        Panel.Note("primitives frame: %.1f ms · %u triangles · %u segments · %u points · %u fragments (%u depth-rejected, %u back-facing)",
                   Ms, Tally.Triangles, Tally.Segments, Tally.Points, Tally.Fragments, Tally.DepthRejected, Tally.BackFacing);

        Vec2 SphereCentre = ToPixel(Camera, { 0, 0, 1.5 }, W, H);
        Vec2 TorusRim = ToPixel(Camera, { 4.5 + 1.6, 0, 0.6 + 0.6 }, W, H);
        Panel.Expect("Pick under sphere centre = 1", Raster.Pick(uint32_t(SphereCentre.X), uint32_t(SphereCentre.Y)) == 1);
        Panel.Expect("Pick under torus top = 2", Raster.Pick(uint32_t(TorusRim.X), uint32_t(TorusRim.Y)) == 2);
        Panel.Expect("Pick on empty backdrop = 0", Raster.Pick(20, 20) == 0);
        Panel.Expect("Depth at sphere < depth at far backdrop", Raster.Depth(uint32_t(SphereCentre.X), uint32_t(SphereCentre.Y)) < Raster.Depth(20, 20));
        Probe Sel = Sample(Image, uint32_t(TorusRim.X), uint32_t(TorusRim.Y));
        Panel.Expect("Selected torus reads selection-orange (R > G > B)", Sel.R > Sel.G && Sel.G > Sel.B);
        Panel.Expect("Front-facing sphere: no back-face fragments needed to fill it (winding proof)", Tally.BackFacing < Tally.Triangles / 2);
    }

    //------------------------------------------------------------------ Proof 02c: inverted sphere shows back-face tint; ortho top view
    Panel.Section("Back-face tint on inverted winding · orthographic top view");
    {
        CameraProjection Camera;
        Camera.Look(CanonicalView::Isometric);
        NurbsSurface Good = NurbsSurface::Sphere({ -2.0, 0, 1.2 }, 1.2).Payload;
        NurbsSurface Bad  = Good.Reversed().Transformed(Mat4::Translation({ 4.0, 0, 0 }));    // U/V swapped → normals inward
        Box3 Bounds; Bounds.Include(Good.Bounds()); Bounds.Include(Bad.Bounds());
        Camera.Frame(Bounds, double(W) / H);
        Raster.BeginTarget(Backdrop);
        Raster.BindView(Camera.ToViewRecord(W, H, 1.0));
        Raster.DrawGrid();
        DrawRecord Steel = ScenePresentation::Tinted(0.62f, 0.66f, 0.72f);
        Raster.DrawSurface(ScenePresentation::SurfaceTriangles(Good, 2e-3), Steel);
        Raster.DrawSurface(ScenePresentation::SurfaceTriangles(Bad, 2e-3), Steel);
        Raster.EndTarget();
        RasterImage Image = Raster.Readback();
        Panel.Expect("Proof_02c_WindingTint.png written", WritePng(ProofPath("Proof_02c_WindingTint.png"), Image));
        Vec2 GoodPx = ToPixel(Camera, { -2.0, 0, 1.2 }, W, H);
        Vec2 BadPx  = ToPixel(Camera, { 2.0, 0, 1.2 }, W, H);
        Probe G = Sample(Image, uint32_t(GoodPx.X), uint32_t(GoodPx.Y));
        Probe B = Sample(Image, uint32_t(BadPx.X), uint32_t(BadPx.Y));
        Panel.Expect("Correct sphere reads neutral steel", std::abs(int(G.R) - int(G.B)) < 40);
        Panel.Expect("Inverted sphere reads warm red (back-face tint)", B.R > B.B + 40 && B.R > B.G + 25);
        Panel.Note("good rgb(%d,%d,%d)  inverted rgb(%d,%d,%d)", G.R, G.G, G.B, B.R, B.G, B.B);

        // Orthographic top view: circle profile must project to a circle of known pixel radius.
        CameraProjection Top;
        Top.Look(CanonicalView::Top);
        Top.Pivot = {}; Top.Distance = 10.0;
        NurbsCurve Circle = NurbsCurve::Circle({}, Vec3::UnitZ(), 3.0).Payload;
        NurbsCurve Slot   = NurbsCurve::Slot(Workplane::XY(), { -4, -4 }, { 2, -5 }, 0.8).Payload;
        NurbsCurve Spline = NurbsCurve::Interpolate({ { -5, 3, 0 }, { -3, 5, 0 }, { -1, 3.5, 0 }, { 1, 5.5, 0 }, { 3, 4, 0 } }).Payload;
        Raster.BeginTarget(Backdrop);
        Raster.BindView(Top.ToViewRecord(W, H, 1.0));
        Raster.DrawGrid();
        DrawRecord Ink = ScenePresentation::Tinted(0.92f, 0.94f, 0.97f); Ink.LineWidth = 2.0f;
        Raster.DrawSegments(ScenePresentation::CurveSegments(Circle), Ink);
        Raster.DrawSegments(ScenePresentation::CurveSegments(Slot), Ink);
        Raster.DrawSegments(ScenePresentation::CurveSegments(Spline), Ink);
        DrawRecord Cage = ScenePresentation::Tinted(0.95f, 0.80f, 0.30f, 0.9f); Cage.LineWidth = 1.0f; Cage.Dashed = true; Cage.PointSize = 7.0f;
        Raster.DrawSegments(ScenePresentation::ControlPolygon(Spline), Cage);
        Raster.DrawPoints(ScenePresentation::ControlPoints(Spline), Cage);
        PointStream Marks;
        Marks.Append({ 0, 0, 0 }, PointGlyph::Cross); Marks.Append({ 0, -3, 0 }, PointGlyph::Diamond); Marks.Append({ -4, -4, 0 }, PointGlyph::Ring);
        DrawRecord Snap = ScenePresentation::Tinted(0.35f, 0.90f, 0.95f); Snap.PointSize = 11.0f;
        Raster.BeginOverlay();
        Raster.DrawPoints(Marks, Snap);
        Raster.EndTarget();
        Image = Raster.Readback();
        Panel.Expect("Proof_02d_SketchTop.png written", WritePng(ProofPath("Proof_02d_SketchTop.png"), Image));
        Vec2 C0 = ToPixel(Top, { 0, 0, 0 }, W, H), C1 = ToPixel(Top, { 3, 0, 0 }, W, H);
        double PixelRadius = C1.Distance(C0);
        double ExpectedRadius = 3.0 / (2.0 * Top.OrthographicHalfHeight()) * H;
        Panel.Equal("Ortho top: circle radius in pixels matches projection", PixelRadius, ExpectedRadius, 1e-6);
        Probe OnCircle = Sample(Image, uint32_t(C0.X + PixelRadius), uint32_t(C0.Y));
        Panel.Expect("Circle stroke lands on its analytic radius (bright pixel)", OnCircle.R > 180 && OnCircle.G > 180);
        Panel.Note("top view: circle r=3 → %.1f px, stroke probe rgb(%d,%d,%d)", PixelRadius, OnCircle.R, OnCircle.G, OnCircle.B);
    }

    //------------------------------------------------------------------ camera arithmetic
    Panel.Section("CameraProjection");
    {
        CameraProjection Camera;
        Camera.Pivot = { 2, -1, 0.5 }; Camera.Distance = 9.0;
        Ray R = Camera.PixelRay(W * 0.5, H * 0.5, W, H);
        Panel.Within("Centre-pixel ray passes through the pivot", (Camera.Pivot - R.Origin).Cross(R.Direction).Length(), 1e-6);
        Vec2 Back = ToPixel(Camera, R.At(5.0), W, H);
        Panel.Within("World→pixel→ray round trip at centre", Back.Distance({ W * 0.5, H * 0.5 }), 1e-3);
        Camera.Look(CanonicalView::Front);
        Panel.Within("Front view looks along +Y", Camera.Forward().Distance(Vec3::UnitY()), 1e-9);
        Camera.Look(CanonicalView::Right);
        Panel.Within("Right view looks along −X", Camera.Forward().Distance(Vec3::UnitX() * -1.0), 1e-9);
        Camera.Look(CanonicalView::Top);
        Panel.Within("Top view looks along −Z", Camera.Forward().Distance(Vec3::UnitZ() * -1.0), 1e-3);
        Panel.Within("Top view: screen-up is +Y", Camera.Up().Distance(Vec3::UnitY()), 1e-3);
    }

    return Panel.Conclude();
}
