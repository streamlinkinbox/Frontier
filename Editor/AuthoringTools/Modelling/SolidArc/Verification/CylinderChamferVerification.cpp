//============================================================================================================================================
// 📦 Verification/CylinderChamferVerification.cpp — Phase 25: exact conical chamfers on circular cylinder cap edges
//============================================================================================================================================
// A circular cap edge is not eligible for the straight-edge prism cutter. Its exact bevel is instead a cylinder plus a
// conical frustum sewn along their shared circle; every assertion below checks that conic geometry rather than accepting
// a merely closed/tessellated approximation.
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] int CapEdgeAt(const BrepBody& Body, Vec3 Origin, Vec3 Axis, double Height) noexcept
{
    Axis = Axis.Normalised();
    for (size_t E = 0; E < Body.Edges.size(); ++E)
    {
        const BrepEdge& Edge = Body.Edges[E];
        if (!Edge.Closed() || Edge.Curve.Classification != CurveClassification::Circle) continue;
        Vec3 P = Edge.Curve.Sample(0.5 * (Edge.Curve.DomainStart() + Edge.Curve.DomainEnd()));
        if (std::fabs((P - Origin).Dot(Axis) - Height) < 1e-9) return static_cast<int>(E);
    }
    return -1;
}

[[nodiscard]] bool ExactConicalBevel(const BrepBody& Body, double Radius, double SetBack) noexcept
{
    if (!Body.Validate().Solid() || Body.Vertices.size() != 3 || Body.Edges.size() != 5 || Body.Faces.size() != 4) return false;
    const NurbsSurface* Cone = nullptr;
    int Cylinders = 0, Planes = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Surface.Classification == SurfaceClassification::Cylinder) ++Cylinders;
        else if (Face.Surface.Classification == SurfaceClassification::Cone) Cone = &Face.Surface;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++Planes;
        else return false;
    }
    if (!Cone || Cylinders != 1 || Planes != 2 || (std::fabs(Cone->RadiusMajor - Radius) > 1e-12 && std::fabs(Cone->RadiusMinor - Radius) > 1e-12)) return false;
    if (std::fabs(std::fabs(Cone->RadiusMajor - Cone->RadiusMinor) - SetBack) > 1e-12) return false;
    Vec3 Axis = Cone->Axis.Normalised();
    double U0 = Cone->DomainStartU(), U1 = Cone->DomainEndU(), V0 = Cone->DomainStartV(), V1 = Cone->DomainEndV();
    Vec3 Start = Cone->Sample(U0, V0), End = Cone->Sample(U0, V1);
    double Height = (End - Start).Dot(Axis);
    if (Height <= 0.0 || std::fabs(Height - SetBack) > 1e-12) return false;
    for (int I = 0; I < 7; ++I)
        for (int J = 0; J < 5; ++J)
        {
            double U = U0 + (U1 - U0) * (static_cast<double>(I) / 6.0);
            Vec3 P = Cone->Sample(U, V0 + (V1 - V0) * (static_cast<double>(J) / 4.0));
            double Along = (P - Cone->Origin).Dot(Axis);
            Vec3 Radial = P - (Cone->Origin + Axis * Along);
            double Expected = Cone->RadiusMajor + (Cone->RadiusMinor - Cone->RadiusMajor) * (Along / Height);
            if (std::fabs(Radial.Length() - Expected) > 1e-10 || std::fabs(Radial.Dot(Axis)) > 1e-10) return false;
        }
    return true;
}

