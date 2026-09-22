//=============================================================================================================================================
// SolidArc · Stage 4b · bounded coaxial conical-apex spherical fillet
//
// One explicit right-circular cone receives one exact spherical cap at its apex. This is not a
// general vertex selector or a general mixed-support apex fillet.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>

using namespace Frontier;

namespace
{
[[nodiscard]] bool ExteriorFaceNormals(const BrepBody& Body, const ConeApexFilletSpecification& Specification) noexcept
{
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Centre = Specification.Base + Axis *
        (Specification.Height - Specification.FilletRadius * std::hypot(Specification.Height, Specification.BaseRadius) /
         Specification.BaseRadius);
    int Cones = 0, Spheres = 0, Planes = 0;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 P = Surface.Sample(U, V);
        const Vec3 N = Body.FaceNormal(static_cast<int>(Face), U, V).Normalised();
        if (!std::isfinite(N.X) || !std::isfinite(N.Y) || !std::isfinite(N.Z)) return false;
        if (Surface.Classification == SurfaceClassification::Cone)
        {
            const Vec3 Radial = (P - Specification.Base - Axis * (P - Specification.Base).Dot(Axis)).Normalised();
            const Vec3 Expected = (Radial * Specification.Height + Axis * Specification.BaseRadius).Normalised();
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
        else return false;
    }
    return Cones == 1 && Spheres == 1 && Planes == 1;
}

struct DerivedApex
{
    double Slant = 0.0;
    double CentreHeight = 0.0;
    double SeamRadius = 0.0;
    double SeamHeight = 0.0;
    double TopHeight = 0.0;
};

[[nodiscard]] DerivedApex Derive(const ConeApexFilletSpecification& S) noexcept
{
    const double L = std::hypot(S.Height, S.BaseRadius);
    const double Zc = S.Height - S.FilletRadius * L / S.BaseRadius;
    return { L, Zc, S.FilletRadius * S.Height / L,
             S.Height - S.FilletRadius * S.Height * S.Height / (S.BaseRadius * L), Zc + S.FilletRadius };
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded coaxial conical-apex spherical fillet");
    Panel.Section("Analytic tangent spherical cap");

    ConeApexFilletSpecification Specification;
    Specification.Base = { 0, 0, 0 };
    Specification.Axis = { 0, 0, 1 };
    Specification.BaseRadius = 4.0;
    Specification.Height = 6.0;
    Specification.FilletRadius = 0.5;

    const DerivedApex D = Derive(Specification);
    const double MaximumRadius = Specification.Height * Specification.BaseRadius / D.Slant;
    const Deliver<BrepBody> Result = BlendSolver::ReconstructConeApexFillet(Specification);
    Panel.Expect("The bounded cone-apex fillet commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Within("The analytic seam radius is exact", D.SeamRadius - 0.41602514716892186, 1e-12);
    Panel.Within("The spherical centre height is exact", D.CentreHeight - 5.098612181134002, 1e-12);
    Panel.Expect("The requested sphere remains inside the analytic fit bound", Specification.FilletRadius < MaximumRadius);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double FrustumVolume = ScalarCriteria::Pi * D.SeamHeight *
            (Specification.BaseRadius * Specification.BaseRadius + Specification.BaseRadius * D.SeamRadius + D.SeamRadius * D.SeamRadius) / 3.0;
        const double CapHeight = D.TopHeight - D.SeamHeight;
        const double SphericalCapVolume = ScalarCriteria::Pi * CapHeight * CapHeight *
            (3.0 * Specification.FilletRadius - CapHeight) / 3.0;
        const double ExpectedVolume = FrustumVolume + SphericalCapVolume;
        const Vec3 Radial = Specification.Axis.Normalised().Cross(Vec3::UnitX()).Normalised();
        const Vec3 ConeNormal = (Radial * Specification.Height + Specification.Axis.Normalised() * Specification.BaseRadius).Normalised();
        const Vec3 CapNormal = (Radial * D.SeamRadius + Specification.Axis.Normalised() *
                                (D.SeamHeight - D.CentreHeight)).Normalised();
        Panel.Expect("The cone, spherical cap and disk sew to deterministic V4/E5/F3/L3 topology",
                     Report.Vertices == 4 && Report.Edges == 5 && Report.Faces == 3 && Report.Loops == 3 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The frustum-plus-spherical-cap volume is analytic",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Within("The cone and spherical cap meet G1 at the tangent circle", 1.0 - ConeNormal.Dot(CapNormal), 1e-12);
        Panel.Expect("The conical apex fillet has outward-facing normals", ExteriorFaceNormals(Result.Payload, Specification));
        Panel.Within("The rounded apex reaches the derived spherical pole", Result.Payload.Bounds().High.Z - D.TopHeight, 1e-9);
        Panel.Expect("The result exposes one cone, one sphere cap and one planar base", Result.Payload.Faces[0].Surface.Classification == SurfaceClassification::Cone &&
                     Result.Payload.Faces[1].Surface.Classification == SurfaceClassification::Sphere &&
                     Result.Payload.Faces[2].Surface.Classification == SurfaceClassification::Plane);
    }

    Panel.Section("Transactional fit and support refusals");
    ConeApexFilletSpecification Bad = Specification;
    Bad.FilletRadius = 0.0;
    Panel.Expect("A zero apex radius refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.FilletRadius = -0.25;
    Panel.Expect("A negative apex radius refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.FilletRadius = MaximumRadius;
    Panel.Expect("A sphere at the limiting fit bound refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.FilletRadius = MaximumRadius * 1.1;
    Panel.Expect("A sphere consuming the cone refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.BaseRadius = 0.0;
    Panel.Expect("A zero cone base radius refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.Height = 0.0;
    Panel.Expect("A zero cone height refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.Axis = { 0, 0, 0 };
    Panel.Expect("A degenerate cone axis refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Bad = Specification;
    Bad.Axis.X = std::numeric_limits<double>::quiet_NaN();
    Panel.Expect("A non-finite cone axis refuses", !BlendSolver::ReconstructConeApexFillet(Bad));
    Panel.Expect("The accepted apex result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/rounded apex proof");
    const Deliver<BrepBody> Sharp = BrepBody::Cone(Specification.Base, Specification.Axis,
                                                    Specification.BaseRadius, 0.0, Specification.Height);
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38b_ConeApexFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpCone", Sharp.Payload.Transformed(Mat4::Translation({ -6, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("RoundedApex", Result.Payload.Transformed(Mat4::Translation({ 6, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38b_ConeApexFillet");
    Panel.Expect("The sharp/rounded apex exterior proof render completes", Rendered);
    Panel.Expect("The cone-apex proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
