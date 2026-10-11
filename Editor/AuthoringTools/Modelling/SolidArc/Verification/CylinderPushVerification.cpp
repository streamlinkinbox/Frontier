//============================================================================================================================================
// 📦 Verification/CylinderPushVerification.cpp — Phase 27: exact direct cap pushes on native right cylinders
//============================================================================================================================================
// A planar cylinder cap is a direct-modelling face operation, not a near-coincident extrusion Boolean. The bounded
// route reconstructs an exact cylinder with the selected cap translated along its actual outward normal.
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] int CapFaceAt(const BrepBody& Body, Vec3 Origin, Vec3 Axis, double Height) noexcept
{
    Axis = Axis.Normalised();
    for (size_t F = 0; F < Body.Faces.size(); ++F)
    {
        const NurbsSurface& Surface = Body.Faces[F].Surface;
        if (Surface.Classification != SurfaceClassification::Plane) continue;
        Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()), 0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
        if (std::fabs((Point - Origin).Dot(Axis) - Height) < 1e-9) return static_cast<int>(F);
    }
    return -1;
}

[[nodiscard]] int CylinderSide(const BrepBody& Body) noexcept
{
    for (size_t F = 0; F < Body.Faces.size(); ++F)
        if (Body.Faces[F].Surface.Classification == SurfaceClassification::Cylinder) return static_cast<int>(F);
    return -1;
}

