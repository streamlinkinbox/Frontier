//============================================================================================================================================
// 📦 Verification/BooleanCylinderSeamVerification.cpp — Phase 24d: seam-invariant right-cylinder Boolean identity
//============================================================================================================================================
// A closed periodic circle can move its seam without changing the cylinder it extrudes. Generic SSI quite correctly
// refuses the resulting coincident side faces; this suite proves the bounded full-cylinder classifier returns the exact
// Boolean identity instead, while radius/height near misses remain outside the gate.
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
struct Result { Deliver<BrepBody> Body; BooleanReport Report; };

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

void CheckIdentity(VerificationPanel& Panel, const char* Name, const BrepBody& A, const BrepBody& B)
{
    Result U = Run(A, B, BodyOperation::Union), S = Run(A, B, BodyOperation::Subtract), I = Run(A, B, BodyOperation::Intersect);
    Panel.Expect((std::string(Name) + ": union returns one valid cylinder").c_str(), SameSolid(U, A));
    Panel.Expect((std::string(Name) + ": common returns one valid cylinder").c_str(), SameSolid(I, A));
    Panel.Expect((std::string(Name) + ": difference explicitly reports empty material").c_str(), !S.Body && S.Body.Denial.Reason == RefusalReason::DegenerateInput);
    Panel.Expect((std::string(Name) + ": no fictitious SSI section is created").c_str(), U.Report.Curves == 0 && S.Report.Curves == 0 && I.Report.Curves == 0);
}

[[nodiscard]] BrepBody RotatedCylinder(double SeamAngle) noexcept
{
    Deliver<NurbsCurve> Circle = NurbsCurve::Arc({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, SeamAngle, ScalarCriteria::TwoPi);
    return Circle ? BrepBody::Extrude(Circle.Payload, { 0, 0, 1 }, 2.0).Payload : BrepBody{};
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 24d · Boolean Cylinder Seam Verification — periodic seam placement is not a Boolean intersection");
    const BrepBody Native = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, 2.0).Payload;

    Panel.Section("Same cylinder with relocated periodic seams or opposite construction direction");
    {
        CheckIdentity(Panel, "Native cylinder versus 60-degree-seamed circle extrusion", Native, RotatedCylinder(ScalarCriteria::Pi / 3.0));
        CheckIdentity(Panel, "Native cylinder versus top-down cylinder", Native, BrepBody::Cylinder({ 0, 0, 2 }, { 0, 0, -1 }, 0.75, 2.0).Payload);
        CheckIdentity(Panel, "Two circular extrusions with independently relocated seams", RotatedCylinder(0.21), RotatedCylinder(1.37));
    }

    Panel.Section("Only fully equal right cylinders take the seam-invariant identity path");
    {
        Result RadiusU = Run(Native, BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.7501, 2.0).Payload, BodyOperation::Union);
        Result HeightI = Run(Native, BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 0.75, 2.0001).Payload, BodyOperation::Intersect);
        Panel.Expect("A 0.0001 radius difference is not treated as identity", !RadiusU.Body && RadiusU.Report.Curves == 0);
        Panel.Expect("A 0.0001 height difference is not treated as identity", !HeightI.Body && HeightI.Report.Curves == 0);

        const BrepBody Crossing = BrepBody::Cylinder({ -2, 0.3, 0 }, { 1, 0, 0 }, 0.5, 4).Payload;
        Result U = Run(BrepBody::Cylinder({ 0, 0, -2 }, { 0, 0, 1 }, 1.0, 4).Payload, Crossing, BodyOperation::Union);
        Result I = Run(BrepBody::Cylinder({ 0, 0, -2 }, { 0, 0, 1 }, 1.0, 4).Payload, Crossing, BodyOperation::Intersect);
        Panel.Expect("Perpendicular crossing cylinders retain the ordinary Boolean result", U.Body && I.Body && U.Body.Payload.Validate().Solid() && I.Body.Payload.Validate().Solid());
        Panel.Expect("Perpendicular crossing cylinders retain their four SSI sections", U.Report.Curves == 4 && I.Report.Curves == 4);
    }

    Panel.Section("C++ console proof: seam-independent cylinder identity versus a real crossing");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase24d_BooleanCylinderSeams.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto RunCommand = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            RunCommand("gizmo off") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,0) 0.75 2 --name=Native") && RunCommand("circle (0,0,0) 0.75 --name=Profile") &&
            RunCommand("extrude Profile 2 --name=CircleExtrude") && RunCommand("boolean union Native CircleExtrude --name=SeamUnion") && RunCommand("matcap SeamUnion gold") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 0") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,0) 0.75 2 --name=Native") && RunCommand("circle (0,0,0) 0.75 --name=Profile") &&
            RunCommand("extrude Profile 2 --name=CircleExtrude") && RunCommand("boolean intersect Native CircleExtrude --name=SeamCommon") && RunCommand("matcap SeamCommon plastic-blue") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 1") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,2) 0.75 -2 --name=TopDown") && RunCommand("cylinder (0,0,0) 0.75 2 --name=BottomUp") &&
            RunCommand("boolean union TopDown BottomUp --name=DirectionUnion") && RunCommand("matcap DirectionUnion steel") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 2") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("cylinder (0,0,-2) 1 4 --name=Vertical") && RunCommand("cylinder (-2,0.3,0) 0.5 4 --axis=(1,0,0) --name=Horizontal") &&
            RunCommand("boolean union Vertical Horizontal --name=Crossing") && RunCommand("matcap Crossing copper") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 3") &&
            RunCommand("render sheet finalize Phase24d_BooleanCylinderSeams");
        Panel.Expect("C++ cylinder-seam proof commands complete without refusal", Rendered);
        Panel.Expect("C++ cylinder-seam proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
