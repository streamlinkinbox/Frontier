/*====================================================================================================================================
                                                  ALGORITHMSTRESSVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Stress and timing over the two hot paths of modelling sessions: Booleans on growing bodies (drilled plates, pocket
//    rows, stacked unions, cylinder crosses, Booleans on blended bodies) and chamfer / fillet on EVERY edge of bodies that
//    are themselves Boolean results. Each step: audit, closed-form or sign-checked volume, wall time and phase split.

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Local/RedundantTopology.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr double VolumeTolerance = 1.0e-9;                                               // [-] - Relative (absolute below 1)
    constexpr double π               = std::numbers::pi;

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    struct BooleanRow
    {
        std::string Scene, Step, Outcome;                                                    // [-]    - Labels
        int         Faces = 0, Pairs = 0, Tested = 0, Fragments = 0, Wrong = 0;              // [-]    - Counts, oracle misses
        double      Volume = 0.0, Expected = std::nan("");                                   // [len³] - Result / closed form
        double      Milliseconds = 0.0, Intersect = 0.0, Split = 0.0, Classify = 0.0;        // [ms]   - Wall + phases
        double      Assemble = 0.0, Maximal = 0.0;                                           // [ms]   - Assembly, maximal faces
        bool        PassedCondition = false;                                                 // [-]
    };

    struct BlendRow
    {
        std::string Body, Profile;                                                           // [-]  - Labels
        int         Edges = 0, Accepted = 0, Refused = 0, Unsound = 0, WrongSign = 0, WrongVolume = 0;   // [-]  - Per-edge tallies
        double      Milliseconds = 0.0, Slowest = 0.0;                                       // [ms] - Total / worst edge
        std::string AllEdges = {};                                                           // [-]  - All edges at once
        double      AllMilliseconds = 0.0;                                                   // [ms]
        std::vector<std::string> Notes = {};                                                 // [-]  - Failures
    };

    std::vector<BooleanRow> BooleanRows;
    std::vector<BlendRow>   BlendRows;
    std::string             OnlyPrefix;                                                      // [-] - --only filter
    int                     OracleSamples = 64;                                              // [-] - --oracle N (0: off, for timing runs)

    [[nodiscard]] bool Selected(const std::string& Name) { return OnlyPrefix.empty() || Name.rfind(OnlyPrefix, 0) == 0; }

    [[nodiscard]] bool SoundBody(const ModelPartition& Partition, BodyToken Body) { return AuditBody(Partition, Body, AuditScope::Complete).SoundCondition(); }

    // 📝 Membership oracle (BooleanValidation B4): random points in the arguments' box; the result's membership must equal
    //    the operation applied to the arguments' memberships. Points within 10⁻⁶ of any boundary are skipped (ambiguous).
    [[nodiscard]] int CountOracleDisagreements(const ModelPartition& Partition, BodyToken First, BodyToken Second, BodyToken Result, BooleanOperation Operation, int Samples, uint32_t Seed)
    {
        const BodyContainment Containment[3] = { PrepareBodyContainment(Partition, First), PrepareBodyContainment(Partition, Second), PrepareBodyContainment(Partition, Result) };
        Vector3 Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
        for (int Index = 0; Index < 2; ++Index)
            for (const FaceDomain& Face : Containment[Index].Faces)
            {
                Low  = { std::min(Low.X, Face.BoundsLow.X), std::min(Low.Y, Face.BoundsLow.Y), std::min(Low.Z, Face.BoundsLow.Z) };
                High = { std::max(High.X, Face.BoundsHigh.X), std::max(High.Y, Face.BoundsHigh.Y), std::max(High.Z, Face.BoundsHigh.Z) };
            }
        std::mt19937 Random(Seed);
        std::uniform_real_distribution<double> Unit(0.0, 1.0);
        WindingSettings Settings;
        Settings.Precision = 1.0e-6;
        int Wrong = 0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            const Vector3 Point = { Low.X + (High.X - Low.X) * Unit(Random), Low.Y + (High.Y - Low.Y) * Unit(Random), Low.Z + (High.Z - Low.Z) * Unit(Random) };
            PointMembership Members[3];
            bool Ambiguous = false;
            for (int Index = 0; Index < 3; ++Index)
            {
                Members[Index] = ClassifySolidPointScreened(Containment[Index], Point, Settings).Membership;
                Ambiguous = Ambiguous || Members[Index] == PointMembership::Boundary;
            }
            if (Ambiguous) continue;
            const bool InFirst = Members[0] == PointMembership::Inside, InSecond = Members[1] == PointMembership::Inside;
            const bool Expected = Operation == BooleanOperation::Union ? (InFirst || InSecond) : Operation == BooleanOperation::Intersection ? (InFirst && InSecond) : (InFirst && !InSecond);
            if (Expected != (Members[2] == PointMembership::Inside)) ++Wrong;
        }
        return Wrong;
    }

    // One Boolean step: result body replaces Current when accepted.
    BodyToken StepBoolean(ModelPartition& Partition, const std::string& Scene, const std::string& Step, BodyToken Current, BodyToken Tool, BooleanOperation Operation, double Expected,
                          bool RefusalExpected = false)
    {
        BooleanRow Row;
        Row.Scene    = Scene;
        Row.Step     = Step;
        Row.Expected = Expected;
        // 📝 ComputeBoolean's three stages timed apart: General Fuse, assembly, maximal faces (same calls, same order).
        const Clock::time_point  Start      = Clock::now();
        const GeneralFuseOutcome Fuse       = RunGeneralFuse(Partition, Current, Tool, {});
        const GeneralFuseStatistics& Statistics = Fuse.Statistics;
        const Clock::time_point  Assembled  = Clock::now();
        BooleanOutcome           Outcome    = AssembleBoolean(Partition, Fuse, Operation, false);
        Row.Assemble = Elapsed(Assembled);
        if (Outcome.Refusal.empty())
        {
            const Clock::time_point Merged     = Clock::now();
            const RedundancyOutcome Redundancy = RemoveRedundantTopology(Partition, Outcome.Body);
            Outcome.Faces -= Redundancy.FacesMerged;
            Row.Maximal = Elapsed(Merged);
        }
        Row.Milliseconds = Elapsed(Start);
        Row.Pairs        = Statistics.FacePairs;
        Row.Tested       = Statistics.FacePairsTested;
        Row.Fragments    = Statistics.Fragments[0] + Statistics.Fragments[1];
        Row.Intersect    = Statistics.IntersectMilliseconds;
        Row.Split        = Statistics.SplitMilliseconds;
        Row.Classify     = Statistics.ClassifyMilliseconds;
        if (!Outcome.Refusal.empty() && RefusalExpected)
        {
            Row.PassedCondition = true;
            Row.Outcome = "🚧 refused (tangent contact, known limitation): " + Outcome.Refusal;
            BooleanRows.push_back(Row);
            return Current;
        }
        if (!Outcome.Refusal.empty())
        {
            Row.Outcome = "🔴 refused: " + Outcome.Refusal;
            BooleanRows.push_back(Row);
            return Current;
        }
        Row.Faces  = Outcome.Faces;
        Row.Volume = IntegrateBodyVolume(Partition, Outcome.Body);
        const AuditReport Audit = AuditBody(Partition, Outcome.Body, AuditScope::Complete);
        const bool Sound   = Audit.SoundCondition();
        const bool Volume  = std::isnan(Expected) || std::fabs(Row.Volume - Expected) <= VolumeTolerance * std::max(1.0, std::fabs(Expected));
        Row.Wrong  = Sound ? CountOracleDisagreements(Partition, Current, Tool, Outcome.Body, Operation, OracleSamples, static_cast<uint32_t>(BooleanRows.size() + 1)) : -1;
        Row.PassedCondition = Sound && Volume && Row.Wrong == 0;
        Row.Outcome = Row.PassedCondition ? "🟢 sound" : !Sound ? "🔴 unsound: " + std::to_string(Audit.Findings.size()) + " findings, first " + Audit.Findings.front().Code + " " + Audit.Findings.front().Subject + " " + Audit.Findings.front().Detail : !Volume ? "🔴 volume" : "🔴 membership oracle: " + std::to_string(Row.Wrong) + " / " + std::to_string(OracleSamples) + " points wrong";
        BooleanRows.push_back(Row);
        return Outcome.Body;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                BOOLEAN SCENES
    //--------------------------------------------------------------------------------------------------------------------

    void RunBooleanScenes()
    {
        // ① Plate drilled by 12 through holes, one at a time (face count grows 6 → 30).
        if (Selected("DrilledPlate"))
        {
            ModelPartition Partition;
            BodyToken      Plate  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 8.0, 6.0, 1.0 }).Value;
            double         Volume = 48.0;
            const double   Radius = 0.35;
            for (int Hole = 0; Hole < 12; ++Hole)
            {
                const Vector3   Foot  = { 1.0 + 2.0 * (Hole % 4), 1.0 + 2.0 * (Hole / 4), -0.5 };
                const BodyToken Drill = ConstructCylinder(Partition, ConstructFrame(Foot, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), Radius, 2.0).Value;
                Volume -= π * Radius * Radius;
                Plate = StepBoolean(Partition, "DrilledPlate", "hole " + std::to_string(Hole + 1), Plate, Drill, BooleanOperation::Difference, Volume);
            }
        }
        // ② Row of 8 turned pockets cut from the top (oblique planar sections).
        if (Selected("PocketRow"))
        {
            ModelPartition Partition;
            BodyToken      Plate  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 16.0, 4.0, 2.0 }).Value;
            double         Volume = 128.0;
            for (int Pocket = 0; Pocket < 8; ++Pocket)
            {
                const double  Turn   = 0.15 + 0.1 * Pocket;                                  // [rad]
                const Vector3 Centre = { 1.0 + 2.0 * Pocket, 2.0, 2.0 };
                const Vector3 U = { std::cos(Turn), std::sin(Turn), 0.0 }, V = { -std::sin(Turn), std::cos(Turn), 0.0 };
                std::vector<Vector3> Profile;
                for (const auto& [A, B] : { std::pair{ -0.5, -0.6 }, std::pair{ 0.5, -0.6 }, std::pair{ 0.5, 0.6 }, std::pair{ -0.5, 0.6 } })
                    Profile.push_back(Centre + U * A + V * B + Vector3{ 0.0, 0.0, -0.8 });
                const BodyToken Cutter = ConstructPrism(Partition, Profile, { 0.0, 0.0, 1.5 }).Value.Body;
                Volume -= 1.0 * 1.2 * 0.8;
                Plate = StepBoolean(Partition, "PocketRow", "pocket " + std::to_string(Pocket + 1), Plate, Cutter, BooleanOperation::Difference, Volume);
            }
        }
        // ③ Staircase of 8 unions with coplanar floors and flush sides (common blocks, ON fragments).
        if (Selected("Staircase"))
        {
            ModelPartition Partition;
            BodyToken      Stair  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 1.0, 3.0, 1.0 }).Value;
            double         Volume = 3.0;
            for (int Step = 1; Step < 9; ++Step)
            {
                const BodyToken Tread = ConstructBlock(Partition, { 0.5 * Step, 0.0, 0.0 }, { 1.0, 3.0, 1.0 + 0.5 * Step }).Value;
                Volume += 0.5 * 3.0 * (1.0 + 0.5 * Step);                                    // New half beyond the previous block
                Volume += 0.5 * 3.0 * 0.5;                                                   // Raised over the shared half
                Stair = StepBoolean(Partition, "Staircase", "tread " + std::to_string(Step), Stair, Tread, BooleanOperation::Union, Volume);
            }
        }
        // ④ Cylinder crosses (SSI of two quadrics: the slow BooleanValidation scenes).
        if (Selected("CylinderCross"))
        {
            for (int Variant = 0; Variant < 3; ++Variant)
            {
                ModelPartition Partition;
                const double Big = 0.6, Small = 0.3 + 0.1 * Variant, Offset = 0.05 * Variant;
                const BodyToken Main  = ConstructCylinder(Partition, ConstructFrame({ 0.0, 0.0, -2.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), Big, 4.0).Value;
                const BodyToken Cross = ConstructCylinder(Partition, ConstructFrame({ -2.0, Offset, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), Small, 4.0).Value;
                const std::string Step = "r " + std::to_string(Big).substr(0, 3) + " × " + std::to_string(Small).substr(0, 3) + " off " + std::to_string(Offset).substr(0, 4);
                (void)StepBoolean(Partition, "CylinderCross", Step + " ∪", Main, Cross, BooleanOperation::Union, std::nan(""));
                // 📝 Variant 2 is tangent (offset + small radius = big radius): the difference is pinched at one vertex, so
                //    a manifold modeller must refuse it.
                (void)StepBoolean(Partition, "CylinderCross", Step + (Variant == 2 ? " − (tangent)" : " −"), Main, Cross, BooleanOperation::Difference, std::nan(""), Variant == 2);
            }
        }
        // ⑤ Booleans on blended bodies (tori, corner spheres, chamfer planes as arguments).
        if (Selected("BlendedBody"))
        {
            for (BlendProfile Profile : { BlendProfile::Fillet, BlendProfile::Chamfer })
            {
                ModelPartition Partition;
                const BodyToken    Block   = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 3.0, 2.0, 1.0 }).Value;
                const BlendOutcome Blended = BlendEdges(Partition, Block, Partition.CollectInventory(Block).Edges, { Profile, 0.25 });
                const char*        Label   = Profile == BlendProfile::Fillet ? "fillet" : "chamfer";
                if (!Blended.Refusal.empty())
                {
                    BooleanRows.push_back({ "BlendedBody", std::string(Label) + " all edges", "🔴 refused: " + Blended.Refusal });
                    continue;
                }
                const double    Before = IntegrateBodyVolume(Partition, Blended.Body);
                const BodyToken Drill  = ConstructCylinder(Partition, ConstructFrame({ 1.5, 1.0, -1.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.4, 3.0).Value;
                BodyToken       Body   = StepBoolean(Partition, "BlendedBody", std::string(Label) + " − drill", Blended.Body, Drill, BooleanOperation::Difference, Before - π * 0.16);
                const BodyToken Slot   = ConstructBlock(Partition, { -1.0, 0.8, 0.6 }, { 5.0, 0.4, 1.0 }).Value;
                Body = StepBoolean(Partition, "BlendedBody", std::string(Label) + " − slot across blends", Body, Slot, BooleanOperation::Difference, std::nan(""));
                // 📝 Boss r 0.28 crosses the G1 edge between the top face and its fillet (y = 0.25) transversally; r 0.3
                //    is exactly tangent to the slot wall y = 0.8 — a tangent contact, refused today (🚧 known limitation).
                const BodyToken Boss    = ConstructCylinder(Partition, ConstructFrame({ 0.6, 0.5, 0.5 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.28, 1.0).Value;
                (void)StepBoolean(Partition, "BlendedBody", std::string(Label) + " ∪ boss", Body, Boss, BooleanOperation::Union, std::nan(""));
                const BodyToken Tangent = ConstructCylinder(Partition, ConstructFrame({ 0.6, 0.5, 0.5 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.3, 1.0).Value;
                (void)StepBoolean(Partition, "BlendedBody", std::string(Label) + " ∪ boss tangent to the slot wall (🚧)", Body, Tangent, BooleanOperation::Union, std::nan(""), true);
            }
        }
        // ⑥ Random block / cylinder sequences (robustness: refusals and unsound results are the bugs).
        if (Selected("RandomSequence"))
        {
            std::mt19937 Random(20261027);
            std::uniform_real_distribution<double> Unit(0.0, 1.0);
            for (int Sequence = 0; Sequence < 6; ++Sequence)
            {
                ModelPartition Partition;
                BodyToken Body = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value;
                for (int Step = 0; Step < 6; ++Step)
                {
                    const bool      Cylinder = Unit(Random) < 0.5;
                    const bool      Subtract = Unit(Random) < 0.7;
                    const Vector3   Foot     = { 4.0 * Unit(Random), 3.0 * Unit(Random), -0.5 + 2.0 * Unit(Random) };
                    const double    Turn     = 2.0 * π * Unit(Random);
                    const BodyToken Tool     = Cylinder ? ConstructCylinder(Partition, ConstructFrame(Foot, { 0.3 * Unit(Random), 0.3 * Unit(Random), 1.0 }, { std::cos(Turn), std::sin(Turn), 0.0 }),
                                                                            0.2 + 0.4 * Unit(Random), 1.0 + 2.0 * Unit(Random)).Value
                                                        : ConstructBlock(Partition, Foot, { 0.4 + Unit(Random), 0.4 + Unit(Random), 0.4 + Unit(Random) }).Value;
                    const std::string Label = "seq " + std::to_string(Sequence + 1) + " · " + std::to_string(Step + 1) + (Subtract ? " −" : " ∪") + (Cylinder ? " cylinder" : " block");
                    Body = StepBoolean(Partition, "RandomSequence", Label, Body, Tool, Subtract ? BooleanOperation::Difference : BooleanOperation::Union, std::nan(""));
                }
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                BLEND SCENES
    //--------------------------------------------------------------------------------------------------------------------

    struct BlendBody
    {
        std::string Name = {};
        std::function<BodyToken(ModelPartition&)> Build = {};
    };

    [[nodiscard]] BodyToken Unite(ModelPartition& Partition, BodyToken First, BodyToken Second, BooleanOperation Operation)
    {
        const BooleanOutcome Outcome = ComputeBoolean(Partition, First, Second, Operation);
        return Outcome.Refusal.empty() ? Outcome.Body : BodyToken{};
    }

    void RunBlendScenes(double Size)
    {
        const std::vector<BlendBody> Bodies =
        {
            { "Block", [](ModelPartition& P) { return ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value; } },
            { "Cylinder", [](ModelPartition& P) { return ConstructCylinder(P, ConstructFrame({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 1.0, 2.0).Value; } },
            { "Wedge", [](ModelPartition& P) { return ConstructPrism(P, { { 0.0, 0.0, 0.0 }, { 4.0, 0.0, 0.0 }, { 3.2, 1.5, 0.0 }, { 0.6, 2.2, 0.0 } }, { 0.0, 0.0, 1.5 }).Value.Body; } },
            { "DrilledBlock", [](ModelPartition& P) { return Unite(P, ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value,
                                                                   ConstructCylinder(P, ConstructFrame({ 2.0, 1.5, -1.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.6, 4.0).Value, BooleanOperation::Difference); } },
            { "LBracket", [](ModelPartition& P) { return Unite(P, ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 1.0 }).Value,
                                                               ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 1.0, 3.0, 3.0 }).Value, BooleanOperation::Union); } },
            { "Boss", [](ModelPartition& P) { return Unite(P, ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 1.0 }).Value,
                                                           ConstructCylinder(P, ConstructFrame({ 2.0, 1.5, 0.5 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 0.7, 1.5).Value, BooleanOperation::Union); } },
            { "Pocket", [](ModelPartition& P) { return Unite(P, ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value,
                                                             ConstructBlock(P, { 1.0, 1.0, 1.0 }, { 2.0, 1.0, 2.0 }).Value, BooleanOperation::Difference); } },
            { "TurnedCut", [](ModelPartition& P)
              {
                  const double Turn = 0.4;
                  std::vector<Vector3> Profile;
                  for (const auto& [A, B] : { std::pair{ -1.0, -0.5 }, std::pair{ 1.0, -0.5 }, std::pair{ 1.0, 0.5 }, std::pair{ -1.0, 0.5 } })
                      Profile.push_back({ 4.0 + A * std::cos(Turn) - B * std::sin(Turn), 1.5 + A * std::sin(Turn) + B * std::cos(Turn), -1.0 });
                  return Unite(P, ConstructBlock(P, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value, ConstructPrism(P, Profile, { 0.0, 0.0, 4.0 }).Value.Body, BooleanOperation::Difference);
              } },
        };
        for (const BlendBody& Specimen : Bodies)
        {
            if (!Selected("Blend" + Specimen.Name)) continue;
            for (BlendProfile Profile : { BlendProfile::Chamfer, BlendProfile::Fillet })
            {
                BlendRow Row;
                Row.Body    = Specimen.Name;
                Row.Profile = (Profile == BlendProfile::Chamfer ? "chamfer " : "fillet ") + std::to_string(Size).substr(0, 4);
                ModelPartition  Partition;
                const BodyToken Body = Specimen.Build(Partition);
                if (!Body.PresentCondition()) { Row.Notes.push_back("specimen Boolean refused"); BlendRows.push_back(Row); continue; }
                const double                  Volume = IntegrateBodyVolume(Partition, Body);
                // 📝 Seam edges (both fins on one periodic face) are chart artefacts, not selectable edges: skipped.
                std::vector<EdgeToken> Edges;
                for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
                {
                    const FinToken Fin = Partition.Edges.Resolve(Edge).RadialFin;
                    const auto     FaceOf = [&](FinToken Of) { return Partition.Loops.Resolve(Partition.Fins.Resolve(Of).Loop).Face; };
                    if (FaceOf(Fin) == FaceOf(Partition.RadialMate(Fin))) continue;
                    Edges.push_back(Edge);
                }
                Row.Edges = int(Edges.size());
                for (size_t Index = 0; Index < Edges.size(); ++Index)
                {
                    const Clock::time_point Start   = Clock::now();
                    const BlendOutcome      Outcome = BlendEdges(Partition, Body, { Edges[Index] }, { Profile, Size });
                    const double            Took    = Elapsed(Start);
                    Row.Milliseconds += Took;
                    Row.Slowest       = std::max(Row.Slowest, Took);
                    if (!Outcome.Refusal.empty())
                    {
                        ++Row.Refused;
                        if (Row.Notes.size() < 4) Row.Notes.push_back("edge " + std::to_string(Index) + ": " + Outcome.Refusal);
                        continue;
                    }
                    ++Row.Accepted;
                    if (!SoundBody(Partition, Outcome.Body)) { ++Row.Unsound; Row.Notes.push_back("edge " + std::to_string(Index) + ": unsound result"); continue; }
                    const double Change = IntegrateBodyVolume(Partition, Outcome.Body) - Volume;  // [len³]
                    const bool   Convex = Outcome.Sheets.empty() || Outcome.Sheets.front().ConvexCondition;
                    if ((Convex && Change >= 0.0) || (!Convex && Change <= 0.0))
                    {
                        ++Row.WrongSign;
                        Row.Notes.push_back("edge " + std::to_string(Index) + ": ΔV " + std::to_string(Change) + (Convex ? " on a convex edge" : " on a concave edge"));
                    }
                    // Closed form for a straight edge between perpendicular planes ending on perpendicular caps (Block).
                    if (Specimen.Name == "Block")
                    {
                        const double Along    = MeasureEdgeLength(Partition, Edges[Index]);
                        const double Expected = -(Profile == BlendProfile::Chamfer ? 0.5 : 1.0 - 0.25 * π) * Size * Size * Along;
                        if (std::fabs(Change - Expected) > VolumeTolerance * std::max(1.0, Volume))
                        {
                            ++Row.WrongVolume;
                            Row.Notes.push_back("edge " + std::to_string(Index) + ": ΔV " + std::to_string(Change) + " vs " + std::to_string(Expected));
                        }
                    }
                }
                const Clock::time_point Start = Clock::now();
                const BlendOutcome      All   = BlendEdges(Partition, Body, Edges, { Profile, Size });
                Row.AllMilliseconds = Elapsed(Start);
                Row.AllEdges = !All.Refusal.empty() ? "refused: " + All.Refusal : SoundBody(Partition, All.Body) ? "sound" : "UNSOUND";
                BlendRows.push_back(Row);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    for (int Index = 1; Index + 1 < ArgumentCount; ++Index)
    {
        if (std::string(Arguments[Index]) == "--only")   OnlyPrefix    = Arguments[Index + 1];
        if (std::string(Arguments[Index]) == "--oracle") OracleSamples = std::atoi(Arguments[Index + 1]);
    }
    const Clock::time_point Start = Clock::now();
    RunBooleanScenes();
    RunBlendScenes(0.1);
    RunBlendScenes(0.3);
    const double WallMs = Elapsed(Start);                                                    // [ms] - Includes audits, volumes, oracle
    double BooleanMs = 0.0, BlendMs = 0.0;                                                   // [ms] - Timed operations only
    for (const BooleanRow& Row : BooleanRows) BooleanMs += Row.Milliseconds;
    for (const BlendRow& Row : BlendRows) BlendMs += Row.Milliseconds + Row.AllMilliseconds;

    int BooleanFailures = 0, BlendFailures = 0;
    for (const BooleanRow& Row : BooleanRows) BooleanFailures += Row.PassedCondition ? 0 : 1;
    for (const BlendRow& Row : BlendRows) BlendFailures += Row.Unsound + Row.WrongSign + Row.WrongVolume + (Row.AllEdges.rfind("UNSOUND", 0) == 0 ? 1 : 0);

    std::printf("# GeometryProcessing — AlgorithmStressValidation\n\n");
    std::printf("**%s** · Booleans %zu steps, %d failing · %.0f ms · blends %zu rows, %d failing · %.0f ms · wall %.0f ms\n\n",
                BooleanFailures + BlendFailures == 0 ? "🟢 PASS" : "🔴 FAIL", BooleanRows.size(), BooleanFailures, BooleanMs, BlendRows.size(), BlendFailures, BlendMs, WallMs);
    std::printf("## Booleans\n\n| | Scene | Step | Faces | Pairs tested | Fragments | Volume | Closed form | ms | SSI ms | Split ms | Classify ms | Assembly ms | Maximal ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const BooleanRow& Row : BooleanRows)
    {
        char Expected[32] = "—";
        if (!std::isnan(Row.Expected)) std::snprintf(Expected, sizeof(Expected), "%.9f", Row.Expected);
        std::printf("| %s | %s | %s | %d | %d/%d | %d | %.9f | %s | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |\n", Row.Outcome.rfind("🚧", 0) == 0 ? "🚧" : Row.PassedCondition ? "🟢" : "🔴", Row.Scene.c_str(), Row.Step.c_str(),
                    Row.Faces, Row.Tested, Row.Pairs, Row.Fragments, Row.Volume, Expected, Row.Milliseconds, Row.Intersect, Row.Split, Row.Classify, Row.Assemble, Row.Maximal);
        if (!Row.PassedCondition) std::printf("|  | | ↳ %s | | | | | | | | | | | |\n", Row.Outcome.c_str());
    }
    std::printf("\n## Chamfer / fillet on every edge (one edge per call), then all edges at once\n\n");
    std::printf("| | Body | Profile | Edges | Accepted | Refused | Unsound | Wrong ΔV sign | Wrong ΔV | ms (all single) | Slowest edge ms | All at once | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const BlendRow& Row : BlendRows)
    {
        const bool Passed = Row.Unsound + Row.WrongSign + Row.WrongVolume == 0 && Row.AllEdges.rfind("UNSOUND", 0) != 0;
        std::printf("| %s | %s | %s | %d | %d | %d | %d | %d | %d | %.1f | %.1f | %s | %.1f |\n", Passed ? "🟢" : "🔴", Row.Body.c_str(), Row.Profile.c_str(), Row.Edges, Row.Accepted, Row.Refused,
                    Row.Unsound, Row.WrongSign, Row.WrongVolume, Row.Milliseconds, Row.Slowest, Row.AllEdges.c_str(), Row.AllMilliseconds);
    }
    std::printf("\n## Notes\n\n");
    for (const BlendRow& Row : BlendRows) for (const std::string& Note : Row.Notes) std::printf("- %s %s · %s\n", Row.Body.c_str(), Row.Profile.c_str(), Note.c_str());
    return BooleanFailures + BlendFailures == 0 ? 0 : 1;
}
