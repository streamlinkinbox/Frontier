//============================================================================================================================================
// 📦 Verification/CylinderFilletVerification.cpp — Phase 26: exact rolling-ball fillets on circular cylinder cap edges
//============================================================================================================================================
// A native circular cylinder rim has an analytic constant-radius solution: revolve a rational quarter-circle meridian
// around the cylinder axis. These checks verify the quarter-torus itself and its G1 endpoint normals, not only a mesh.
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

[[nodiscard]] bool ExactTorusRoll(const BrepBody& Body, double Radius, double RollRadius) noexcept
{
    if (!Body.Validate().Solid() || Body.Vertices.size() != 3 || Body.Edges.size() != 5 || Body.Faces.size() != 4) return false;
    const NurbsSurface* Torus = nullptr;
    int TorusFace = -1, Cylinders = 0, Planes = 0;
    for (size_t F = 0; F < Body.Faces.size(); ++F)
    {
        const NurbsSurface& Surface = Body.Faces[F].Surface;
        if (Surface.Classification == SurfaceClassification::Cylinder) ++Cylinders;
        else if (Surface.Classification == SurfaceClassification::Torus) { Torus = &Surface; TorusFace = static_cast<int>(F); }
        else if (Surface.Classification == SurfaceClassification::Plane) ++Planes;
        else return false;
    }
    if (!Torus || Cylinders != 1 || Planes != 2 || std::fabs(Torus->RadiusMajor - (Radius - RollRadius)) > 1e-12 || std::fabs(Torus->RadiusMinor - RollRadius) > 1e-12) return false;
    Vec3 Axis = Torus->Axis.Normalised();
    double U0 = Torus->DomainStartU(), U1 = Torus->DomainEndU(), V0 = Torus->DomainStartV(), V1 = Torus->DomainEndV();
    for (int I = 0; I < 7; ++I)
        for (int J = 0; J < 5; ++J)
        {
            Vec3 P = Torus->Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 6.0), V0 + (V1 - V0) * (static_cast<double>(J) / 4.0));
            Vec3 Delta = P - Torus->Origin;
            double Along = Delta.Dot(Axis);
            Vec3 Radial = Delta - Axis * Along;
            double Implicit = (Radial.Length() - Torus->RadiusMajor) * (Radial.Length() - Torus->RadiusMajor) + Along * Along - RollRadius * RollRadius;
            if (std::fabs(Implicit) > 1e-10) return false;
        }
    // V starts where the torus meets the retained cylindrical wall, then ends where it meets its planar cap.
    Vec3 Outer = Torus->Sample(0.5 * (U0 + U1), V0), Inner = Torus->Sample(0.5 * (U0 + U1), V1);
    double OuterAlong = (Outer - Torus->Origin).Dot(Axis), InnerAlong = (Inner - Torus->Origin).Dot(Axis);
    Vec3 OuterRadial = Outer - Torus->Origin - Axis * OuterAlong;
    Vec3 InnerRadial = Inner - Torus->Origin - Axis * InnerAlong;
    Vec3 OuterNormal = Body.FaceNormal(TorusFace, 0.5 * (U0 + U1), V0).Normalised();
    Vec3 InnerNormal = Body.FaceNormal(TorusFace, 0.5 * (U0 + U1), V1).Normalised();
    const double CapSign = InnerAlong >= 0.0 ? 1.0 : -1.0;
    return std::fabs(OuterAlong) < 1e-10 && std::fabs(OuterRadial.Length() - Radius) < 1e-10 &&
           std::fabs(InnerRadial.Length() - (Radius - RollRadius)) < 1e-10 && std::fabs(std::fabs(InnerAlong) - RollRadius) < 1e-10 &&
           OuterNormal.Dot(OuterRadial.Normalised()) > 1.0 - 1e-10 && InnerNormal.Dot(Axis * CapSign) > 1.0 - 1e-10;
}

