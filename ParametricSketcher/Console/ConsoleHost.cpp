//============================================================================================================================================
// 📦 ParametricSketcher/Console/ConsoleHost.cpp — Command set: sketch curves, primitive surfaces, extrude/revolve/loft, scene, view, render
//============================================================================================================================================

#include "ConsoleHost.h"
#include "Presentation/ScenePresentation.h"
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>

namespace Frontier
{

namespace
{
const float Backdrop[4] = { 0.117f, 0.129f, 0.153f, 1.0f };
const char* KindName(ItemKind K) noexcept { return K == ItemKind::Curve ? "curve" : "surface"; }
}

ConsoleHost::ConsoleHost(std::string ProofDirectory, uint32_t Width, uint32_t Height) noexcept
    : Proofs(std::move(ProofDirectory)), Surface(std::make_unique<SoftwareRaster>(Width, Height))
{
    Register();
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

void ConsoleHost::DescribeItem(const SceneItem& Item) noexcept
{
    Box3 B = Item.Bounds();
    if (Item.Kind == ItemKind::Curve)
    {
        const NurbsCurve& C = Item.Curve;
        Row("#%-3u %-18s curve    deg %d  poles %-4d %s%s  length %.4f  bounds [%.2f %.2f %.2f]–[%.2f %.2f %.2f]%s%s",
            Item.Identity, Item.Name.c_str(), C.Degree, C.PoleCount(), C.Rational() ? "rational" : "integral", C.Closed() ? " closed" : "",
            C.Length(), B.Low.X, B.Low.Y, B.Low.Z, B.High.X, B.High.Y, B.High.Z, Item.Construction ? "  [construction]" : "", Item.Selected ? "  [selected]" : "");
    }
    else
    {
        const NurbsSurface& S = Item.Surface;
        Row("#%-3u %-18s surface  deg %dx%d  poles %dx%d  %s%s%s  bounds [%.2f %.2f %.2f]–[%.2f %.2f %.2f]%s",
            Item.Identity, Item.Name.c_str(), S.DegreeU, S.DegreeV, S.CountU, S.CountV, S.Rational() ? "rational" : "integral",
            S.ClosedU() ? " closedU" : "", S.ClosedV() ? " closedV" : "", B.Low.X, B.Low.Y, B.Low.Z, B.High.X, B.High.Y, B.High.Z, Item.Selected ? "  [selected]" : "");
    }
}

bool ConsoleHost::AddCurve(const CommandLine& C, const char* Stem, Deliver<NurbsCurve> Result) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneItem& Item = Scene.AddCurve(C.FlagValue("name").value_or(Stem), std::move(Result.Payload));
    Item.Construction = C.Flag("construction");
    DescribeItem(Item);
    return true;
}

bool ConsoleHost::AddSurface(const CommandLine& C, const char* Stem, Deliver<NurbsSurface> Result) noexcept
{
    if (!Result) return Refuse("%s refused: %s — %s", Stem, Refusal::Describe(Result.Denial.Reason), Result.Denial.Detail);
    SceneItem& Item = Scene.AddSurface(C.FlagValue("name").value_or(Stem), std::move(Result.Payload));
    DescribeItem(Item);
    return true;
}

SceneItem* ConsoleHost::Resolve(const std::string& Token) noexcept
{
    if (!Token.empty() && Token[0] == '#')
        if (auto Id = CommandCodec::ParseNumber(Token.substr(1))) return Scene.Find(static_cast<uint32_t>(*Id));
    if (SceneItem* I = Scene.Find(Token)) return I;
    if (auto Id = CommandCodec::ParseNumber(Token)) return Scene.Find(static_cast<uint32_t>(*Id));
    return nullptr;
}

std::vector<SceneItem*> ConsoleHost::ResolveMany(const CommandLine& C, size_t FirstIndex) noexcept
{
    std::vector<SceneItem*> Out;
    if (C.Count() <= FirstIndex || (C.Count() == FirstIndex + 1 && C.Arguments[FirstIndex] == "selected"))
    {
        for (SceneItem& I : Scene.Items()) if (I.Selected) Out.push_back(&I);
        return Out;
    }
    if (C.Count() == FirstIndex + 1 && C.Arguments[FirstIndex] == "all")
    {
        for (SceneItem& I : Scene.Items()) Out.push_back(&I);
        return Out;
    }
    for (size_t I = FirstIndex; I < C.Count(); ++I)
        if (SceneItem* Item = Resolve(C.Arguments[I])) Out.push_back(Item);
        else { Refuse("no item '%s'", C.Arguments[I].c_str()); Out.clear(); return Out; }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER
//------------------------------------------------------------------------------------------------------------------------

void ConsoleHost::Render() noexcept
{
    Surface->BeginTarget(Backdrop);
    Surface->BindView(View.ToViewRecord(Surface->Width(), Surface->Height(), 1.0));
    Surface->DrawGrid();

    for (const SceneItem& Item : Scene.Items())
    {
        if (Item.Hidden || Item.Kind != ItemKind::Surface) continue;
        DrawRecord D = ScenePresentation::Tinted(0.62f, 0.66f, 0.72f);
        D.PickIdentity = Item.Identity;
        D.Highlight = Item.Selected ? 2.0f : 0.0f;
        Surface->DrawSurface(ScenePresentation::SurfaceTriangles(Item.Surface, 2e-3), D);
        if (ShowIsoCurves)
        {
            DrawRecord Iso = ScenePresentation::Tinted(0.10f, 0.11f, 0.13f, 0.45f); Iso.LineWidth = 1.0f; Iso.PickIdentity = Item.Identity;
            Surface->DrawSegments(ScenePresentation::SurfaceIsoCurves(Item.Surface, 8, 8), Iso);
        }
        if (ShowControlCages || Item.Selected)
        {
            DrawRecord Net = ScenePresentation::Tinted(0.95f, 0.80f, 0.30f, 0.9f); Net.LineWidth = 1.0f; Net.Dashed = true; Net.PointSize = 6.0f;
            Surface->DrawSegments(ScenePresentation::ControlNet(Item.Surface), Net);
            Surface->DrawPoints(ScenePresentation::ControlPoints(Item.Surface), Net);
        }
    }
    for (const SceneItem& Item : Scene.Items())
    {
        if (Item.Hidden || Item.Kind != ItemKind::Curve) continue;
        DrawRecord D = Item.Selected ? ScenePresentation::Tinted(1.0f, 0.62f, 0.20f) : ScenePresentation::Tinted(0.92f, 0.94f, 0.97f);
        D.LineWidth = Item.Selected ? 2.5f : 2.0f;
        D.Dashed = Item.Construction;
        D.PickIdentity = Item.Identity;
        D.Highlight = Item.Selected ? 2.0f : 0.0f;
        Surface->DrawSegments(ScenePresentation::CurveSegments(Item.Curve), D);
        if (ShowControlCages || Item.Selected)
        {
            DrawRecord Cage = ScenePresentation::Tinted(0.95f, 0.80f, 0.30f, 0.9f); Cage.LineWidth = 1.0f; Cage.Dashed = true; Cage.PointSize = 7.0f;
            Surface->DrawSegments(ScenePresentation::ControlPolygon(Item.Curve), Cage);
            Surface->DrawPoints(ScenePresentation::ControlPoints(Item.Curve), Cage);
        }
    }

    Surface->BeginOverlay();
    ScenePresentation::DrawTriad(*Surface, View.OrthographicHalfHeight() * 0.12);
    Surface->EndTarget();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMMANDS
//------------------------------------------------------------------------------------------------------------------------

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
        return AddCurve(C, "Line", NurbsCurve::Line(A, B));
    });
    Add("polyline", "polyline (x,y) (x,y) ... [--closed]", [=, this](const CommandLine& C)
    {
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 0; I < C.Count(); ++I) { if (!Lift(C, I, P)) return Refuse("polyline: argument %zu is not a point", I + 1); Pts.push_back(P); }
        return AddCurve(C, "Polyline", NurbsCurve::Polyline(Pts, C.Flag("closed")));
    });
    Add("rect", "rect (x,y) (x,y) [--radius=R] [--center]", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "rect")) return false;
        auto A = C.Point2(0), B = C.Point2(1); if (!A || !B) return Refuse("rect: two planar points required");
        if (C.Flag("center")) { Vec2 Half = *B; B = *A + Half; A = *A - Half; }
        return AddCurve(C, "Rectangle", NurbsCurve::Rectangle(Plane, *A, *B, C.FlagNumber("radius").value_or(0.0)));
    });
    Add("polygon", "polygon (cx,cy) radius sides [--rotation=deg] [--circumscribed]", [=, this](const CommandLine& C)
    {
        double R = 0, N = 0; if (!Need(C, 3, "polygon") || !NumberArg(C, 1, R, "polygon") || !NumberArg(C, 2, N, "polygon")) return false;
        auto Ctr = C.Point2(0); if (!Ctr) return Refuse("polygon: centre must be a planar point");
        return AddCurve(C, "Polygon", NurbsCurve::Polygon(Plane, *Ctr, R, static_cast<int>(N), ScalarCriteria::Radians(C.FlagNumber("rotation").value_or(0.0)), !C.Flag("circumscribed")));
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
        Vec3 N = Plane.Normal(); if (auto F = C.FlagValue("normal")) if (auto V = CommandCodec::ParsePoint(*F)) N = *V;
        return AddCurve(C, "Circle", NurbsCurve::Circle(Ctr, N, R));
    });
    Add("arc", "arc (cx,cy) radius startDeg sweepDeg  |  arc --three (a) (b) (c)", [=, this](const CommandLine& C)
    {
        if (C.Flag("three"))
        {
            Vec3 A, B, D; if (!Need(C, 3, "arc") || !Lift(C, 0, A) || !Lift(C, 1, B) || !Lift(C, 2, D)) return Refuse("arc --three: three points required");
            return AddCurve(C, "Arc", NurbsCurve::ArcThreePoints(A, B, D));
        }
        Vec3 Ctr; double R = 0, S0 = 0, Sw = 0;
        if (!Need(C, 4, "arc") || !Lift(C, 0, Ctr) || !NumberArg(C, 1, R, "arc") || !NumberArg(C, 2, S0, "arc") || !NumberArg(C, 3, Sw, "arc")) return false;
        return AddCurve(C, "Arc", NurbsCurve::Arc(Ctr, Plane.Normal(), R, ScalarCriteria::Radians(S0), ScalarCriteria::Radians(Sw)));
    });
    Add("ellipse", "ellipse (cx,cy) radiusMajor radiusMinor [--rotation=deg]", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double A = 0, B = 0; if (!Need(C, 3, "ellipse") || !Lift(C, 0, Ctr) || !NumberArg(C, 1, A, "ellipse") || !NumberArg(C, 2, B, "ellipse")) return false;
        double Rot = ScalarCriteria::Radians(C.FlagNumber("rotation").value_or(0.0));
        Vec3 Major = Plane.AxisX * std::cos(Rot) + Plane.AxisY * std::sin(Rot);
        return AddCurve(C, "Ellipse", NurbsCurve::Ellipse(Ctr, Plane.Normal(), Major, A, B));
    });
    Add("spline", "spline (p) (p) (p) ... [--degree=3] [--closed]   interpolating", [=, this](const CommandLine& C)
    {
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 0; I < C.Count(); ++I) { if (!Lift(C, I, P)) return Refuse("spline: argument %zu is not a point", I + 1); Pts.push_back(P); }
        return AddCurve(C, "Spline", NurbsCurve::Interpolate(Pts, static_cast<int>(C.FlagNumber("degree").value_or(3)), C.Flag("closed")));
    });
    Add("cpcurve", "cpcurve (p) (p) (p) ... [--degree=3] [--periodic]   control-point curve", [=, this](const CommandLine& C)
    {
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 0; I < C.Count(); ++I) { if (!Lift(C, I, P)) return Refuse("cpcurve: argument %zu is not a point", I + 1); Pts.push_back(P); }
        return AddCurve(C, "ControlCurve", NurbsCurve::ControlPoints(static_cast<int>(C.FlagNumber("degree").value_or(3)), Pts, C.Flag("periodic")));
    });

    //---------------------------------------------- primitive surfaces ----------------------------------------------
    Add("sphere", "sphere (cx,cy,cz) radius", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double R = 0; if (!Need(C, 2, "sphere") || !PointArg(C, 0, Ctr, "sphere") || !NumberArg(C, 1, R, "sphere")) return false;
        return AddSurface(C, "Sphere", NurbsSurface::Sphere(Ctr, R));
    });
    Add("cylinder", "cylinder (foot) radius height [--axis=(x,y,z)]", [=, this](const CommandLine& C)
    {
        Vec3 F; double R = 0, H = 0; if (!Need(C, 3, "cylinder") || !PointArg(C, 0, F, "cylinder") || !NumberArg(C, 1, R, "cylinder") || !NumberArg(C, 2, H, "cylinder")) return false;
        Vec3 Axis = Plane.Normal(); if (auto A = C.FlagValue("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        return AddSurface(C, "Cylinder", NurbsSurface::Cylinder(F, Axis, R, H));
    });
    Add("cone", "cone (foot) radiusFoot radiusTop height [--axis=(x,y,z)]", [=, this](const CommandLine& C)
    {
        Vec3 F; double R0 = 0, R1 = 0, H = 0;
        if (!Need(C, 4, "cone") || !PointArg(C, 0, F, "cone") || !NumberArg(C, 1, R0, "cone") || !NumberArg(C, 2, R1, "cone") || !NumberArg(C, 3, H, "cone")) return false;
        Vec3 Axis = Plane.Normal(); if (auto A = C.FlagValue("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        return AddSurface(C, "Cone", NurbsSurface::Cone(F, Axis, R0, R1, H));
    });
    Add("torus", "torus (centre) radiusMajor radiusMinor [--axis=(x,y,z)]", [=, this](const CommandLine& C)
    {
        Vec3 Ctr; double R0 = 0, R1 = 0; if (!Need(C, 3, "torus") || !PointArg(C, 0, Ctr, "torus") || !NumberArg(C, 1, R0, "torus") || !NumberArg(C, 2, R1, "torus")) return false;
        Vec3 Axis = Plane.Normal(); if (auto A = C.FlagValue("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        return AddSurface(C, "Torus", NurbsSurface::Torus(Ctr, Axis, R0, R1));
    });
    Add("plane", "plane (origin) lengthU lengthV", [=, this](const CommandLine& C)
    {
        Vec3 O; double LU = 0, LV = 0; if (!Need(C, 3, "plane") || !PointArg(C, 0, O, "plane") || !NumberArg(C, 1, LU, "plane") || !NumberArg(C, 2, LV, "plane")) return false;
        return AddSurface(C, "Plane", NurbsSurface::Plane(O, Plane.AxisX, Plane.AxisY, LU, LV));
    });
    Add("patch", "patch countU countV (p00) (p01) ... row-major [--degree=3]   B-spline patch", [=, this](const CommandLine& C)
    {
        double CU = 0, CV = 0; if (!Need(C, 2, "patch") || !NumberArg(C, 0, CU, "patch") || !NumberArg(C, 1, CV, "patch")) return false;
        std::vector<Vec3> Pts; Vec3 P;
        for (size_t I = 2; I < C.Count(); ++I) { if (!PointArg(C, I, P, "patch")) return false; Pts.push_back(P); }
        int Deg = static_cast<int>(C.FlagNumber("degree").value_or(3));
        return AddSurface(C, "Patch", NurbsSurface::Patch(std::min(Deg, int(CU) - 1), std::min(Deg, int(CV) - 1), int(CU), int(CV), Pts));
    });

    //---------------------------------------------- derived surfaces ----------------------------------------------
    Add("extrude", "extrude <curve> length [--direction=(x,y,z)]", [=, this](const CommandLine& C)
    {
        double L = 0; if (!Need(C, 2, "extrude") || !NumberArg(C, 1, L, "extrude")) return false;
        SceneItem* Item = Resolve(C.Arguments[0]); if (!Item || Item->Kind != ItemKind::Curve) return Refuse("extrude: '%s' is not a curve", C.Arguments[0].c_str());
        Vec3 Dir = Plane.Normal(); if (auto A = C.FlagValue("direction")) if (auto V = CommandCodec::ParsePoint(*A)) Dir = *V;
        return AddSurface(C, "Extrusion", NurbsSurface::Extrusion(Item->Curve, Dir, L));
    });
    Add("revolve", "revolve <curve> angleDeg [--origin=(x,y,z)] [--axis=(x,y,z)]", [=, this](const CommandLine& C)
    {
        double Angle = 0; if (!Need(C, 2, "revolve") || !NumberArg(C, 1, Angle, "revolve")) return false;
        SceneItem* Item = Resolve(C.Arguments[0]); if (!Item || Item->Kind != ItemKind::Curve) return Refuse("revolve: '%s' is not a curve", C.Arguments[0].c_str());
        Vec3 O = Plane.Origin, Axis = Plane.AxisY;
        if (auto A = C.FlagValue("origin")) if (auto V = CommandCodec::ParsePoint(*A)) O = *V;
        if (auto A = C.FlagValue("axis")) if (auto V = CommandCodec::ParsePoint(*A)) Axis = *V;
        return AddSurface(C, "Revolution", NurbsSurface::Revolution(Item->Curve, O, Axis, ScalarCriteria::Radians(Angle)));
    });
    Add("loft", "loft <curve> <curve> ... [--degree=3]", [=, this](const CommandLine& C)
    {
        std::vector<NurbsCurve> Sections;
        for (SceneItem* I : ResolveMany(C, 0)) { if (I->Kind != ItemKind::Curve) return Refuse("loft: '%s' is not a curve", I->Name.c_str()); Sections.push_back(I->Curve); }
        if (Sections.size() < 2) return Refuse("loft: at least two curves");
        return AddSurface(C, "Loft", NurbsSurface::Loft(Sections, static_cast<int>(C.FlagNumber("degree").value_or(3))));
    });
    Add("ruled", "ruled <curveA> <curveB>", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "ruled")) return false;
        SceneItem* A = Resolve(C.Arguments[0]); SceneItem* B = Resolve(C.Arguments[1]);
        if (!A || !B || A->Kind != ItemKind::Curve || B->Kind != ItemKind::Curve) return Refuse("ruled: two curves required");
        return AddSurface(C, "Ruled", NurbsSurface::Ruled(A->Curve, B->Curve));
    });

    //---------------------------------------------- scene ----------------------------------------------
    Add("list", "list — every item with its measurements", [=, this](const CommandLine&)
    {
        if (Scene.Items().empty()) Row("(empty scene)");
        for (const SceneItem& I : Scene.Items()) DescribeItem(I);
        return true;
    });
    Add("describe", "describe <item> — poles and knots", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "describe")) return false;
        SceneItem* Item = Resolve(C.Arguments[0]); if (!Item) return Refuse("no item '%s'", C.Arguments[0].c_str());
        DescribeItem(*Item);
        if (Item->Kind == ItemKind::Curve)
        {
            const NurbsCurve& K = Item->Curve;
            std::printf("    knots:"); for (double T : K.Knots) std::printf(" %.4g", T); std::printf("\n");
            for (int I = 0; I < K.PoleCount(); ++I) { Vec3 P = K.Poles[I].Divide(); std::printf("    pole %-3d (%9.4f %9.4f %9.4f)  w %.4f\n", I, P.X, P.Y, P.Z, K.Poles[I].W); }
        }
        else
        {
            const NurbsSurface& S = Item->Surface;
            std::printf("    knotsU:"); for (double T : S.KnotsU) std::printf(" %.4g", T); std::printf("\n    knotsV:"); for (double T : S.KnotsV) std::printf(" %.4g", T); std::printf("\n");
            for (int I = 0; I < S.CountU; ++I) for (int J = 0; J < S.CountV; ++J) { Vec3 P = S.Pole(I, J).Divide(); std::printf("    pole %2d,%-2d (%9.4f %9.4f %9.4f)  w %.4f\n", I, J, P.X, P.Y, P.Z, S.Pole(I, J).W); }
        }
        return true;
    });
    Add("select", "select <item...> | all | none | invert", [=, this](const CommandLine& C)
    {
        if (C.Count() == 1 && C.Arguments[0] == "none") { for (SceneItem& I : Scene.Items()) I.Selected = false; Row("selection cleared"); return true; }
        if (C.Count() == 1 && C.Arguments[0] == "invert") { for (SceneItem& I : Scene.Items()) I.Selected = !I.Selected; }
        else
        {
            std::vector<SceneItem*> Items = ResolveMany(C, 0); if (Items.empty()) return false;
            if (!C.Flag("add")) for (SceneItem& I : Scene.Items()) I.Selected = false;
            for (SceneItem* I : Items) I->Selected = true;
        }
        int N = 0; for (const SceneItem& I : Scene.Items()) if (I.Selected) { ++N; DescribeItem(I); }
        Row("%d selected", N);
        return true;
    });
    Add("delete", "delete <item...> | selected", [=, this](const CommandLine& C)
    {
        std::vector<uint32_t> Ids; for (SceneItem* I : ResolveMany(C, 0)) Ids.push_back(I->Identity);
        for (uint32_t Id : Ids) Scene.Remove(Id);
        Row("deleted %zu item(s)", Ids.size());
        return true;
    });
    Add("hide", "hide <item...> | selected  ·  unhide all", [=, this](const CommandLine& C)
    {
        for (SceneItem* I : ResolveMany(C, 0)) I->Hidden = true;
        return true;
    });
    Add("unhide", "unhide <item...> | all", [=, this](const CommandLine& C)
    {
        for (SceneItem* I : ResolveMany(C, 0)) I->Hidden = false;
        return true;
    });
    Add("rename", "rename <item> <newName>", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "rename")) return false;
        SceneItem* I = Resolve(C.Arguments[0]); if (!I) return Refuse("no item '%s'", C.Arguments[0].c_str());
        I->Name = Scene.UniqueName(C.Arguments[1]); DescribeItem(*I); return true;
    });
    Add("clear", "clear — empty the scene", [=, this](const CommandLine&) { Scene.Clear(); Row("scene cleared"); return true; });
    Add("move", "move <item...> (dx,dy,dz)", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "move")) return false;
        Vec3 D; if (!PointArg(C, C.Count() - 1, D, "move")) return false;
        CommandLine Sub = C; Sub.Arguments.pop_back();
        Mat4 M = Mat4::Translation(D);
        for (SceneItem* I : ResolveMany(Sub, 0)) { if (I->Kind == ItemKind::Curve) I->Curve = I->Curve.Transformed(M); else I->Surface = I->Surface.Transformed(M); DescribeItem(*I); }
        return true;
    });

    //---------------------------------------------- workplane & view ----------------------------------------------
    Add("workplane", "workplane xy|xz|yz [--origin=(x,y,z)]", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "workplane")) return false;
        const std::string& N = C.Arguments[0];
        if (N == "xy")      Plane = Workplane::XY();
        else if (N == "xz") Plane = Workplane::XZ();
        else if (N == "yz") Plane = Workplane::YZ();
        else return Refuse("workplane: xy, xz or yz");
        if (auto O = C.FlagValue("origin")) if (auto V = CommandCodec::ParsePoint(*O)) Plane.Origin = *V;
        Row("workplane %s origin (%.3f %.3f %.3f) normal (%.0f %.0f %.0f)", N.c_str(), Plane.Origin.X, Plane.Origin.Y, Plane.Origin.Z, Plane.Normal().X, Plane.Normal().Y, Plane.Normal().Z);
        return true;
    });
    Add("view", "view front|back|right|left|top|bottom|iso|persp|ortho  ·  view orbit yawDeg pitchDeg  ·  view frame [selected]  ·  view dolly steps", [=, this](const CommandLine& C)
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
        else if (N == "ortho") View.Orthographic = true;
        else if (N == "orbit") { double Y = 0, P = 0; if (!NumberArg(C, 1, Y, "view") || !NumberArg(C, 2, P, "view")) return false; View.Orbit(ScalarCriteria::Radians(Y), ScalarCriteria::Radians(P)); }
        else if (N == "dolly") { double S = 0; if (!NumberArg(C, 1, S, "view")) return false; View.Dolly(S); }
        else if (N == "frame")
        {
            Box3 B = Scene.Bounds(C.Count() > 1 && C.Arguments[1] == "selected");
            if (B.Empty()) B.Include({ -5, -5, 0 }), B.Include({ 5, 5, 0 });
            View.Frame(B.Inflated(B.Diagonal() * 0.05), Aspect);
        }
        else return Refuse("view: unknown mode '%s'", N.c_str());
        Vec3 E = View.Eye();
        Row("view %s  eye (%.2f %.2f %.2f)  pivot (%.2f %.2f %.2f)  distance %.2f  yaw %.1f° pitch %.1f°  %s",
            N.c_str(), E.X, E.Y, E.Z, View.Pivot.X, View.Pivot.Y, View.Pivot.Z, View.Distance, ScalarCriteria::Degrees(View.Yaw), ScalarCriteria::Degrees(View.Pitch), View.Orthographic ? "ortho" : "persp");
        return true;
    });
    Add("show", "show cages on|off  ·  show iso on|off", [=, this](const CommandLine& C)
    {
        if (!Need(C, 2, "show")) return false;
        bool On = C.Arguments[1] == "on";
        if (C.Arguments[0] == "cages") ShowControlCages = On; else if (C.Arguments[0] == "iso") ShowIsoCurves = On; else return Refuse("show: cages|iso");
        return true;
    });
    Add("render", "render <name> [--size=WxH] — writes Proofs/<name>.png", [=, this](const CommandLine& C)
    {
        if (!Need(C, 1, "render")) return false;
        if (auto S = C.FlagValue("size"))
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
    Add("pick", "pick x y — identity and depth under a pixel of the last render", [=, this](const CommandLine& C)
    {
        double X = 0, Y = 0; if (!Need(C, 2, "pick") || !NumberArg(C, 0, X, "pick") || !NumberArg(C, 1, Y, "pick")) return false;
        uint32_t Id = Surface->Pick(uint32_t(X), uint32_t(Y));
        SceneItem* Item = Scene.Find(Id);
        Row("pixel (%d,%d) → %s%s  depth %.5f", int(X), int(Y), Id ? "#" : "nothing", Id ? std::to_string(Id).c_str() : "", Surface->Depth(uint32_t(X), uint32_t(Y)));
        if (Item) DescribeItem(*Item);
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
        if (!It->second(C)) Ok = false;
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
