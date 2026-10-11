//============================================================================================================================================
// 📦 Verification/BooleanIdentityVerification.cpp — Phase 24b: exact duplicate B-rep Boolean identity
//============================================================================================================================================
// SSI has no transverse intersection to march when every face, edge and vertex is already exactly coincident. This suite
// verifies the intentional narrow answer: an exactly equal, valid B-rep copy selects the surviving operand directly;
// it does not turn close-but-distinct solids into an identity result.
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
    return R.Body && R.Body.Payload.Validate().Solid() && Source.Validate().Solid() &&
           std::fabs(R.Body.Payload.Validate().Volume - Source.Validate().Volume) < 1e-9 &&
           R.Body.Payload.Vertices.size() == Source.Vertices.size() && R.Body.Payload.Edges.size() == Source.Edges.size() && R.Body.Payload.Faces.size() == Source.Faces.size();
}

void CheckIdentity(VerificationPanel& Panel, const char* Name, const BrepBody& A, const BrepBody& B)
{
    Result U = Run(A, B, BodyOperation::Union);
    Result S = Run(A, B, BodyOperation::Subtract);
    Result I = Run(A, B, BodyOperation::Intersect);
    Panel.Expect((std::string(Name) + ": union returns the sole valid solid").c_str(), SameSolid(U, A));
    Panel.Expect((std::string(Name) + ": common returns the sole valid solid").c_str(), SameSolid(I, A));
    Panel.Expect((std::string(Name) + ": difference is an explicit empty-result refusal").c_str(), !S.Body && S.Body.Denial.Reason == RefusalReason::DegenerateInput);
    Panel.Expect((std::string(Name) + ": no coincident SSI curves are invented").c_str(), U.Report.Curves == 0 && S.Report.Curves == 0 && I.Report.Curves == 0);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 24b · Boolean Identity Verification — exact duplicate B-reps bypass non-transversal SSI");

    Panel.Section("Exact duplicate primitives and generated solids");
    {
        const BrepBody Sphere = BrepBody::Sphere({ 0, 0, 0 }, 1.0).Payload;
        const BrepBody SphereCopy = Sphere;
        CheckIdentity(Panel, "Copied sphere", Sphere, SphereCopy);

        const BrepBody CylinderA = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, 2.0).Payload;
        const BrepBody CylinderB = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, 2.0).Payload;
        CheckIdentity(Panel, "Independently built cylinder", CylinderA, CylinderB);

        const BrepBody Torus = BrepBody::Torus({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 0.5).Payload;
        const BrepBody TorusCopy = Torus;
        CheckIdentity(Panel, "Copied torus", Torus, TorusCopy);

        const NurbsCurve Circle = NurbsCurve::Circle({ 0, 0, 0 }, { 0, 0, 1 }, 0.5).Payload;
        const BrepBody ExtrudeA = BrepBody::Extrude(Circle, { 0, 0, 1 }, 2.0).Payload;
        const BrepBody ExtrudeB = BrepBody::Extrude(Circle, { 0, 0, 1 }, 2.0).Payload;
        CheckIdentity(Panel, "Independently built extrude", ExtrudeA, ExtrudeB);
    }

    Panel.Section("Exact identity includes trimmed result topology, not only primitive parameters");
    {
        const BrepBody A = BrepBody::Sphere({ 0, 0, 0 }, 1.0).Payload;
        const BrepBody B = BrepBody::Sphere({ 1.0, 0, 0 }, 1.0).Payload;
        Result Built = Run(A, B, BodyOperation::Union);
        Panel.Expect("The source Boolean for a trimmed identity copy is valid", Built.Body && Built.Body.Payload.Validate().Solid() && Built.Report.Curves == 2);
        const BrepBody Copy = Built.Body ? Built.Body.Payload : BrepBody{};
        if (Built.Body) CheckIdentity(Panel, "Copied trimmed sphere-union result", Built.Body.Payload, Copy);
        else
        {
            Panel.Expect("Copied trimmed sphere-union result: union returns the sole valid solid", false);
            Panel.Expect("Copied trimmed sphere-union result: common returns the sole valid solid", false);
            Panel.Expect("Copied trimmed sphere-union result: difference is an explicit empty-result refusal", false);
            Panel.Expect("Copied trimmed sphere-union result: no coincident SSI curves are invented", false);
        }
    }

    Panel.Section("Near geometry is not guessed as identity; ordinary crossing remains SSI");
    {
        const BrepBody A = BrepBody::Sphere({ 0, 0, 0 }, 1.0).Payload;
        const BrepBody Near = BrepBody::Sphere({ 0.0001, 0, 0 }, 1.0).Payload;
        Result NearU = Run(A, Near, BodyOperation::Union);
        Result NearI = Run(A, Near, BodyOperation::Intersect);
        Panel.Expect("A near-coincident pair is not silently replaced with the target", !NearU.Body && !NearI.Body && NearU.Body.Denial.Reason == RefusalReason::Unsupported && NearI.Body.Denial.Reason == RefusalReason::Unsupported);

        const BrepBody Crossing = BrepBody::Sphere({ 1.2, 0.3, 0.2 }, 1.0).Payload;
        Result U = Run(A, Crossing, BodyOperation::Union);
        Result S = Run(A, Crossing, BodyOperation::Subtract);
        Result I = Run(A, Crossing, BodyOperation::Intersect);
        Panel.Expect("A distinct crossing sphere pair still takes the ordinary Boolean path", U.Body && S.Body && I.Body && U.Body.Payload.Validate().Solid() && S.Body.Payload.Validate().Solid() && I.Body.Payload.Validate().Solid());
        Panel.Expect("A distinct crossing sphere pair reports its two true SSI curves", U.Report.Curves == 2 && S.Report.Curves == 2 && I.Report.Curves == 2);
    }

    Panel.Section("C++ console proof: primitive, toroidal, and ordinary Boolean outcomes");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase24b_BooleanIdentity.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto RunCommand = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            RunCommand("gizmo off") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("sphere (0,0,0) 1 --name=SphereA") && RunCommand("sphere (0,0,0) 1 --name=SphereB") &&
            RunCommand("boolean union SphereA SphereB --name=SphereIdentity") && RunCommand("matcap SphereIdentity gold") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 0") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,0) 0.75 2 --name=CylinderA") && RunCommand("cylinder (0,0,0) 0.75 2 --name=CylinderB") &&
            RunCommand("boolean intersect CylinderA CylinderB --name=CylinderIdentity") && RunCommand("matcap CylinderIdentity plastic-blue") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 1") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("torus (0,0,0) 2 0.5 --name=TorusA") && RunCommand("torus (0,0,0) 2 0.5 --name=TorusB") &&
            RunCommand("boolean union TorusA TorusB --name=TorusIdentity") && RunCommand("matcap TorusIdentity steel") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 2") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("sphere (0,0,0) 1 --name=CrossingA") && RunCommand("sphere (1.2,0.3,0.2) 1 --name=CrossingB") &&
            RunCommand("boolean union CrossingA CrossingB --name=CrossingUnion") && RunCommand("matcap CrossingUnion copper") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 3") &&
            RunCommand("render sheet finalize Phase24b_BooleanIdentity");
        Panel.Expect("C++ identity proof commands complete without refusal", Rendered);
        Panel.Expect("C++ identity proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
