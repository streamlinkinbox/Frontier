/*====================================================================================================================================
                                                    REMASTEREDINSPECTION.CPP
====================================================================================================================================*/
// 🧩 Line-protocol inspection host: one request per stdin line, one JSON reply per stdout line

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/IntersectionAudit.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/BlendCatalogue.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/BooleanCatalogue.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Local/FaceOperationCatalogue.h"
#include "Engine/GeometryProcessing/Inspection/BlendEncoding.h"
#include "Engine/GeometryProcessing/Inspection/BooleanEncoding.h"
#include "Engine/GeometryProcessing/Inspection/FaceOperationEncoding.h"
#include "Engine/GeometryProcessing/Inspection/FeatureEncoding.h"
#include "Engine/GeometryProcessing/Inspection/InspectionEncoding.h"
#include "Engine/GeometryProcessing/Inspection/IntersectionEncoding.h"
#include "Engine/GeometryProcessing/Inspection/JsonWriter.h"
#include "Engine/GeometryProcessing/Inspection/SpecimenCatalogue.h"
#include "Engine/GeometryProcessing/Inspection/TessellationEncoding.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] double ElapsedMilliseconds(Clock::time_point Start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - Start).count();
    }

    [[nodiscard]] bool BooleanSceneCondition(const std::string& Name)
    {
        const std::vector<std::string> Scenes = EnumerateBooleanScenes();
        return std::find(Scenes.begin(), Scenes.end(), Name) != Scenes.end();
    }

    [[nodiscard]] bool BlendSpecimenCondition(const std::string& Name)
    {
        const std::vector<std::string> Specimens = EnumerateBlendSpecimens();
        return std::find(Specimens.begin(), Specimens.end(), Name) != Specimens.end();
    }

    // 📝 Blend specimen with the viewer's Size (catalogue size by default); the source body stays in the partition.
    struct AppliedBlend
    {
        BlendSpecimen Specimen = {};                                                         // [-] - Catalogue entry
        BlendSettings Settings = {};                                                         // [-] - Applied settings
        BlendOutcome  Outcome  = {};                                                         // [-] - Result or refusal
    };

    [[nodiscard]] AppliedBlend ApplyBlend(ModelPartition& Partition, const std::string& Name, const SpecimenParameters& Parameters)
    {
        AppliedBlend Applied;
        Applied.Specimen = ConstructBlendSpecimen(Partition, Name);
        Applied.Settings = Applied.Specimen.Settings;
        Applied.Settings.Size = std::clamp(Parameters.Resolve("Size", Applied.Settings.Size), 1.0e-4, 10.0);
        Applied.Outcome  = BlendEdges(Partition, Applied.Specimen.Body, Applied.Specimen.Edges, Applied.Settings);
        return Applied;
    }

    [[nodiscard]] bool FaceOperationSpecimenCondition(const std::string& Name)
    {
        const std::vector<std::string> Specimens = EnumerateFaceOperationSpecimens();
        return std::find(Specimens.begin(), Specimens.end(), Name) != Specimens.end();
    }

    // 📝 Face-operation specimen with the viewer's Amount (multiplier on the last step's distance, angle, vector and
    //    scale change; 1 = catalogue). Steps run in order; the first refusal stops the chain, the source stays intact.
    struct AppliedFaceOperation
    {
        FaceOperationSpecimen              Specimen  = {};                                   // [-]  - Catalogue entry
        std::vector<FaceOperationSettings> Steps     = {};                                   // [-]  - Steps actually run
        FaceOperationOutcome               Outcome   = {};                                   // [-]  - Last outcome (or the refusal)
        int                                Completed = 0;                                    // [-]  - Steps that succeeded
        double                             Amount    = 1.0;                                  // [-]  - Applied multiplier
        double                             Milliseconds = 0.0;                               // [ms] - Σ step wall time
    };

    [[nodiscard]] AppliedFaceOperation ApplyFaceOperationSpecimen(ModelPartition& Partition, const std::string& Name, const SpecimenParameters& Parameters)
    {
        AppliedFaceOperation Applied;
        Applied.Specimen = ConstructFaceOperationSpecimen(Partition, Name);
        Applied.Steps    = Applied.Specimen.Steps;
        Applied.Amount   = std::clamp(Parameters.Resolve("Amount", 1.0), -4.0, 4.0);
        if (!Applied.Steps.empty())
        {
            FaceOperationSettings& Last = Applied.Steps.back();
            Last.Distance *= Applied.Amount;
            Last.Angle    *= Applied.Amount;
            Last.Vector    = Last.Vector * Applied.Amount;
            Last.Factor    = 1.0 + (Last.Factor - 1.0) * Applied.Amount;
        }
        BodyToken Cursor = Applied.Specimen.Body;
        Applied.Outcome.Body = Cursor;
        for (const FaceOperationSettings& Step : Applied.Steps)
        {
            Applied.Outcome = ApplyFaceOperation(Partition, Cursor, Step);
            Applied.Milliseconds += Applied.Outcome.Milliseconds;
            if (!Applied.Outcome.Refusal.empty()) break;
            Cursor = Applied.Outcome.Body;
            ++Applied.Completed;
        }
        return Applied;
    }

    [[nodiscard]] bool SketchSpecimenCondition(const std::string& Name)
    {
        for (const SketchSpecimen& Specimen : BuildSketchCatalogue()) if (Specimen.Name == Name) return true;
        return false;
    }

    [[nodiscard]] bool FeatureSpecimenCondition(const std::string& Name)
    {
        const std::vector<std::string> Specimens = EnumerateFeatureSpecimens();
        return std::find(Specimens.begin(), Specimens.end(), Name) != Specimens.end();
    }

    // 📝 Feature tree with the viewer's parameters, replayed up to Upto steps. References are first captured at the
    //    authored defaults (as a designer would have picked them), so a changed parameter exercises the real resolver.
    struct RegeneratedFeature
    {
        FeatureSpecimen     Specimen = {};                                                   // [-] - Catalogue entry (authored tree)
        FeatureTree         Tree     = {};                                                   // [-] - Tree as replayed
        RegenerationOutcome Outcome  = {};                                                   // [-] - Body, names, step reports
        int                 Shown    = 0;                                                    // [-] - Steps replayed
        int                 Total    = 0;                                                    // [-] - Steps authored
    };

    [[nodiscard]] RegeneratedFeature RegenerateFeatureSpecimen(ModelPartition& Partition, const std::string& Name, const SpecimenParameters& Parameters)
    {
        RegeneratedFeature Regenerated;
        Regenerated.Specimen = BuildFeatureSpecimen(Name);
        Regenerated.Tree     = Regenerated.Specimen.Tree;
        {
            ModelPartition Authoring;
            (void)RegenerateFeatureTree(Authoring, Regenerated.Tree, true);
        }
        for (FeatureParameter& Parameter : Regenerated.Tree.Parameters) Parameter.Value = Parameters.Resolve(Parameter.Name, Parameter.Value);
        Regenerated.Total = static_cast<int>(Regenerated.Tree.Steps.size());
        Regenerated.Shown = std::clamp(static_cast<int>(std::lround(Parameters.Resolve("Upto", Regenerated.Total))), 1, Regenerated.Total);
        Regenerated.Tree.Steps.resize(Regenerated.Shown);
        Regenerated.Outcome = RegenerateFeatureTree(Partition, Regenerated.Tree, false);
        return Regenerated;
    }

    // The body a slice classifies: a model specimen, the chosen operation of a Boolean scene, or a blend result (Source=1: before).
    [[nodiscard]] BodyToken ConstructSliceBody(ModelPartition& Partition, const std::string& Name, const SpecimenParameters& Parameters, std::string& Refusal)
    {
        if (SketchSpecimenCondition(Name)) { Refusal = "a sketch has no body"; return {}; }
        if (FeatureSpecimenCondition(Name))
        {
            const RegeneratedFeature Regenerated = RegenerateFeatureSpecimen(Partition, Name, Parameters);
            if (!Regenerated.Outcome.Body.PresentCondition()) Refusal = "no body after the replayed steps";
            return Regenerated.Outcome.Body;
        }
        if (FaceOperationSpecimenCondition(Name))
        {
            const AppliedFaceOperation Applied = ApplyFaceOperationSpecimen(Partition, Name, Parameters);
            if (Parameters.Resolve("Source", 0.0) > 0.5) return Applied.Specimen.Body;
            Refusal = Applied.Outcome.Refusal;
            return Applied.Outcome.Body;
        }
        if (BlendSpecimenCondition(Name))
        {
            const AppliedBlend Applied = ApplyBlend(Partition, Name, Parameters);
            if (Parameters.Resolve("Source", 0.0) > 0.5) return Applied.Specimen.Body;
            Refusal = Applied.Outcome.Refusal;
            return Applied.Outcome.Body;
        }
        if (BooleanSceneCondition(Name))
        {
            const int          Shown = std::clamp(static_cast<int>(std::lround(Parameters.Resolve("Operation", 0.0))), 0, 3);
            const BooleanScene Scene = ConstructBooleanScene(Partition, Name);
            const BooleanOutcome Result = ComputeBoolean(Partition, Scene.First, Scene.Second, static_cast<BooleanOperation>(Shown));
            Refusal = Result.Refusal;
            return Result.Body;
        }
        if (IntersectionSceneCondition(Name)) { Refusal = "intersection scenes enclose no solid"; return {}; }
        const SpecimenOutcome Specimen = ConstructSpecimen(Partition, Name, Parameters);
        Refusal = Specimen.Refusal;
        return Specimen.Body;
    }

    // 📝 Generalized-winding-number slice: an N × N grid on the plane x_Axis = low + Offset·(high − low) of the body box,
    //    every point classified by ClassifySolidPoint (proximity → nearest normal → Σ Ω_F / 4π).
    [[nodiscard]] std::string AnswerSlice(const std::string& Name, const SpecimenParameters& Parameters)
    {
        ModelPartition Partition;
        std::string    Refusal;
        const BodyToken Body = ConstructSliceBody(Partition, Name, Parameters, Refusal);
        if (!Refusal.empty()) return EncodeRefusal(Name, Refusal);
        const int    Axis       = std::clamp(static_cast<int>(std::lround(Parameters.Resolve("Axis", 2.0))), 0, 2);
        const double Offset     = std::clamp(Parameters.Resolve("Offset", 0.5), 0.0, 1.0);    // [0-1] - Plane position in the box
        const int    Resolution = std::clamp(static_cast<int>(std::lround(Parameters.Resolve("Resolution", 29.0))), 3, 81);

        // 💡 A slice only rounds Σ Ω_F / 4π to an integer: 1e-6 panel tolerances keep the error ~10⁵ below the ½ threshold
        //    (ContainmentValidation runs the default 1e-10 tolerances).
        WindingSettings Settings;
        Settings.OuterTolerance = 1.0e-6;
        Settings.InnerTolerance = 1.0e-7;
        const Clock::time_point Start = Clock::now();
        const BodyContainment Containment = PrepareBodyContainment(Partition, Body);
        Vector3 Low = { 1.0e300, 1.0e300, 1.0e300 }, High = { -1.0e300, -1.0e300, -1.0e300 };   // [len] - Body box
        for (const FaceDomain& Face : Containment.Faces)
        {
            Low  = { std::min(Low.X, Face.BoundsLow.X), std::min(Low.Y, Face.BoundsLow.Y), std::min(Low.Z, Face.BoundsLow.Z) };
            High = { std::max(High.X, Face.BoundsHigh.X), std::max(High.Y, Face.BoundsHigh.Y), std::max(High.Z, Face.BoundsHigh.Z) };
        }
        const auto Component = [](Vector3& Point, int Index) -> double& { return Index == 0 ? Point.X : Index == 1 ? Point.Y : Point.Z; };
        const int  First = (Axis + 1) % 3, Second = (Axis + 2) % 3;                           // [-] - In-plane axes
        const double Margin = 0.08;                                                           // [-] - Grid overhang per side
        long Tallies[3] = { 0, 0, 0 }, Methods[3] = { 0, 0, 0 };
        int64_t Evaluations = 0;
        JsonWriter Writer;
        Writer.OpenObject();
        Writer.String("Kind", "Slice");
        Writer.String("Specimen", Name);
        Writer.Integer("Axis", Axis);
        Writer.Number("Offset", Offset);
        Writer.Integer("Resolution", Resolution);
        Writer.Number("OuterTolerance", Settings.OuterTolerance);
        Writer.OpenArray("Points");
        for (int Row = 0; Row < Resolution; ++Row)
        {
            for (int Column = 0; Column < Resolution; ++Column)
            {
                Vector3 Point = {};
                Component(Point, Axis)   = Component(Low, Axis) + (Component(High, Axis) - Component(Low, Axis)) * Offset;
                const double FirstSpan   = Component(High, First) - Component(Low, First);
                const double SecondSpan  = Component(High, Second) - Component(Low, Second);
                Component(Point, First)  = Component(Low, First) - Margin * FirstSpan + (1.0 + 2.0 * Margin) * FirstSpan * Column / (Resolution - 1);
                Component(Point, Second) = Component(Low, Second) - Margin * SecondSpan + (1.0 + 2.0 * Margin) * SecondSpan * Row / (Resolution - 1);
                const SolidPointClass Class = ClassifySolidPoint(Containment, Point, Settings);
                ++Tallies[static_cast<int>(Class.Membership)];
                ++Methods[std::min(2, static_cast<int>(Class.Method))];
                Evaluations += Class.Evaluations;
                Writer.OpenArray();
                Writer.Number(nullptr, Point.X);
                Writer.Number(nullptr, Point.Y);
                Writer.Number(nullptr, Point.Z);
                Writer.Integer(nullptr, static_cast<long>(Class.Membership));
                Writer.Number(nullptr, Class.Winding);
                Writer.CloseArray();
            }
        }
        Writer.CloseArray();
        Writer.OpenObject("Tallies");
        Writer.Integer("Outside", Tallies[static_cast<int>(PointMembership::Outside)]);
        Writer.Integer("Inside", Tallies[static_cast<int>(PointMembership::Inside)]);
        Writer.Integer("Boundary", Tallies[static_cast<int>(PointMembership::Boundary)]);
        Writer.Integer("Winding", Methods[static_cast<int>(ContainmentMethod::Winding)]);
        Writer.Integer("NearestNormal", Methods[static_cast<int>(ContainmentMethod::NearestNormal)]);
        Writer.Integer("Proximity", Methods[static_cast<int>(ContainmentMethod::Proximity)]);
        Writer.Integer("Evaluations", static_cast<long>(Evaluations));
        Writer.CloseObject();
        Writer.Number("Milliseconds", ElapsedMilliseconds(Start));
        Writer.CloseObject();
        return Writer.Result();
    }

    // 📝 Shared-edge CDT tessellation (N) of a model or Boolean result: ε = SagRatio × body diagonal, audited by N1–N7.
    [[nodiscard]] std::string AnswerTessellation(const std::string& Name, const SpecimenParameters& Parameters)
    {
        ModelPartition Partition;
        std::string    Refusal;
        const BodyToken Body = ConstructSliceBody(Partition, Name, Parameters, Refusal);
        if (!Refusal.empty()) return EncodeRefusal(Name, Refusal);
        TessellationSettings Settings;
        Settings.SagRatio = std::clamp(Parameters.Resolve("SagRatio", 1.0e-3), 1.0e-4, 5.0e-2);  // [-] - ε / diagonal
        const TessellationOutcome     Outcome = TessellateBody(Partition, Body, Settings);
        const Clock::time_point       Start   = Clock::now();
        const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
        return EncodeTessellation(Partition, Name, Settings.SagRatio, Outcome, Report, ElapsedMilliseconds(Start));
    }

    // 📝 Request grammar:  specimen | slice | tessellate <Name> [Key=Number …]   |   catalogue   |   quit
    [[nodiscard]] std::string AnswerRequest(const std::string& Request)
    {
        std::istringstream Stream(Request);
        std::string        Verb;
        Stream >> Verb;
        if (Verb == "catalogue") return EncodeCatalogue();
        if (Verb != "specimen" && Verb != "slice" && Verb != "tessellate") return EncodeRefusal("", "unknown request '" + Verb + "'");

        std::string        Name;
        SpecimenParameters Parameters;
        Stream >> Name;
        for (std::string Assignment; Stream >> Assignment;)
        {
            const size_t Separator = Assignment.find('=');                                   // [idx] - Key/number split
            if (Separator == std::string::npos) continue;
            try { Parameters.Values[Assignment.substr(0, Separator)] = std::stod(Assignment.substr(Separator + 1)); }
            catch (...) { return EncodeRefusal(Name, "parameter '" + Assignment + "' is not a number"); }
        }

        if (Verb == "slice") return AnswerSlice(Name, Parameters);
        if (Verb == "tessellate") return AnswerTessellation(Name, Parameters);
        if (SketchSpecimenCondition(Name))
        {
            for (const SketchSpecimen& Specimen : BuildSketchCatalogue())
            {
                if (Specimen.Name != Name) continue;
                SketchModel Solved = Specimen.Sketch;
                bool        EditedCondition = false;
                for (SketchConstraint& Constraint : Solved.Constraints)
                {
                    if (!EditableDimensionCondition(Constraint) || Constraint.Label.empty()) continue;
                    const double Edited = Parameters.Resolve(Constraint.Label, Constraint.Value);
                    EditedCondition = EditedCondition || Edited != Constraint.Value;
                    Constraint.Value = Edited;
                }
                const SketchSolveOutcome Solve   = SolveSketch(Solved);
                const SketchProfile      Profile = ExtractSketchProfile(Solved);
                return EncodeSketch(Specimen, Solved, Solve, Profile, EditedCondition);
            }
        }
        if (FeatureSpecimenCondition(Name))
        {
            ModelPartition    Partition;
            FeatureInspection Inspection;
            const RegeneratedFeature Regenerated = RegenerateFeatureSpecimen(Partition, Name, Parameters);
            Inspection.ShownSteps = Regenerated.Shown;
            Inspection.TotalSteps = Regenerated.Total;
            AuditReport Audit;
            const Clock::time_point Start = Clock::now();
            if (Regenerated.Outcome.Body.PresentCondition())
            {
                Audit = AuditBody(Partition, Regenerated.Outcome.Body);
                Inspection.Volume = IntegrateBodyVolume(Partition, Regenerated.Outcome.Body);
            }
            Inspection.AuditMilliseconds = ElapsedMilliseconds(Start);
            return EncodeFeature(Partition, Regenerated.Specimen, Regenerated.Tree, Regenerated.Outcome, Audit, Inspection);
        }
        if (FaceOperationSpecimenCondition(Name))
        {
            ModelPartition          Partition;
            FaceOperationInspection Inspection;
            Clock::time_point Start = Clock::now();
            const AppliedFaceOperation Applied = ApplyFaceOperationSpecimen(Partition, Name, Parameters);
            Inspection.ConstructionMilliseconds = std::max(0.0, ElapsedMilliseconds(Start) - Applied.Milliseconds);
            Inspection.Applied               = Applied.Steps;
            Inspection.Amount                = Applied.Amount;
            Inspection.CompletedSteps        = Applied.Completed;
            Inspection.OperationMilliseconds = Applied.Milliseconds;
            Inspection.SourceShown           = Parameters.Resolve("Source", 0.0) > 0.5;
            Inspection.SourceVolume          = IntegrateBodyVolume(Partition, Applied.Specimen.Body);
            const bool      Changed = !Inspection.SourceShown && Applied.Outcome.Refusal.empty();
            const BodyToken Shown   = Changed ? Applied.Outcome.Body : Applied.Specimen.Body;
            Inspection.ShownVolume  = Changed && Applied.Outcome.Category == BodyCategory::Sheet ? std::nan("") : IntegrateBodyVolume(Partition, Shown);
            Start = Clock::now();
            const AuditReport Audit = AuditBody(Partition, Shown);
            Inspection.AuditMilliseconds = ElapsedMilliseconds(Start);
            return EncodeFaceOperation(Partition, Applied.Specimen, Applied.Outcome, Shown, Audit, Inspection);
        }
        if (BlendSpecimenCondition(Name))
        {
            ModelPartition    Partition;
            BlendInspection   Inspection;
            Clock::time_point Start   = Clock::now();
            const AppliedBlend Applied = ApplyBlend(Partition, Name, Parameters);
            Inspection.ConstructionMilliseconds = std::max(0.0, ElapsedMilliseconds(Start) - Applied.Outcome.Statistics.Milliseconds);
            Inspection.Applied      = Applied.Settings;
            Inspection.SourceShown  = Parameters.Resolve("Source", 0.0) > 0.5;
            Inspection.SourceVolume = IntegrateBodyVolume(Partition, Applied.Specimen.Body);
            const BodyToken Shown   = Inspection.SourceShown || !Applied.Outcome.Refusal.empty() ? Applied.Specimen.Body : Applied.Outcome.Body;
            Inspection.ShownVolume  = IntegrateBodyVolume(Partition, Shown);
            Start = Clock::now();
            const AuditReport Audit = AuditBody(Partition, Shown);
            Inspection.AuditMilliseconds = ElapsedMilliseconds(Start);
            return EncodeBlend(Partition, Applied.Specimen, Applied.Outcome, Shown, Audit, Inspection);
        }
        if (BooleanSceneCondition(Name))
        {
            const int Shown = std::clamp(static_cast<int>(std::lround(Parameters.Resolve("Operation", 0.0))), 0, 3);
            ModelPartition          Partition;
            BooleanInspectionTiming Timing;
            Clock::time_point Start = Clock::now();
            const BooleanScene Scene = ConstructBooleanScene(Partition, Name);
            Timing.ConstructionMilliseconds = ElapsedMilliseconds(Start);
            Start = Clock::now();
            const GeneralFuseOutcome Fuse = RunGeneralFuse(Partition, Scene.First, Scene.Second);
            Timing.FuseMilliseconds = ElapsedMilliseconds(Start);
            std::array<BooleanInspectionResult, 4> Results;
            for (int Index = 0; Index < 4 && Fuse.Refusal.empty(); ++Index)
            {
                Start = Clock::now();
                Results[Index].Outcome      = AssembleBoolean(Partition, Fuse, static_cast<BooleanOperation>(Index));
                Results[Index].Milliseconds = ElapsedMilliseconds(Start);
                if (Results[Index].Outcome.Refusal.empty()) Results[Index].Volume = IntegrateBodyVolume(Partition, Results[Index].Outcome.Body);
            }
            AuditReport Audit;
            Start = Clock::now();
            if (Fuse.Refusal.empty() && Results[Shown].Outcome.Refusal.empty()) Audit = AuditBody(Partition, Results[Shown].Outcome.Body);
            Timing.AuditMilliseconds = ElapsedMilliseconds(Start);
            return EncodeBoolean(Partition, Scene, static_cast<BooleanOperation>(Shown), Fuse, Results, Audit, Timing);
        }

        if (IntersectionSceneCondition(Name))
        {
            const IntersectionScene Scene = ConstructIntersectionScene(Name, Parameters);
            Clock::time_point Start = Clock::now();
            const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Scene.First, Scene.FirstBox, Scene.Second, Scene.SecondBox, Scene.Settings);
            const double SolveMilliseconds = ElapsedMilliseconds(Start);
            Start = Clock::now();
            const IntersectionAuditReport Audit = AuditIntersection(Scene, Outcome);
            return EncodeIntersection(Name, Scene, Outcome, Audit, SolveMilliseconds, ElapsedMilliseconds(Start));
        }
        ModelPartition   Partition;
        InspectionTiming Timing;
        Clock::time_point Start = Clock::now();
        const SpecimenOutcome Specimen = ConstructSpecimen(Partition, Name, Parameters);
        Timing.ConstructionMilliseconds = ElapsedMilliseconds(Start);
        if (!Specimen.Refusal.empty()) return EncodeRefusal(Name, Specimen.Refusal);

        Start = Clock::now();
        const AuditReport Audit = AuditBody(Partition, Specimen.Body);
        Timing.AuditMilliseconds = ElapsedMilliseconds(Start);
        Start = Clock::now();
        const double Volume = IntegrateBodyVolume(Partition, Specimen.Body);
        Timing.VolumeMilliseconds = ElapsedMilliseconds(Start);
        return EncodeInspection(Partition, Name, Specimen, Audit, Volume, Timing);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    // 💡 With arguments the host answers once (handy for scripts); without, it serves the line protocol.
    if (ArgumentCount > 1)
    {
        std::string Request;
        for (int ArgumentIndex = 1; ArgumentIndex < ArgumentCount; ++ArgumentIndex) Request += std::string(ArgumentIndex > 1 ? " " : "") + Arguments[ArgumentIndex];
        std::cout << AnswerRequest(Request) << std::endl;
        return 0;
    }
    for (std::string Request; std::getline(std::cin, Request);)
    {
        if (Request == "quit") break;
        if (Request.empty()) continue;
        std::cout << AnswerRequest(Request) << std::endl;
    }
    return 0;
}
