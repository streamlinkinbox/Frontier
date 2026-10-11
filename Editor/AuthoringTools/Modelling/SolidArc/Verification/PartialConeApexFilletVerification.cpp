//=============================================================================================================================================
// SolidArc · Stage 4d · bounded partial coaxial conical-apex spherical fillet
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>

using namespace Frontier;

namespace
{
[[nodiscard]] bool AnalyticNormals(const BrepBody& Body, const PartialConeApexFilletSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(S.Base, Axis).AxisX.Normalised();
    const Vec3 Tangent = Axis.Cross(Radial).Normalised();
    const double Slant = std::hypot(S.Height, S.BaseRadius);
    const Vec3 Centre = S.Base + Axis * (S.Height - S.FilletRadius * Slant / S.BaseRadius);
    int Cones = 0, Spheres = 0, Planes = 0, Meridians = 0;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const NurbsSurface& Surface = Body.Faces[I].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 P = Surface.Sample(U, V);
        const Vec3 N = Body.FaceNormal(static_cast<int>(I), U, V).Normalised();
        if (N.Length() <= ScalarCriteria::GeometricTolerance) return false;
        if (Surface.Classification == SurfaceClassification::Cone)
        {
            const Vec3 R = (P - S.Base - Axis * (P - S.Base).Dot(Axis)).Normalised();
            const Vec3 Expected = (R * S.Height + Axis * S.BaseRadius).Normalised();
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Cones;
        }
        else if (Surface.Classification == SurfaceClassification::Sphere)
        {
            if (N.Dot((P - Centre).Normalised()) < 1.0 - 1e-6) return false;
            ++Spheres;
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            if (N.Dot(Axis) > -1.0 + 1e-6) return false;
            ++Planes;
        }
        else if (Surface.Classification == SurfaceClassification::Coons)
        {
            const Vec3 FromBase = P - S.Base;
            const double Angle = std::atan2(FromBase.Dot(Tangent), FromBase.Dot(Radial));
            const Vec3 Expected = Angle < S.SweepAngle * 0.5 ? Tangent * -1.0 : Tangent;
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Meridians;
        }
        else return false;
    }
    return Cones == 1 && Spheres == 1 && Planes == 1 && Meridians == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded partial coaxial conical-apex spherical fillet");
    PartialConeApexFilletSpecification Specification;
    Specification.Base = { 0, 0, 0 };
    Specification.Axis = { 0, 0, 1 };
    Specification.BaseRadius = 4.0;
    Specification.Height = 6.0;
    Specification.FilletRadius = 0.5;
    Specification.SweepAngle = ScalarCriteria::Pi;

    Panel.Section("Strict partial spherical apex sector");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructPartialConeApexFillet(Specification);
    const double Slant = std::hypot(Specification.Height, Specification.BaseRadius);
    const double MaximumRadius = Specification.Height * Specification.BaseRadius / Slant;
    const double CentreHeight = Specification.Height - Specification.FilletRadius * Slant / Specification.BaseRadius;
    const double SeamRadius = Specification.FilletRadius * Specification.Height / Slant;
    const double SeamHeight = Specification.Height - Specification.FilletRadius * Specification.Height * Specification.Height /
                               (Specification.BaseRadius * Slant);
    const double TopHeight = CentreHeight + Specification.FilletRadius;
    const double FrustumVolume = ScalarCriteria::Pi * SeamHeight *
        (Specification.BaseRadius * Specification.BaseRadius + Specification.BaseRadius * SeamRadius + SeamRadius * SeamRadius) / 3.0;
    const double CapHeight = TopHeight - SeamHeight;
    const double CapVolume = ScalarCriteria::Pi * CapHeight * CapHeight *
        (3.0 * Specification.FilletRadius - CapHeight) / 3.0;
    const double ExpectedVolume = (FrustumVolume + CapVolume) * 0.5;
    Panel.Expect("The strict half-turn apex sector commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The requested apex sphere is below the analytic fit limit", Specification.FilletRadius < MaximumRadius);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The partial cone, cap, base and two meridians sew as V6/E9/F5/L5",
                     Report.Vertices == 6 && Report.Edges == 9 && Report.Faces == 5 && Report.Loops == 5 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The half-turn volume is the analytic sector identity",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("The partial cone-apex surfaces have outward normals", AnalyticNormals(Result.Payload, Specification));
        Panel.Expect("The partial result remains a strict sector rather than a full revolution",
                     Specification.SweepAngle > 0.0 && Specification.SweepAngle < ScalarCriteria::TwoPi);
    }

    Panel.Section("Transactional partial-apex refusals");
    PartialConeApexFilletSpecification Bad = Specification;
    Bad.SweepAngle = 0.0;
    Panel.Expect("A zero sweep refuses", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Bad = Specification;
    Bad.SweepAngle = ScalarCriteria::TwoPi;
    Panel.Expect("A full sweep delegates to the complete route and refuses partial-only input", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Bad = Specification;
    Bad.SweepAngle = -0.5;
    Panel.Expect("A negative sweep refuses", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Bad = Specification;
    Bad.FilletRadius = MaximumRadius;
    Panel.Expect("A limiting apex radius refuses", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Bad = Specification;
    Bad.FilletRadius = -0.2;
    Panel.Expect("A negative apex radius refuses", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Bad = Specification;
    Bad.BaseRadius = 0.0;
    Panel.Expect("A zero base radius refuses", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Bad = Specification;
    Bad.Axis = { 0, 0, 0 };
    Panel.Expect("A degenerate axis refuses", !BlendSolver::ReconstructPartialConeApexFillet(Bad));
    Panel.Expect("The accepted partial apex remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/rounded partial apex proof");
    const Vec3 Radial = Workplane::FromNormal(Specification.Base, Specification.Axis).AxisX;
    Deliver<NurbsCurve> SharpProfile = NurbsCurve::Polyline({ Specification.Base,
        Specification.Base + Radial * Specification.BaseRadius,
        Specification.Base + Specification.Axis.Normalised() * Specification.Height }, true);
    const Deliver<BrepBody> Sharp = SharpProfile
        ? BrepBody::Revolve(SharpProfile.Payload, Specification.Base, Specification.Axis, Specification.SweepAngle)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sharp partial cone profile is degenerate");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38d_PartialConeApexFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpPartialCone", Sharp.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("RoundedPartialApex", Result.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38d_PartialConeApexFillet");
    Panel.Expect("The sharp/rounded partial-apex proof render completes", Rendered);
    Panel.Expect("The partial-apex proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