[[nodiscard]] bool ExactCylinder(const BrepBody& Body, Vec3 Base, Vec3 Axis, double Radius, double Height) noexcept
{
    if (!Body.Validate().Solid() || Body.Vertices.size() != 2 || Body.Edges.size() != 3 || Body.Coedges.size() != 6 || Body.Loops.size() != 3 || Body.Faces.size() != 3) return false;
    int Side = CylinderSide(Body); if (Side < 0) return false;
    const NurbsSurface& Surface = Body.Faces[Side].Surface;
    Axis = Axis.Normalised();
    if (Surface.Origin.Distance(Base) > 1e-10 || Surface.Axis.Normalised().Dot(Axis) < 1.0 - 1e-12 || std::fabs(Surface.RadiusMajor - Radius) > 1e-12 || std::fabs(Surface.RadiusMinor - Radius) > 1e-12) return false;
    double U0 = Surface.DomainStartU(), U1 = Surface.DomainEndU(), V0 = Surface.DomainStartV(), V1 = Surface.DomainEndV();
    for (int I = 0; I < 7; ++I)
        for (int J = 0; J < 5; ++J)
        {
            Vec3 P = Surface.Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 6.0), V0 + (V1 - V0) * (static_cast<double>(J) / 4.0));
            double Along = (P - Base).Dot(Axis);
            Vec3 Radial = P - (Base + Axis * Along);
            if (std::fabs(Radial.Length() - Radius) > 1e-10 || std::fabs(Along - Height * (static_cast<double>(J) / 4.0)) > 1e-10) return false;
        }
    return CapFaceAt(Body, Base, Axis, 0.0) >= 0 && CapFaceAt(Body, Base, Axis, Height) >= 0;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 27 · Cylinder Push Verification — exact direct cap-face offsets");
    constexpr double Radius = 10.0, Height = 20.0;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const BrepBody Cylinder = BrepBody::Cylinder(Base, Axis, Radius, Height).Payload;
    const int Bottom = CapFaceAt(Cylinder, Base, Axis, 0.0), Top = CapFaceAt(Cylinder, Base, Axis, Height), Side = CylinderSide(Cylinder);

    Panel.Section("Both native circular caps push exactly along their outward normals");
    Panel.Expect("Native cylinder exposes lower cap, side, and upper cap faces", Bottom >= 0 && Side >= 0 && Top >= 0 && Bottom != Top);
    for (const auto& Case : { std::tuple{ "upper outward", Top, 5.0, Base, 25.0 }, std::tuple{ "upper inward", Top, -5.0, Base, 15.0 },
                              std::tuple{ "lower outward", Bottom, 5.0, Vec3{ 0, 0, -5 }, 25.0 }, std::tuple{ "lower inward", Bottom, -5.0, Vec3{ 0, 0, 5 }, 15.0 } })
    {
        const auto& [Name, Face, Distance, ExpectedBase, ExpectedHeight] = Case;
        Deliver<BrepBody> Result = BlendSolver::PushFace(Cylinder, Face, Distance);
        Panel.Expect((std::string("The ") + Name + " cap push remains a closed manifold solid").c_str(), Result && Result.Payload.Validate().Solid());
        Panel.Expect((std::string("The ") + Name + " cap push is the exact expected cylinder").c_str(), Result && ExactCylinder(Result.Payload, ExpectedBase, Axis, Radius, ExpectedHeight));
    }

    Panel.Section("Axis and construction direction are geometric, not display conventions");
    {
        const Vec3 ObliqueBase{ 2, -4, 3 }, ObliqueAxis{ 2, -1, 4 }, UnitAxis = ObliqueAxis.Normalised();
        constexpr double ObliqueRadius = 8.0, ObliqueHeight = 15.0;
        const BrepBody Oblique = BrepBody::Cylinder(ObliqueBase, ObliqueAxis, ObliqueRadius, ObliqueHeight).Payload;
        const int ObliqueTop = CapFaceAt(Oblique, ObliqueBase, ObliqueAxis, ObliqueHeight);
        Deliver<BrepBody> Up = ObliqueTop >= 0 ? BlendSolver::PushFace(Oblique, ObliqueTop, 2.0) : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique cap face not found");
        Panel.Expect("A non-unit oblique axis exposes its upper cap face", ObliqueTop >= 0);
        Panel.Expect("The oblique outward cap push remains a closed manifold solid", Up && Up.Payload.Validate().Solid());
        Panel.Expect("The oblique outward cap push preserves the exact expected cylinder", Up && ExactCylinder(Up.Payload, ObliqueBase, UnitAxis, ObliqueRadius, 17.0));

        const BrepBody Reversed = BrepBody::Cylinder(Base, Axis, Radius, -Height).Payload;
        const int FarCap = CapFaceAt(Reversed, Base, Axis, -Height);
        Deliver<BrepBody> FarOut = FarCap >= 0 ? BlendSolver::PushFace(Reversed, FarCap, 4.0) : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "reversed cap face not found");
        Panel.Expect("A negative-height cylinder exposes the far cap face", FarCap >= 0);
        Panel.Expect("The reversed-direction cap push remains a closed manifold solid", FarOut && FarOut.Payload.Validate().Solid());
        Panel.Expect("The reversed-direction outward push moves the low cap and preserves an exact cylinder", FarOut && ExactCylinder(FarOut.Payload, { 0, 0, -24 }, Axis, Radius, 24.0));
    }

    Panel.Section("Radial native-cylinder support and collapsing selections");
    Deliver<BrepBody> SideOffset = BlendSolver::PushFace(Cylinder, Side, 2.0);
    Panel.Expect("The cylindrical side takes its exact direct radial-offset route", SideOffset && ExactCylinder(SideOffset.Payload, Base, Axis, Radius + 2.0, Height));
    Panel.Expect("An inward push that consumes the complete cylinder height is refused", !BlendSolver::PushFace(Cylinder, Top, -Height));

    Panel.Section("C++ console proof: outward and inward direct cylinder-cap face pushes");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase27_CylinderPushes.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto Run = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            Run("gizmo off") &&
            Run("reset") && Run("view iso") && Run("cylinder (-13,0,0) 8 20 --name=TopReference") && Run("matcap TopReference steel") && Run("cylinder (13,0,0) 8 20 --name=TopSource") && Run("push TopSource 5 --face=2 --name=TopRaised") && Run("matcap TopRaised gold") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 0") &&
            Run("reset") && Run("view bottom") && Run("view orbit 45 25") && Run("cylinder (-13,0,0) 8 20 --name=BottomReference") && Run("matcap BottomReference steel") && Run("cylinder (13,0,0) 8 20 --name=BottomSource") && Run("push BottomSource 5 --face=2 --name=BottomRaised") && Run("matcap BottomRaised plastic-blue") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 1") &&
            Run("reset") && Run("view iso") && Run("cylinder (-13,0,0) 8 20 --name=InsetReference") && Run("matcap InsetReference steel") && Run("cylinder (13,0,0) 8 20 --name=TopInsetSource") && Run("push TopInsetSource -8 --face=2 --name=TopInset") && Run("matcap TopInset copper") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 2") &&
            Run("reset") && Run("view iso") && Run("cylinder (-13,0,0) 8 20 --axis=(2,-1,4) --name=ObliqueReference") && Run("matcap ObliqueReference steel") && Run("cylinder (13,0,0) 8 20 --axis=(2,-1,4) --name=ObliqueSource") && Run("push ObliqueSource 4 --face=2 --name=ObliqueRaised") && Run("matcap ObliqueRaised gold") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 3") &&
            Run("render sheet finalize Phase27_CylinderPushes");
        Panel.Expect("C++ cylinder-cap push proof commands complete without refusal", Rendered);
        Panel.Expect("C++ cylinder-cap push proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
