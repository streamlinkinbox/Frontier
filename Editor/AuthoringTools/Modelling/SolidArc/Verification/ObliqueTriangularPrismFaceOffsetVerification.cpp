//=============================================================================================================================================
// SolidArc · Phase 44 · bounded oblique triangular-prism face offset
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double GeneratorLength = 7.0;
constexpr double Offset = 1.4;
const Vec3 GeneratorDirection = Vec3(0.35, 0.20, 1.0).Normalised();
const std::vector<Vec3> BaseProfile{ { 0, 0, 0 }, { 5, 0, 0 }, { 1.5, 3.5, 0 } };

[[nodiscard]] Deliver<BrepBody> ObliquePrism() noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(BaseProfile, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, GeneratorDirection, GeneratorLength)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> VerticalTrianglePrism() noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(BaseProfile, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), GeneratorLength)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
    {
        const BrepFace& F = Body.Faces[I];
        const Vec3 N = Body.FaceNormal(I, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                       0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
        const double D = N.Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = I; }
    }
    return Best;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool HasVertex(const BrepBody& Body, Vec3 Point, double Tolerance = 1e-8) noexcept
{
    for (const BrepVertex& Vertex : Body.Vertices)
        if (Vertex.Point.Distance(Point) <= Tolerance) return true;
    return false;
}

[[nodiscard]] bool IsPlanarTriangleCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const int Loop = F.Loops.front();
    return Loop >= 0 && Loop < static_cast<int>(Body.Loops.size()) && Body.Loops[Loop].Coedges.size() == 3;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 44 · bounded oblique triangular-prism face offset");
    const auto SourceDeliver = ObliquePrism();
    Panel.Expect("The oblique triangular prism source is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has V6/E9/C18/L5/F5 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Source.Vertices.size() == 6 && Source.Edges.size() == 9 &&
                 Source.Coedges.size() == 18 && Source.Loops.size() == 5 && Source.Faces.size() == 5);
    Panel.Expect("The selected caps are planar triangular faces", IsPlanarTriangleCap(Source, Top, true) &&
                 IsPlanarTriangleCap(Source, Bottom, false) && Top != Bottom);
    Panel.Expect("The source generators are genuinely oblique", GeneratorDirection.X != 0.0 || GeneratorDirection.Y != 0.0);

    Panel.Section("Exact oblique face-offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetObliqueTriangularPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no oblique prism source");
    Panel.Expect("The selected cap produces a closed oblique triangular prism", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const double TriangleArea = 0.5 * 5.0 * 3.5;
        const double Expected = TriangleArea * (GeneratorDirection.Z * GeneratorLength + Offset);
        Panel.Expect("The offset retains V6/E9/C18/L5/F5 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows the oblique prism projection identity", std::fabs(R.Volume - Expected), 1e-7);
        Panel.Expect("The lower triangular profile is unchanged", HasVertex(Result.Payload, BaseProfile[0]) &&
                     HasVertex(Result.Payload, BaseProfile[1]) && HasVertex(Result.Payload, BaseProfile[2]));
        Panel.Expect("The upper cap moves exactly along its +Z normal", HasVertex(Result.Payload, BaseProfile[0] + GeneratorDirection * GeneratorLength + Vec3::UnitZ() * Offset) &&
                     HasVertex(Result.Payload, BaseProfile[1] + GeneratorDirection * GeneratorLength + Vec3::UnitZ() * Offset) &&
                     HasVertex(Result.Payload, BaseProfile[2] + GeneratorDirection * GeneratorLength + Vec3::UnitZ() * Offset));
        Panel.Expect("The separate reconstruction preserves the oblique source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public face-offset dispatcher reaches the oblique route", Dispatch && Dispatch.Payload.Validate().Solid());
    Panel.Expect("Only the upper cap is supported by this bounded route", !FaceEditSolver::OffsetObliqueTriangularPrism(Source, Bottom, Offset));

    Panel.Section("Oblique-prism offset refusal boundaries");
    const auto Vertical = VerticalTrianglePrism();
    Panel.Expect("A vertical triangular prism remains outside the oblique route", Vertical && !FaceEditSolver::OffsetObliqueTriangularPrism(Vertical.Payload, FaceToward(Vertical.Payload, Vec3::UnitZ()), Offset));
    const auto Box = BrepBody::Box({ -3, -3, 0 }, { 3, 3, GeneratorLength });
    Panel.Expect("A rectangular box remains outside the oblique triangular route", Box && !FaceEditSolver::OffsetObliqueTriangularPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 3.0, GeneratorLength);
    Panel.Expect("A curved cylinder refuses the oblique triangular route", Cylinder && !FaceEditSolver::OffsetObliqueTriangularPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Side, zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetObliqueTriangularPrism(Source, FaceToward(Source, Vec3::UnitX()), Offset) &&
                 !FaceEditSolver::OffsetObliqueTriangularPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetObliqueTriangularPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetObliqueTriangularPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed oblique prism refuses transactionally", !FaceEditSolver::OffsetObliqueTriangularPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the oblique source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct non-axis-aligned face-offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase44_ObliqueTriangularPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("ObliqueSharpPrism", Source.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ObliqueOffsetPrism", Result.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase44_ObliqueTriangularPrismFaceOffset");
    Panel.Expect("The oblique-prism proof render completes", Rendered);
    Panel.Expect("The oblique-prism proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
