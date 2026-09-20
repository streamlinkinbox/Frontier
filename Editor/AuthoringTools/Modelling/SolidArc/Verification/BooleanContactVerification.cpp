//============================================================================================================================================
// 📦 Verification/BooleanContactVerification.cpp — Phase 24: exact contact classification and topology-safe box booleans
//============================================================================================================================================
// A generic surface/surface marcher quite properly has no section curve for coincident faces or zero-volume contacts.
// These checks exercise the constructive axis-box resolver instead: face contact is healed into one box, while point and
// edge contact remain separate, manifold components rather than being welded into a non-manifold B-rep.
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

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

[[nodiscard]] bool SolidVolume(const Result& R, double Volume, double Tolerance = 1e-9) noexcept
{
    return R.Body && R.Body.Payload.Validate().Solid() && std::fabs(R.Body.Payload.Validate().Volume - Volume) <= Tolerance;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 24 · Boolean Contact Verification — exact axis-box contact healing without non-manifold welds");
    const BrepBody Unit = BrepBody::Box({ 0, 0, 0 }, { 1, 1, 1 }).Payload;

    Panel.Section("Face contact: merge a zero-gap shared face into one healed box");
    {
        const BrepBody Adjacent = BrepBody::Box({ 1, 0, 0 }, { 2, 1, 1 }).Payload;
        Result U = Run(Unit, Adjacent, BodyOperation::Union);
        Result S = Run(Unit, Adjacent, BodyOperation::Subtract);
        Result I = Run(Unit, Adjacent, BodyOperation::Intersect);
        Panel.Expect("Face-touch union is a single closed manifold component", U.Body && U.Body.Payload.Validate().Solid() && U.Body.Payload.Validate().Hulls == 1 && U.Body.Payload.Vertices.size() == 8 && U.Body.Payload.Edges.size() == 12 && U.Body.Payload.Faces.size() == 6);
        Panel.Within("Face-touch union has exact volume 2", U.Body ? std::fabs(U.Body.Payload.Validate().Volume - 2.0) : 1.0, 1e-9);
        Panel.Expect("Face-touch subtraction preserves the target", SolidVolume(S, 1.0));
        Panel.Expect("Face-touch common is an explicit empty-volume refusal", !I.Body && I.Body.Denial.Reason == RefusalReason::DegenerateInput);
        Panel.Expect("Exact contact uses no invented SSI curve", U.Report.Curves == 0 && S.Report.Curves == 0 && I.Report.Curves == 0);
    }

    Panel.Section("Coincident and overlapping boxes: exact constructive CSG");
    {
        const BrepBody Same = BrepBody::Box({ 0, 0, 0 }, { 1, 1, 1 }).Payload;
        Result U0 = Run(Unit, Same, BodyOperation::Union);
        Result S0 = Run(Unit, Same, BodyOperation::Subtract);
        Result I0 = Run(Unit, Same, BodyOperation::Intersect);
        Panel.Expect("Coincident union returns one valid copy", SolidVolume(U0, 1.0) && U0.Body.Payload.Faces.size() == 6);
        Panel.Expect("Coincident subtract is an explicit empty-volume refusal", !S0.Body && S0.Body.Denial.Reason == RefusalReason::DegenerateInput);
        Panel.Expect("Coincident common returns one valid copy", SolidVolume(I0, 1.0) && I0.Body.Payload.Faces.size() == 6);

        const BrepBody Overlap = BrepBody::Box({ 0.5, 0, 0 }, { 1.5, 1, 1 }).Payload;
        Result U = Run(Unit, Overlap, BodyOperation::Union);
        Result S = Run(Unit, Overlap, BodyOperation::Subtract);
        Result I = Run(Unit, Overlap, BodyOperation::Intersect);
        Panel.Expect("Aligned half-overlap union heals to one box", SolidVolume(U, 1.5) && U.Body.Payload.Vertices.size() == 8 && U.Body.Payload.Faces.size() == 6);
        Panel.Expect("Aligned half-overlap subtract heals to the remaining box", SolidVolume(S, 0.5) && S.Body.Payload.Vertices.size() == 8 && S.Body.Payload.Faces.size() == 6);
        Panel.Expect("Aligned half-overlap common heals to the overlap box", SolidVolume(I, 0.5) && I.Body.Payload.Vertices.size() == 8 && I.Body.Payload.Faces.size() == 6);
        Panel.Expect("Constructive overlap uses no numerical SSI curves", U.Report.Curves == 0 && S.Report.Curves == 0 && I.Report.Curves == 0);

        const BrepBody Thin = BrepBody::Box({ 0.9999, 0, 0 }, { 2, 1, 1 }).Payload;
        Result ThinU = Run(Unit, Thin, BodyOperation::Union), ThinS = Run(Unit, Thin, BodyOperation::Subtract), ThinI = Run(Unit, Thin, BodyOperation::Intersect);
        Panel.Expect("A real 0.0001 overlap is not mistaken for contact", SolidVolume(ThinU, 2.0) && SolidVolume(ThinS, 0.9999) && SolidVolume(ThinI, 0.0001));
    }

    Panel.Section("Edge, point and gap contact: preserve separate solid components");
    {
        const BrepBody EdgeTouch = BrepBody::Box({ 1, 1, 0 }, { 2, 2, 1 }).Payload;
        const BrepBody PointTouch = BrepBody::Box({ 1, 1, 1 }, { 2, 2, 2 }).Payload;
        const BrepBody Gap = BrepBody::Box({ 1.00001, 0, 0 }, { 2.00001, 1, 1 }).Payload;
        for (const auto& C : { std::pair{ "edge", EdgeTouch }, std::pair{ "point", PointTouch }, std::pair{ "gap", Gap } })
        {
            Result U = Run(Unit, C.second, BodyOperation::Union);
            Result S = Run(Unit, C.second, BodyOperation::Subtract);
            Result I = Run(Unit, C.second, BodyOperation::Intersect);
            BodyReport Report = U.Body ? U.Body.Payload.Validate() : BodyReport{};
            Panel.Expect((std::string("The ") + C.first + " union preserves two manifold hulls").c_str(), U.Body && Report.Solid() && Report.Hulls == 2 && Report.NonManifoldEdges == 0 && std::fabs(Report.Volume - 2.0) < 1e-9);
            Panel.Expect((std::string("The ") + C.first + " subtract preserves the target").c_str(), SolidVolume(S, 1.0));
            Panel.Expect((std::string("The ") + C.first + " common is empty volume").c_str(), !I.Body && I.Body.Denial.Reason == RefusalReason::DegenerateInput);
        }
    }

    Panel.Section("Contained and non-rectangular results: retain appropriate exact or SSI routes");
    {
        const BrepBody Inner = BrepBody::Box({ 0.25, 0.25, 0.25 }, { 0.75, 0.75, 0.75 }).Payload;
        Result U = Run(Unit, Inner, BodyOperation::Union);
        Result S = Run(Unit, Inner, BodyOperation::Subtract);
        Result I = Run(Unit, Inner, BodyOperation::Intersect);
        Panel.Expect("Contained union retains the enclosing box", SolidVolume(U, 1.0) && U.Body.Payload.Faces.size() == 6);
        Panel.Expect("Contained subtract retains a valid two-hull cavity shell", SolidVolume(S, 0.875) && S.Body.Payload.Validate().Hulls == 2);
        Panel.Expect("Contained common returns the inner box", SolidVolume(I, 0.125) && I.Body.Payload.Faces.size() == 6);

        const BrepBody Large = BrepBody::Box({ 0, 0, 0 }, { 2, 2, 2 }).Payload;
        const BrepBody Corner = BrepBody::Box({ 1, 1, 1 }, { 3, 3, 3 }).Payload;
        Result CornerU = Run(Large, Corner, BodyOperation::Union);
        Result CornerS = Run(Large, Corner, BodyOperation::Subtract);
        Result CornerI = Run(Large, Corner, BodyOperation::Intersect);
        Panel.Expect("A non-rectangular corner union falls back to a valid SSI result", SolidVolume(CornerU, 15.0));
        Panel.Expect("A non-rectangular corner subtract falls back to a valid SSI result", SolidVolume(CornerS, 7.0));
        Panel.Expect("The box-shaped corner common stays constructive", SolidVolume(CornerI, 1.0) && CornerI.Report.Curves == 0);
        Panel.Expect("The non-box-shaped outcomes retain their six SSI sections", CornerU.Report.Curves == 6 && CornerS.Report.Curves == 6);
    }

    Panel.Section("C++ console proof: four contact outcomes in one contact sheet");
    {
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase24_BooleanContacts.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1280, 800);
        auto RunCommand = [&](const char* Command) { return Host.Execute(Command); };
        bool Rendered =
            RunCommand("gizmo off") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("box (0,0,0) (2,2,2) --name=FaceA") && RunCommand("box (2,0,0) (4,2,2) --name=FaceB") &&
            RunCommand("boolean union FaceA FaceB --name=FaceContact") && RunCommand("matcap FaceContact gold") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 0") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("box (0,0,0) (2,2,2) --name=EdgeA") && RunCommand("box (2,2,0) (4,4,2) --name=EdgeB") &&
            RunCommand("boolean union EdgeA EdgeB --name=EdgeContact") && RunCommand("matcap EdgeContact plastic-blue") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 1") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("box (0,0,0) (2,2,2) --name=SameA") && RunCommand("box (0,0,0) (2,2,2) --name=SameB") &&
            RunCommand("boolean intersect SameA SameB --name=CoincidentCommon") && RunCommand("matcap CoincidentCommon steel") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 2") &&
            RunCommand("reset") && RunCommand("view iso") &&
            RunCommand("box (0,0,0) (3,2,2) --name=CutA") && RunCommand("box (2,0,0) (5,2,2) --name=CutB") &&
            RunCommand("boolean subtract CutA CutB --name=OverlapDifference") && RunCommand("matcap OverlapDifference copper") && RunCommand("view fit") && RunCommand("view dolly 0.78") && RunCommand("render sheet 3") &&
            RunCommand("render sheet finalize Phase24_BooleanContacts");
        Panel.Expect("C++ contact proof commands complete without refusal", Rendered);
        Panel.Expect("C++ contact proof PNG is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
