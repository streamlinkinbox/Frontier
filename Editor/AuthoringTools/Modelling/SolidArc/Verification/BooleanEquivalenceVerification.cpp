//============================================================================================================================================
// 📦 Verification/BooleanEquivalenceVerification.cpp — Phase 24c: affine-NURBS equivalent Boolean identity
//============================================================================================================================================
// A native cylinder and a circular-profile extrusion can be the same B-rep geometry while their NURBS parameter domains
// differ (physical length versus [0,1]) and their surface classification hints differ. This verifies exact control-net
// identity under affine knot remapping—not a spatially fuzzy same-shape heuristic.
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
struct Result
{
    Deliver<BrepBody> Body;
    BooleanReport     Report;
};

[[nodiscard]] Result Run(const BrepBody& A, const BrepBody& B, BodyOperation Operation) noexcept
{
    Result R;
    R.Body = IntersectionSolver::Combine(A, B, Operation, &R.Report);
    return R;
}

[[nodiscard]] bool SameSolid(const Result& R, const BrepBody& Source) noexcept
{
    return R.Body && R.Body.Payload.Validate().Solid() &&
           std::fabs(R.Body.Payload.Validate().Volume - Source.Validate().Volume) < 1e-9 &&
           R.Body.Payload.Vertices.size() == Source.Vertices.size() && R.Body.Payload.Edges.size() == Source.Edges.size() && R.Body.Payload.Faces.size() == Source.Faces.size();
}

void CheckEquivalent(VerificationPanel& Panel, const char* Name, const BrepBody& A, const BrepBody& B)
{
    Result U = Run(A, B, BodyOperation::Union);
    Result S = Run(A, B, BodyOperation::Subtract);
    Result I = Run(A, B, BodyOperation::Intersect);
    Panel.Expect((std::string(Name) + ": union preserves the target geometry").c_str(), SameSolid(U, A));
    Panel.Expect((std::string(Name) + ": common preserves the target geometry").c_str(), SameSolid(I, A));
    Panel.Expect((std::string(Name) + ": subtract explicitly reports an empty result").c_str(), !S.Body && S.Body.Denial.Reason == RefusalReason::DegenerateInput);
    Panel.Expect((std::string(Name) + ": reports no fictitious SSI curve").c_str(), U.Report.Curves == 0 && S.Report.Curves == 0 && I.Report.Curves == 0);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 24c · Boolean Equivalence Verification — affine NURBS parameter domains preserve exact geometry");

    Panel.Section("Native cylinder equals an extrusion of the same full circle");
    {
        const BrepBody Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, 2.0).Payload;
        const NurbsCurve Circle = NurbsCurve::Circle({ 0, 0, 0 }, { 0, 0, 1 }, 0.75).Payload;
        const BrepBody ExtrudedCircle = BrepBody::Extrude(Circle, { 0, 0, 1 }, 2.0).Payload;
        CheckEquivalent(Panel, "Cylinder ∪/−/∩ circle-extrude", Cylinder, ExtrudedCircle);
        CheckEquivalent(Panel, "Circle-extrude ∪/−/∩ cylinder", ExtrudedCircle, Cylinder);
    }

    Panel.Section("Affine knot reparameterization is accepted; spatial differences are not");
    {
        const BrepBody Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, 2.0).Payload;
        const NurbsCurve Circle = NurbsCurve::Circle({ 0, 0, 0 }, { 0, 0, 1 }, 0.75).Payload;
        const BrepBody Remapped = BrepBody::Extrude(Circle.Reparameterised(-3.0, 6.0), { 0, 0, 1 }, 2.0).Payload;
        CheckEquivalent(Panel, "Cylinder ∪/−/∩ explicitly remapped circle-extrude", Cylinder, Remapped);

        const BrepBody DifferentRadius = BrepBody::Extrude(NurbsCurve::Circle({ 0, 0, 0 }, { 0, 0, 1 }, 0.7501).Payload, { 0, 0, 1 }, 2.0).Payload;
        Result U = Run(Cylinder, DifferentRadius, BodyOperation::Union);
        Result I = Run(Cylinder, DifferentRadius, BodyOperation::Intersect);
        Panel.Expect("A 0.0001 radius change is not mistaken for equivalent geometry", !U.Body && !I.Body && U.Report.Curves == 0 && I.Report.Curves == 0);
    }

    Panel.Section("A distinct crossing pair retains the general SSI route");
    {
        const BrepBody A = BrepBody::Sphere({ 0, 0, 0 }, 1.0).Payload;
        const BrepBody B = BrepBody::Sphere({ 1.2, 0.3, 0.2 }, 1.0).Payload;
        Result U = Run(A, B, BodyOperation::Union), S = Run(A, B, BodyOperation::Subtract), I = Run(A, B, BodyOperation::Intersect);
        Panel.Expect("Crossing spheres remain valid ordinary Boolean results", U.Body && S.Body && I.Body && U.Body.Payload.Validate().Solid() && S.Body.Payload.Validate().Solid() && I.Body.Payload.Validate().Solid());
        Panel.Expect("Crossing spheres retain their two SSI sections", U.Report.Curves == 2 && S.Report.Curves == 2 && I.Report.Curves == 2);
    }

    Panel.Section("C++ console proof: construction-independent cylinder identity");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase24c_BooleanEquivalence.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto RunCommand = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            RunCommand("gizmo off") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,0) 0.75 2 --name=Cylinder") && RunCommand("circle (0,0,0) 0.75 --name=Profile") &&
            RunCommand("extrude Profile 2 --name=CircleExtrude") && RunCommand("boolean union Cylinder CircleExtrude --name=Union") && RunCommand("matcap Union gold") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 0") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,0) 0.75 2 --name=Cylinder") && RunCommand("circle (0,0,0) 0.75 --name=Profile") &&
            RunCommand("extrude Profile 2 --name=CircleExtrude") && RunCommand("boolean intersect Cylinder CircleExtrude --name=Common") && RunCommand("matcap Common plastic-blue") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 1") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,0) 0.75 2 --name=Cylinder") && RunCommand("circle (0,0,0) 0.75 --name=Profile") &&
            RunCommand("extrude Profile 2 --name=CircleExtrude") && RunCommand("boolean union CircleExtrude Cylinder --name=ReverseUnion") && RunCommand("matcap ReverseUnion steel") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 2") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("sphere (0,0,0) 1 --name=CrossingA") && RunCommand("sphere (1.2,0.3,0.2) 1 --name=CrossingB") && RunCommand("boolean union CrossingA CrossingB --name=Crossing") && RunCommand("matcap Crossing copper") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 3") &&
            RunCommand("render sheet finalize Phase24c_BooleanEquivalence");
        Panel.Expect("C++ equivalence proof commands complete without refusal", Rendered);
        Panel.Expect("C++ equivalence proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
