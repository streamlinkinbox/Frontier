//=============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/ConeCapPushVerification.cpp — exact native-frustum cap offsets
//=============================================================================================================================================
// A cap push keeps the selected plane on the original infinite conical support. The supported shape is a full native
// frustum; apex cones and general trimmed/free-form cones remain explicit refusals.
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] int ConeSide(const BrepBody& Body) noexcept
{
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Cone) return static_cast<int>(Face);
    return -1;
}

[[nodiscard]] int CapFaceAt(const BrepBody& Body, Vec3 Base, Vec3 Axis, double Along) noexcept
{
    Axis = Axis.Normalised();
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        if (Surface.Classification != SurfaceClassification::Plane) continue;
        Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                    0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
        if (std::fabs((Point - Base).Dot(Axis) - Along) < 1e-8) return static_cast<int>(Face);
    }
    return -1;
}

[[nodiscard]] bool Near(double A, double B, double Relative = 1e-9) noexcept
{
    return std::fabs(A - B) <= Relative * std::max({ 1.0, std::fabs(A), std::fabs(B) });
}

[[nodiscard]] bool ExactCone(const BrepBody& Body, Vec3 Base, Vec3 Axis,
                             double RadiusFoot, double RadiusTop, double Height) noexcept
{
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Body.Vertices.size() != 2 || Body.Edges.size() != 3 ||
        Body.Coedges.size() != 6 || Body.Loops.size() != 3 || Body.Faces.size() != 3) return false;

    const int SideFace = ConeSide(Body);
    if (SideFace < 0) return false;
    const NurbsSurface& Side = Body.Faces[SideFace].Surface;
    Axis = Axis.Normalised();
    if (Side.Origin.Distance(Base) > 1e-9 || Side.Axis.Normalised().Dot(Axis) < 1.0 - 1e-12 ||
        !Near(Side.RadiusMajor, RadiusFoot, 1e-11) || !Near(Side.RadiusMinor, RadiusTop, 1e-11)) return false;

    const double U0 = Side.DomainStartU(), U1 = Side.DomainEndU();
    const double V0 = Side.DomainStartV(), V1 = Side.DomainEndV();
    for (int I = 0; I < 9; ++I)
        for (int J = 0; J < 5; ++J)
        {
            const double U = U0 + (U1 - U0) * (static_cast<double>(I) / 8.0);
            const double Fraction = static_cast<double>(J) / 4.0;
            Vec3 Point = Side.Sample(U, V0 + (V1 - V0) * Fraction);
            double Along = (Point - Base).Dot(Axis);
            double Radius = (Point - (Base + Axis * Along)).Length();
            if (!Near(Along, Height * Fraction) ||
                !Near(Radius, ScalarCriteria::Lerp(RadiusFoot, RadiusTop, Fraction))) return false;
        }

    if (CapFaceAt(Body, Base, Axis, 0.0) < 0 || CapFaceAt(Body, Base, Axis, Height) < 0) return false;
    const double AnalyticVolume = ScalarCriteria::Pi * std::fabs(Height) *
        (RadiusFoot * RadiusFoot + RadiusFoot * RadiusTop + RadiusTop * RadiusTop) / 3.0;
    return std::fabs(Report.Volume - AnalyticVolume) <= AnalyticVolume * 1e-3;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 30 · Cone Cap Push Verification — exact cap-face offsets");
    constexpr double RadiusFoot = 10.0, RadiusTop = 4.0, Height = 20.0, Distance = 2.0;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const BrepBody Cone = BrepBody::Cone(Base, Axis, RadiusFoot, RadiusTop, Height).Payload;
    const int Lower = CapFaceAt(Cone, Base, Axis, 0.0), Upper = CapFaceAt(Cone, Base, Axis, Height);

    Panel.Section("Both cap planes remain on the exact supporting cone");
    Panel.Expect("Native frustum exposes two distinct planar cap faces", Lower >= 0 && Upper >= 0 && Lower != Upper);
    struct Case
    {
        const char* Name;
        int Face;
        double Push;
        Vec3 ExpectedBase;
        double ExpectedFoot, ExpectedTop, ExpectedHeight;
    };
    const std::vector<Case> Cases{
        { "Upper outward", Upper, Distance, Base, 10.0, 3.4, 22.0 },
        { "Lower outward", Lower, Distance, { 0, 0, -2 }, 10.6, 4.0, 22.0 },
        { "Upper inward", Upper, -Distance, Base, 10.0, 4.6, 18.0 },
        { "Lower inward", Lower, -Distance, { 0, 0, 2 }, 9.4, 4.0, 18.0 },
    };
    for (const Case& Test : Cases)
    {
        Deliver<BrepBody> Result = BlendSolver::PushFace(Cone, Test.Face, Test.Push);
        Panel.Expect((std::string(Test.Name) + " push has exact base, axis, radii, height, caps, samples, topology, and volume").c_str(),
                     Result && ExactCone(Result.Payload, Test.ExpectedBase, Axis,
                                         Test.ExpectedFoot, Test.ExpectedTop, Test.ExpectedHeight));
    }

    Panel.Section("Axis, construction direction, and slope sign are geometric");
    const Vec3 ObliqueBase{ 2, -4, 3 }, ObliqueAxis{ 2, -1, 4 }, ObliqueUnit = ObliqueAxis.Normalised();
    constexpr double ObliqueFoot = 8.0, ObliqueTop = 3.0, ObliqueHeight = 15.0;
    const BrepBody Oblique = BrepBody::Cone(ObliqueBase, ObliqueAxis, ObliqueFoot, ObliqueTop, ObliqueHeight).Payload;
    const int ObliqueUpper = CapFaceAt(Oblique, ObliqueBase, ObliqueUnit, ObliqueHeight);
    Panel.Expect("A non-unit oblique cone exposes its upper cap", ObliqueUpper >= 0);
    Deliver<BrepBody> ObliqueResult = BlendSolver::PushFace(Oblique, ObliqueUpper, Distance);
    Panel.Expect("Oblique upper-cap push retains the exact support and unit construction axis",
                 ObliqueResult && ExactCone(ObliqueResult.Payload, ObliqueBase, ObliqueUnit, 8.0, 7.0 / 3.0, 17.0));

    // Negative construction height is legal in NurbsSurface::Cone. Canonical direct edits run from the geometric low
    // ring to the high ring so the same outward semantics apply without depending on the source parameter direction.
    const BrepBody Reversed = BrepBody::Cone(Base, Axis, RadiusFoot, RadiusTop, -Height).Payload;
    const int ReversedFar = CapFaceAt(Reversed, Base, Axis, -Height), ReversedNear = CapFaceAt(Reversed, Base, Axis, 0.0);
    Panel.Expect("A negative-height cone exposes both geometric cap planes", ReversedFar >= 0 && ReversedNear >= 0);
    Deliver<BrepBody> ReversedNearOut = BlendSolver::PushFace(Reversed, ReversedNear, Distance);
    Deliver<BrepBody> ReversedFarOut = BlendSolver::PushFace(Reversed, ReversedFar, Distance);
    Panel.Expect("Negative-height near-cap push follows the same infinite support",
                 ReversedNearOut && ExactCone(ReversedNearOut.Payload, { 0, 0, -20 }, Axis, 4.0, 10.6, 22.0));
    Panel.Expect("Negative-height far-cap push follows the same infinite support",
                 ReversedFarOut && ExactCone(ReversedFarOut.Payload, { 0, 0, -22 }, Axis, 3.4, 10.0, 22.0));
    const double ReversedOffsetScale = std::sqrt(1.0 + 0.3 * 0.3);
    Deliver<BrepBody> ReversedSideOut = BlendSolver::PushFace(Reversed, ConeSide(Reversed), Distance);
    Panel.Expect("Negative-height conical side also takes the exact normal-offset route",
                 ReversedSideOut && ExactCone(ReversedSideOut.Payload, { 0, 0, -20 }, Axis,
                                               4.0 + Distance * ReversedOffsetScale,
                                               10.0 + Distance * ReversedOffsetScale, 20.0));

    const BrepBody Expanding = BrepBody::Cone(Base, Axis, 4.0, 10.0, Height).Payload;
    const int ExpandingLower = CapFaceAt(Expanding, Base, Axis, 0.0);
    Deliver<BrepBody> ExpandingResult = BlendSolver::PushFace(Expanding, ExpandingLower, Distance);
    Panel.Expect("An expanding frustum preserves its positive slope during a lower-cap push",
                 ExpandingResult && ExactCone(ExpandingResult.Payload, { 0, 0, -2 }, Axis, 3.4, 10.0, 22.0));

    Panel.Section("Degenerate continuations refuse rather than crossing an apex");
    Panel.Expect("An inward push consuming the complete height refuses", !BlendSolver::PushFace(Cone, Upper, -Height));
    const double RadiusCollapse = RadiusTop / 0.3 + 1.0;
    Panel.Expect("A tapering upper-cap continuation that crosses the apex refuses",
                 !BlendSolver::PushFace(Cone, Upper, RadiusCollapse));
    Panel.Expect("An expanding lower-cap continuation that crosses the apex refuses",
                 !BlendSolver::PushFace(Expanding, ExpandingLower, RadiusCollapse));
    const BrepBody Apex = BrepBody::Cone(Base, Axis, RadiusFoot, 0.0, Height).Payload;
    const int ApexLower = CapFaceAt(Apex, Base, Axis, 0.0);
    Panel.Expect("An apex cone stays outside the full-frustum direct route and refuses cleanly",
                 ApexLower >= 0 && !BlendSolver::PushFace(Apex, ApexLower, Distance));

    Panel.Section("C++ console integration and visual proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase30_ConeCapPushes.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Run = [&](const char* Command) { return Host.Execute(Command); };
    const bool Rendered =
        Run("gizmo off") &&
        Run("reset") && Run("view iso") && Run("cone (-15,0,0) 8 3 20 --name=Ref") && Run("matcap Ref steel") &&
        Run("cone (15,0,0) 8 3 20 --name=Src") && Run("push Src 4 --face=2 --name=Top") && Run("matcap Top gold") &&
        Run("view fit") && Run("render sheet 0") &&
        Run("reset") && Run("view iso") && Run("cone (-15,0,0) 8 3 20 --name=Ref") && Run("matcap Ref steel") &&
        Run("cone (15,0,0) 8 3 20 --name=Src") && Run("push Src 4 --face=1 --name=Bottom") && Run("matcap Bottom plastic-blue") &&
        Run("view fit") && Run("render sheet 1") &&
        Run("reset") && Run("view iso") && Run("cone (-15,0,0) 8 3 20 --name=Ref") && Run("matcap Ref steel") &&
        Run("cone (15,0,0) 8 3 20 --name=Src") && Run("push Src -8 --face=2 --name=Inset") && Run("matcap Inset copper") &&
        Run("view fit") && Run("render sheet 2") && Run("render sheet finalize Phase30_ConeCapPushes");
    Panel.Expect("C++ cone-cap proof commands complete without refusal", Rendered);
    const SceneFigure* ConsoleResult = Host.Document().Find("Inset");
    Panel.Expect("The console commits the exact kernel result, not only a successful preview",
                 ConsoleResult && ConsoleResult->Classification == FigureClassification::Body &&
                 ExactCone(ConsoleResult->Body, { 15, 0, 0 }, Axis, 8.0, 5.0, 12.0));
    Panel.Expect("C++ cone-cap proof PNG is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
