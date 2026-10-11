//============================================================================================================================================
// 📦 Verification/CylinderSidePushVerification.cpp — Phase 28: exact radial direct-face offsets on native right cylinders
//============================================================================================================================================
// Offsetting a cylindrical face by distance d is an exact radius edit R→R+d. It must not be sent through a planar-face
// push or a coincident Boolean; this verifier samples the rebuilt NURBS cylinder itself.
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
    return true;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 28 · Cylinder Side Push Verification — exact radial direct-face offsets");
    constexpr double Radius = 10.0, Height = 20.0;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const BrepBody Cylinder = BrepBody::Cylinder(Base, Axis, Radius, Height).Payload;
    const int Side = CylinderSide(Cylinder);

    Panel.Section("Outward and inward cylindrical-face pushes are exact radius edits");
    Panel.Expect("Native cylinder exposes its classified cylindrical side face", Side >= 0);
    for (const auto& Case : { std::pair{ "outward", 3.0 }, std::pair{ "inward", -3.0 } })
    {
        Deliver<BrepBody> Result = BlendSolver::PushFace(Cylinder, Side, Case.second);
        Panel.Expect((std::string("The ") + Case.first + " side push remains a closed manifold solid").c_str(), Result && Result.Payload.Validate().Solid());
        Panel.Expect((std::string("The ") + Case.first + " side push has exact R=" + std::to_string(Radius + Case.second) + " cylinder geometry").c_str(), Result && ExactCylinder(Result.Payload, Base, Axis, Radius + Case.second, Height));
    }

    Panel.Section("Axis and construction direction preserve the radial-face semantics");
    {
        const Vec3 ObliqueBase{ 2, -4, 3 }, ObliqueAxis{ 2, -1, 4 };
        constexpr double ObliqueRadius = 8.0, ObliqueHeight = 15.0;
        const BrepBody Oblique = BrepBody::Cylinder(ObliqueBase, ObliqueAxis, ObliqueRadius, ObliqueHeight).Payload;
        Deliver<BrepBody> Wider = BlendSolver::PushFace(Oblique, CylinderSide(Oblique), 2.0);
        Panel.Expect("An oblique non-unit cylinder side pushes to a closed manifold solid", Wider && Wider.Payload.Validate().Solid());
        Panel.Expect("The oblique side push preserves its axis/base/height and changes only radius", Wider && ExactCylinder(Wider.Payload, ObliqueBase, ObliqueAxis, 10.0, ObliqueHeight));

        const BrepBody Reversed = BrepBody::Cylinder(Base, Axis, Radius, -Height).Payload;
        Deliver<BrepBody> ReversedWider = BlendSolver::PushFace(Reversed, CylinderSide(Reversed), 2.0);
        Panel.Expect("A reversed-height cylinder side push remains a closed manifold solid", ReversedWider && ReversedWider.Payload.Validate().Solid());
        Panel.Expect("The reversed-height side push reconstructs the same physical span at the new radius", ReversedWider && ExactCylinder(ReversedWider.Payload, { 0, 0, -20 }, Axis, 12.0, Height));
    }

    Panel.Section("Scope and feasibility refuse cleanly");
    Panel.Expect("An inward push that reaches the cylinder axis is refused", !BlendSolver::PushFace(Cylinder, Side, -Radius));
    Panel.Expect("A zero radial push is refused", !BlendSolver::PushFace(Cylinder, Side, 0.0));
    {
        Deliver<NurbsCurve> Circle = NurbsCurve::Circle(Base, Axis, Radius);
        Deliver<NurbsSurface> Surface = Circle ? NurbsSurface::Extrusion(Circle.Payload, Axis, Height) : Deliver<NurbsSurface>::Reject(RefusalReason::Unsupported, "source circle failed");
        std::vector<NurbsSurface> Faces; if (Surface) Faces.push_back(std::move(Surface.Payload));
        Deliver<BrepBody> Extrusion = BrepBody::Sew(Faces);
        int ExtrusionSide = Extrusion ? CylinderSide(Extrusion.Payload) : -1;
        Panel.Expect("The equivalent circular extrusion remains a valid solid with an extrusion side", Extrusion && Extrusion.Payload.Validate().Solid() && ExtrusionSide < 0);
        Panel.Expect("The circular-extrusion side does not enter the native-cylinder radial-offset route", !(Extrusion && BlendSolver::PushFace(Extrusion.Payload, 0, 2.0)));
    }

    Panel.Section("C++ console proof: exact radial direct-face offsets");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase28_CylinderSidePushes.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto Run = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            Run("gizmo off") &&
            Run("reset") && Run("view iso") && Run("cylinder (-15,0,0) 8 20 --name=OutwardReference") && Run("matcap OutwardReference steel") && Run("cylinder (15,0,0) 8 20 --name=OutwardSource") && Run("push OutwardSource 3 --face=0 --name=OutwardOffset") && Run("matcap OutwardOffset gold") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 0") &&
            Run("reset") && Run("view iso") && Run("cylinder (-15,0,0) 8 20 --name=InwardReference") && Run("matcap InwardReference steel") && Run("cylinder (15,0,0) 8 20 --name=InwardSource") && Run("push InwardSource -3 --face=0 --name=InwardOffset") && Run("matcap InwardOffset copper") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 1") &&
            Run("reset") && Run("view top") && Run("cylinder (-15,0,0) 8 20 --name=TopReference") && Run("matcap TopReference steel") && Run("cylinder (15,0,0) 8 20 --name=TopSource") && Run("push TopSource 5 --face=0 --name=TopOffset") && Run("matcap TopOffset plastic-blue") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 2") &&
            Run("reset") && Run("view iso") && Run("cylinder (-15,0,0) 8 20 --axis=(2,-1,4) --name=ObliqueReference") && Run("matcap ObliqueReference steel") && Run("cylinder (15,0,0) 8 20 --axis=(2,-1,4) --name=ObliqueSource") && Run("push ObliqueSource 3 --face=0 --name=ObliqueOffset") && Run("matcap ObliqueOffset gold") && Run("view fit") && Run("view dolly 0.78") && Run("render sheet 3") &&
            Run("render sheet finalize Phase28_CylinderSidePushes");
        Panel.Expect("C++ cylinder-side push proof commands complete without refusal", Rendered);
        Panel.Expect("C++ cylinder-side push proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