[[nodiscard]] double FilletedCylinderVolume(double Radius, double Height, double RollRadius) noexcept
{
    // Cylinder volume less the revolved square-minus-quarter-circle corner at one circular rim.
    const double Removed = ScalarCriteria::Pi * ((Radius * Radius - (Radius - RollRadius) * (Radius - RollRadius)) * RollRadius -
        (Radius - RollRadius) * ScalarCriteria::Pi * RollRadius * RollRadius * 0.5 - 2.0 * RollRadius * RollRadius * RollRadius / 3.0);
    return ScalarCriteria::Pi * Radius * Radius * Height - Removed;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 26 · Cylinder Fillet Verification — exact quarter-torus rolls on curved circular edges");
    constexpr double Radius = 10.0, Height = 20.0, RollRadius = 3.0;
    const BrepBody Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, Radius, Height).Payload;
    const int Bottom = CapEdgeAt(Cylinder, { 0, 0, 0 }, { 0, 0, 1 }, 0.0), Top = CapEdgeAt(Cylinder, { 0, 0, 0 }, { 0, 0, 1 }, Height);

    Panel.Section("Circular top and bottom cap edges rebuild as exact quarter-torus rolling-ball fillets");
    Panel.Expect("Native cylinder exposes both circular cap edges", Bottom >= 0 && Top >= 0 && Bottom != Top);
    EdgeCornerFrame Frame; std::string Why;
    Panel.Expect("The generic straight-edge frame deliberately refuses a circular cap edge", !BlendSolver::Frame(Cylinder, Top, Frame, Why));
    for (const auto& Case : { std::pair{ "top", Top }, std::pair{ "bottom", Bottom } })
    {
        Deliver<BrepBody> Result = BlendSolver::FilletEdge(Cylinder, Case.second, RollRadius);
        const double RelativeVolumeError = Result ? std::fabs(Result.Payload.Validate().Volume - FilletedCylinderVolume(Radius, Height, RollRadius)) / FilletedCylinderVolume(Radius, Height, RollRadius) : ScalarCriteria::Infinity;
        Panel.Expect((std::string("The ") + Case.first + " circular-cap fillet is a closed manifold solid").c_str(), Result && Result.Payload.Validate().Solid());
        Panel.Expect((std::string("The ") + Case.first + " fillet has the intended 3V/5E/4F topology").c_str(), Result && Result.Payload.Vertices.size() == 3 && Result.Payload.Edges.size() == 5 && Result.Payload.Faces.size() == 4);
        Panel.Expect((std::string("The ") + Case.first + " fillet is an exact quarter-torus with G1 boundary normals").c_str(), Result && ExactTorusRoll(Result.Payload, Radius, RollRadius));
        Panel.Within((std::string("The ") + Case.first + " fillet volume observes the analytic torus construction within tessellation quadrature").c_str(), RelativeVolumeError, 4e-4);
    }

    Panel.Section("Axis-independent construction preserves the exact torus roll");
    {
        const Vec3 ObliqueFoot{ 2.0, -4.0, 3.0 }, ObliqueAxis{ 2.0, -1.0, 4.0 };
        constexpr double ObliqueRadius = 8.0, ObliqueHeight = 15.0, ObliqueRoll = 2.0;
        const BrepBody Oblique = BrepBody::Cylinder(ObliqueFoot, ObliqueAxis, ObliqueRadius, ObliqueHeight).Payload;
        const int ObliqueTop = CapEdgeAt(Oblique, ObliqueFoot, ObliqueAxis, ObliqueHeight);
        Deliver<BrepBody> Result = ObliqueTop >= 0 ? BlendSolver::FilletEdge(Oblique, ObliqueTop, ObliqueRoll) : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique cap edge not found");
        Panel.Expect("An oblique, non-unit construction axis exposes its upper circular cap edge", ObliqueTop >= 0);
        Panel.Expect("The oblique circular-cap fillet remains a closed manifold solid", Result && Result.Payload.Validate().Solid());
        Panel.Expect("The oblique fillet retains the exact sampled torus equation and G1 ends", Result && ExactTorusRoll(Result.Payload, ObliqueRadius, ObliqueRoll));
    }

    Panel.Section("Reversed construction direction retains the same physical rolling-ball result");
    {
        const BrepBody Reversed = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, Radius, -Height).Payload;
        const int FarCap = CapEdgeAt(Reversed, { 0, 0, 0 }, { 0, 0, 1 }, -Height);
        Deliver<BrepBody> Result = FarCap >= 0 ? BlendSolver::FilletEdge(Reversed, FarCap, RollRadius) : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "reversed cap edge not found");
        Panel.Expect("A negative-height cylinder exposes the far circular cap edge", FarCap >= 0);
        Panel.Expect("The reversed-direction cap fillet remains a closed manifold solid", Result && Result.Payload.Validate().Solid());
        Panel.Expect("The reversed-direction fillet retains the exact torus equation and G1 ends", Result && ExactTorusRoll(Result.Payload, Radius, RollRadius));
    }

    Panel.Section("The structural circular-cap route does not overclaim equivalent-looking topology");
    {
        Deliver<NurbsCurve> Circle = NurbsCurve::Circle({ 0, 0, 0 }, { 0, 0, 1 }, Radius);
        Deliver<NurbsSurface> Side = Circle ? NurbsSurface::Extrusion(Circle.Payload, { 0, 0, 1 }, Height) : Deliver<NurbsSurface>::Reject(RefusalReason::Unsupported, "source circle failed");
        std::vector<NurbsSurface> Faces; if (Side) Faces.push_back(std::move(Side.Payload));
        Deliver<BrepBody> Extrusion = BrepBody::Sew(Faces);
        int ExtrusionTop = Extrusion ? CapEdgeAt(Extrusion.Payload, { 0, 0, 0 }, { 0, 0, 1 }, Height) : -1;
        Panel.Expect("The independently sewn circular extrusion is a valid solid with a circular cap edge", Extrusion && Extrusion.Payload.Validate().Solid() && ExtrusionTop >= 0);
        Panel.Expect("Circular-extrusion cap fillet stays on the unsupported curved-edge path", !(Extrusion && ExtrusionTop >= 0 && BlendSolver::FilletEdge(Extrusion.Payload, ExtrusionTop, RollRadius)));
    }

    Panel.Section("Feasibility limits refuse instead of collapsing the cylinder");
    Panel.Expect("Fillet radius reaching the cylinder axis is refused", !BlendSolver::FilletEdge(Cylinder, Top, Radius));
    Panel.Expect("Fillet radius consuming the full height is refused", !BlendSolver::FilletEdge(Cylinder, Top, Height));

    Panel.Section("C++ console proof: top and bottom circular-edge rolling-ball fillets");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase26_CylinderFillets.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto Run = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            Run("gizmo off") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=TopSource") && Run("fillet TopSource 3 --edges=2 --name=TopFillet") && Run("matcap TopFillet gold") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 0") &&
            Run("reset") && Run("view bottom") && Run("view orbit 45 25") && Run("cylinder (0,0,0) 10 20 --name=BottomSource") && Run("fillet BottomSource 3 --edges=0 --name=BottomFillet") && Run("matcap BottomFillet plastic-blue") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 1") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=SmallTop") && Run("fillet SmallTop 1 --edges=2 --name=SmallFillet") && Run("matcap SmallFillet steel") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 2") &&
            Run("reset") && Run("view iso") && Run("cylinder (0,0,0) 10 20 --name=LargeTop") && Run("fillet LargeTop 5 --edges=2 --name=LargeFillet") && Run("matcap LargeFillet copper") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 3") &&
            Run("render sheet finalize Phase26_CylinderFillets");
        Panel.Expect("C++ circular-fillet proof commands complete without refusal", Rendered);
        Panel.Expect("C++ circular-fillet proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