[[nodiscard]] double ChamferedCylinderVolume(double Radius, double Height, double SetBack) noexcept
{
    return ScalarCriteria::Pi * Radius * Radius * (Height - SetBack) +
           ScalarCriteria::Pi * SetBack * (Radius * Radius + Radius * (Radius - SetBack) + (Radius - SetBack) * (Radius - SetBack)) / 3.0;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 25 · Cylinder Chamfer Verification — exact cone-frustum bevels on curved circular edges");
    constexpr double Radius = 10.0, Height = 20.0, SetBack = 3.0;
    const BrepBody Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, Radius, Height).Payload;
    const int Bottom = CapEdgeAt(Cylinder, { 0, 0, 0 }, { 0, 0, 1 }, 0.0), Top = CapEdgeAt(Cylinder, { 0, 0, 0 }, { 0, 0, 1 }, Height);

    Panel.Section("Circular top and bottom cap edges rebuild as exact cylinder-plus-cone solids");
    Panel.Expect("Native cylinder exposes both circular cap edges", Bottom >= 0 && Top >= 0 && Bottom != Top);
    EdgeCornerFrame Frame; std::string Why;
    Panel.Expect("The generic straight-edge frame deliberately refuses a circular cap edge", !BlendSolver::Frame(Cylinder, Top, Frame, Why));
    for (const auto& Case : { std::pair{ "top", Top }, std::pair{ "bottom", Bottom } })
    {
        Deliver<BrepBody> Result = BlendSolver::ChamferEdge(Cylinder, Case.second, SetBack);
        const double RelativeVolumeError = Result ? std::fabs(Result.Payload.Validate().Volume - ChamferedCylinderVolume(Radius, Height, SetBack)) / ChamferedCylinderVolume(Radius, Height, SetBack) : ScalarCriteria::Infinity;
        Panel.Expect((std::string("The ") + Case.first + " circular-cap chamfer is a closed manifold solid").c_str(), Result && Result.Payload.Validate().Solid());
        Panel.Expect((std::string("The ") + Case.first + " chamfer has the intended 3V/5E/4F topology").c_str(), Result && Result.Payload.Vertices.size() == 3 && Result.Payload.Edges.size() == 5 && Result.Payload.Faces.size() == 4);
        Panel.Expect((std::string("The ") + Case.first + " chamfer is an exact conical SetBack=3 surface").c_str(), Result && ExactConicalBevel(Result.Payload, Radius, SetBack));
        Panel.Within((std::string("The ") + Case.first + " chamfer volume observes the analytic frustum within tessellation quadrature").c_str(), RelativeVolumeError, 4e-4);
    }

    Panel.Section("Axis-independent construction preserves the exact conical bevel");
    {
        const Vec3 ObliqueFoot{ 2.0, -4.0, 3.0 }, ObliqueAxis{ 2.0, -1.0, 4.0 };
        constexpr double ObliqueRadius = 8.0, ObliqueHeight = 15.0, ObliqueSetBack = 2.0;
        const BrepBody Oblique = BrepBody::Cylinder(ObliqueFoot, ObliqueAxis, ObliqueRadius, ObliqueHeight).Payload;
        const int ObliqueTop = CapEdgeAt(Oblique, ObliqueFoot, ObliqueAxis, ObliqueHeight);
        Deliver<BrepBody> Result = ObliqueTop >= 0 ? BlendSolver::ChamferEdge(Oblique, ObliqueTop, ObliqueSetBack) : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique cap edge not found");
        Panel.Expect("An oblique, non-unit construction axis exposes its upper circular cap edge", ObliqueTop >= 0);
        Panel.Expect("The oblique circular-cap chamfer remains a closed manifold solid", Result && Result.Payload.Validate().Solid());
        Panel.Expect("The oblique chamfer retains the exact sampled conical generatrix", Result && ExactConicalBevel(Result.Payload, ObliqueRadius, ObliqueSetBack));
    }

    Panel.Section("The structural circular-cap route does not overclaim equivalent-looking topology");
    {
        Deliver<NurbsCurve> Circle = NurbsCurve::Circle({ 0, 0, 0 }, { 0, 0, 1 }, Radius);
        Deliver<NurbsSurface> Side = Circle ? NurbsSurface::Extrusion(Circle.Payload, { 0, 0, 1 }, Height) : Deliver<NurbsSurface>::Reject(RefusalReason::Unsupported, "source circle failed");
        std::vector<NurbsSurface> Faces; if (Side) Faces.push_back(std::move(Side.Payload));
        Deliver<BrepBody> Extrusion = BrepBody::Sew(Faces);
        int ExtrusionTop = Extrusion ? CapEdgeAt(Extrusion.Payload, { 0, 0, 0 }, { 0, 0, 1 }, Height) : -1;
        Panel.Expect("The independently sewn circular extrusion is a valid solid with a circular cap edge", Extrusion && Extrusion.Payload.Validate().Solid() && ExtrusionTop >= 0);
        Panel.Expect("Circular-extrusion cap chamfer stays on the unsupported curved-edge path", !(Extrusion && ExtrusionTop >= 0 && BlendSolver::ChamferEdge(Extrusion.Payload, ExtrusionTop, SetBack)));
    }

    Panel.Section("Feasibility limits refuse instead of collapsing the cylinder");
    Panel.Expect("Set-back reaching the cylinder axis is refused", !BlendSolver::ChamferEdge(Cylinder, Top, Radius));
    Panel.Expect("Set-back consuming the full height is refused", !BlendSolver::ChamferEdge(Cylinder, Top, Height));

    Panel.Section("C++ console proof: top and bottom circular-edge chamfers");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase25_CylinderChamfers.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto Run = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            Run("gizmo off") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=TopSource") && Run("chamfer TopSource 3 --edges=2 --name=TopChamfer") && Run("matcap TopChamfer gold") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 0") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=BottomSource") && Run("chamfer BottomSource 3 --edges=0 --name=BottomChamfer") && Run("matcap BottomChamfer plastic-blue") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 1") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=SmallTop") && Run("chamfer SmallTop 1 --edges=2 --name=SmallChamfer") && Run("matcap SmallChamfer steel") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 2") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=LargeTop") && Run("chamfer LargeTop 5 --edges=2 --name=LargeChamfer") && Run("matcap LargeChamfer copper") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 3") &&
            Run("render sheet finalize Phase25_CylinderChamfers");
        Panel.Expect("C++ circular-chamfer proof commands complete without refusal", Rendered);
        Panel.Expect("C++ circular-chamfer proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
