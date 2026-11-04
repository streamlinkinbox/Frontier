/*====================================================================================================================================
                                                     BOOLEANVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step-3 Boolean gates over the scene catalogue, every operation from ONE General Fuse per scene:
//    B1 topology + geometry audit · B2 closed-form volume · B3 volume identities · B4 membership oracle (generalized
//    winding number of the result against the Boolean of the arguments' memberships) · B5 solid / shell counts

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Boolean/BooleanCatalogue.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

    constexpr double VolumeTolerance   = 1.0e-9;                                             // [-] - Relative (absolute below 1)
    constexpr int    MembershipSamples = 120;                                                // [-] - Points per scene

    struct SceneRow
    {
        std::string Name        = {};                                                        // [-]   - Scene
        long        Checks      = 0;                                                         // [-]   - Individual checks
        long        Failures    = 0;                                                         // [-]   - Failed checks
        double      VolumeError = 0.0;                                                       // [-]   - Worst B2 deviation
        long        VolumeCompared = 0;                                                      // [-]   - B2 comparisons (closed forms)
        double      Identity    = 0.0;                                                       // [len³] - Worst B3 residual
        long        Oracle      = 0;                                                         // [-]   - B4 points compared
        long        OracleWrong = 0;                                                         // [-]   - B4 disagreements
        std::string Statistics  = {};                                                        // [-]   - GF counters
        double      Milliseconds = 0.0;                                                      // [ms]  - Scene wall time
        std::vector<std::string> Notes = {};                                                 // [-]   - Failure details
    };

    struct DetailRow
    {
        std::string Scene, Operation, Outcome;                                               // [-] - Labels
        int         Faces = 0, Shells = 0, Solids = 0;                                       // [-] - Result counts
        double      Volume = 0.0, Expected = 0.0;                                            // [len³] - B2
        double      Milliseconds = 0.0;                                                      // [ms] - Assembly + audit
    };

    std::string OnlyPrefix;                                                                  // [-] - --only filter ("" = everything)

    [[nodiscard]] bool Selected(const std::string& Name) { return OnlyPrefix.empty() || Name.rfind(OnlyPrefix, 0) == 0; }

    std::vector<SceneRow>  Rows;
    std::vector<DetailRow> Details;
    std::mt19937_64        Generator(20261022);

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }
    [[nodiscard]] double Uniform(double Low, double High) { return std::uniform_real_distribution<double>(Low, High)(Generator); }

    [[nodiscard]] double VolumeDeviation(double Measured, double Expected)
    {
        return std::fabs(Measured - Expected) / std::max(1.0, std::fabs(Expected));
    }

    [[nodiscard]] bool Combine(BooleanOperation Operation, bool InFirst, bool InSecond)
    {
        switch (Operation)
        {
            case BooleanOperation::Union:              return InFirst || InSecond;
            case BooleanOperation::Intersection:       return InFirst && InSecond;
            case BooleanOperation::Difference:         return InFirst && !InSecond;
            case BooleanOperation::ReversedDifference: return InSecond && !InFirst;
        }
        return false;
    }

    // 📝 All checks of one argument pair; DetailEnabled = false for random cases (no per-operation detail rows).
    void EvaluateScene(ModelPartition& Partition, const BooleanScene& Scene, int Samples, SceneRow& Row, bool DetailEnabled)
    {
        const std::string& Name = Scene.Name;
        const auto Note = [&](bool Pass, const std::string& Message)
        {
            ++Row.Checks;
            if (Pass) return;
            ++Row.Failures;
            if (Row.Notes.size() < 6) Row.Notes.push_back(Message);
        };

        // Arguments match their closed forms (guards the scene itself).
        Note(VolumeDeviation(IntegrateBodyVolume(Partition, Scene.First), Scene.FirstVolume) <= VolumeTolerance, "V(A) differs from its closed form");
        Note(VolumeDeviation(IntegrateBodyVolume(Partition, Scene.Second), Scene.SecondVolume) <= VolumeTolerance, "V(B) differs from its closed form");

        const GeneralFuseOutcome Fuse = RunGeneralFuse(Partition, Scene.First, Scene.Second);
        Note(Fuse.Refusal.empty(), "General Fuse refused: " + Fuse.Refusal);
        if (!Fuse.Refusal.empty()) return;
        const GeneralFuseStatistics& Counters = Fuse.Statistics;
        char Buffer[256];
        std::snprintf(Buffer, sizeof(Buffer), "%d/%d pairs · %d sect · %d paves · %d common · %d+%d frag (I%d O%d S%d X%d)",
                      Counters.FacePairsTested, Counters.FacePairs, Counters.SectionEdges, Counters.Paves, Counters.CommonBlocks,
                      Counters.Fragments[0], Counters.Fragments[1], Counters.States[0], Counters.States[1], Counters.States[2], Counters.States[3]);
        if (DetailEnabled) Row.Statistics = Buffer;

        double    Volumes[4] = { 0.0, 0.0, 0.0, 0.0 };                                       // [len³] - Measured per operation
        BodyToken Results[4] = {};
        for (int Index = 0; Index < 4; ++Index)
        {
            const auto       OperationStart = Clock::now();
            const auto       Operation      = static_cast<BooleanOperation>(Index);
            const auto&      Expected       = Scene.Expected[Index];
            const BooleanOutcome Result     = AssembleBoolean(Partition, Fuse, Operation);
            const std::string Label         = std::string(BooleanOperationLabel(Operation)) + ": ";
            DetailRow Detail{ Name, BooleanOperationLabel(Operation), "" };
            Detail.Expected = Expected.Volume;
            const bool EmptyRefusal = Result.Refusal == "Boolean: empty result";
            if (Expected.Shells == 0 || (EmptyRefusal && (std::isnan(Expected.Volume) || std::fabs(Expected.Volume) <= 1.0e-12) && Expected.Shells < 0))
            {
                Note(EmptyRefusal, Label + "expected an empty result");
                Detail.Outcome = EmptyRefusal ? "🟢 empty (refused)" : "🔴 not empty";
                if (DetailEnabled) Details.push_back(Detail);
                continue;
            }
            Note(Result.Refusal.empty(), Label + Result.Refusal);
            if (!Result.Refusal.empty()) { Detail.Outcome = "🔴 " + Result.Refusal; if (DetailEnabled) Details.push_back(Detail); continue; }
            Results[Index] = Result.Body;

            // B1 audit.
            const AuditReport Report = AuditBody(Partition, Result.Body);
            std::string FirstFinding = Report.Findings.empty() ? "" : std::string(Report.Findings.front().Code) + " " + Report.Findings.front().Subject + " " + Report.Findings.front().Detail;
            Note(Report.SoundCondition(), Label + "audit " + FirstFinding);
            // B2 volume.
            Volumes[Index] = IntegrateBodyVolume(Partition, Result.Body);
            if (!std::isnan(Expected.Volume))
            {
                const double Deviation = VolumeDeviation(Volumes[Index], Expected.Volume);
                Row.VolumeError = std::max(Row.VolumeError, Deviation);
                ++Row.VolumeCompared;
                Note(Deviation <= VolumeTolerance, Label + "volume " + std::to_string(Volumes[Index]) + " vs " + std::to_string(Expected.Volume));
            }
            // B5 counts.
            if (Expected.Solids >= 0) Note(Result.Solids == Expected.Solids, Label + "solids " + std::to_string(Result.Solids));
            if (Expected.Shells >= 0) Note(Result.Shells == Expected.Shells, Label + "shells " + std::to_string(Result.Shells));

            Detail.Outcome = Report.SoundCondition() ? "🟢 sound" : "🔴 " + FirstFinding;
            Detail.Faces   = Result.Faces;
            Detail.Shells  = Result.Shells;
            Detail.Solids  = Result.Solids;
            Detail.Volume  = Volumes[Index];
            Detail.Milliseconds = Elapsed(OperationStart);
            if (DetailEnabled) Details.push_back(Detail);
        }

        // B3 identities (an empty result has volume 0).
        const double VolumeA = Scene.FirstVolume, VolumeB = Scene.SecondVolume;
        const double UnionIdentity      = std::fabs(Volumes[0] + Volumes[1] - VolumeA - VolumeB);
        const double DifferenceIdentity = std::fabs(Volumes[2] - (VolumeA - Volumes[1]));
        const double ReversedIdentity   = std::fabs(Volumes[3] - (VolumeB - Volumes[1]));
        const double Identity = std::max({ UnionIdentity, DifferenceIdentity, ReversedIdentity });
        Row.Identity = std::max(Row.Identity, Identity);
        Note(Identity <= VolumeTolerance * std::max(1.0, VolumeA + VolumeB), "volume identities violated");

        // B4 membership oracle over the joint bounds.
        const BodyContainment First  = PrepareBodyContainment(Partition, Scene.First);
        const BodyContainment Second = PrepareBodyContainment(Partition, Scene.Second);
        std::vector<BodyContainment> Outputs(4);
        for (int Index = 0; Index < 4; ++Index) if (Results[Index].PresentCondition()) Outputs[Index] = PrepareBodyContainment(Partition, Results[Index]);
        Vector3 Low = First.Faces.front().BoundsLow, High = First.Faces.front().BoundsHigh;
        for (const BodyContainment* Set : { &First, &Second })
        {
            for (const FaceDomain& Face : Set->Faces)
            {
                Low  = { std::min(Low.X, Face.BoundsLow.X), std::min(Low.Y, Face.BoundsLow.Y), std::min(Low.Z, Face.BoundsLow.Z) };
                High = { std::max(High.X, Face.BoundsHigh.X), std::max(High.Y, Face.BoundsHigh.Y), std::max(High.Z, Face.BoundsHigh.Z) };
            }
        }
        WindingSettings Coarse;
        Coarse.Precision = 1.0e-6;                                                           // Skip points this close to a boundary
        // 💡 Screened winding (same membership, coarse quadrature first): the strict quadrature made this oracle 97 % of the
        //    run — 95 s per crossing-cylinder scene against 0.6 s for its General Fuse.
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            const Vector3 Point = { Uniform(Low.X, High.X), Uniform(Low.Y, High.Y), Uniform(Low.Z, High.Z) };
            const SolidPointClass InFirst  = ClassifySolidPointScreened(First, Point, Coarse);
            const SolidPointClass InSecond = ClassifySolidPointScreened(Second, Point, Coarse);
            if (InFirst.Membership == PointMembership::Boundary || InSecond.Membership == PointMembership::Boundary) continue;
            for (int Index = 0; Index < 4; ++Index)
            {
                const bool Expected = Combine(static_cast<BooleanOperation>(Index), InFirst.Membership == PointMembership::Inside, InSecond.Membership == PointMembership::Inside);
                bool Measured = false;
                if (Results[Index].PresentCondition())
                {
                    const SolidPointClass InResult = ClassifySolidPointScreened(Outputs[Index], Point, Coarse);
                    if (InResult.Membership == PointMembership::Boundary) continue;
                    Measured = InResult.Membership == PointMembership::Inside;
                }
                ++Row.Oracle;
                if (Measured != Expected) ++Row.OracleWrong;
            }
        }
        Note(Row.OracleWrong == 0, Name + ": membership oracle disagreements");
    }

    void ValidateScene(const std::string& Name)
    {
        const auto Start = Clock::now();
        ModelPartition Partition;
        const BooleanScene Scene = ConstructBooleanScene(Partition, Name);
        SceneRow Row;
        Row.Name = Name;
        EvaluateScene(Partition, Scene, MembershipSamples, Row, true);
        Row.Milliseconds = Elapsed(Start);
        Rows.push_back(Row);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                RANDOM FAMILIES
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] Vector3 RandomDirection()
    {
        for (;;)
        {
            const Vector3 Candidate{ Uniform(-1.0, 1.0), Uniform(-1.0, 1.0), Uniform(-1.0, 1.0) };
            const double  Magnitude = Length(Candidate);
            if (Magnitude > 0.2 && Magnitude <= 1.0) return Candidate * (1.0 / Magnitude);
        }
    }

    [[nodiscard]] OrthonormalFrame RandomFrame(Vector3 Origin)
    {
        const Vector3 Axis = RandomDirection();
        Vector3 Hint = RandomDirection();
        while (Length(Cross(Hint, Axis)) < 0.3) Hint = RandomDirection();
        return ConstructFrame(Origin, Axis, Hint);
    }

    // Convex polygon clip (Sutherland–Hodgman, counter-clockwise inputs) and shoelace area — independent of the kernel.
    [[nodiscard]] double ConvexOverlapArea(std::vector<Vector2> Subject, const std::vector<Vector2>& Clip)
    {
        for (size_t Index = 0; Index < Clip.size() && !Subject.empty(); ++Index)
        {
            const Vector2 EdgeStart = Clip[Index], EdgeEnd = Clip[(Index + 1) % Clip.size()];
            const auto Side = [&](Vector2 Point) { return Cross(EdgeEnd - EdgeStart, Point - EdgeStart); };
            std::vector<Vector2> Kept;
            for (size_t Corner = 0; Corner < Subject.size(); ++Corner)
            {
                const Vector2 Current = Subject[Corner], Next = Subject[(Corner + 1) % Subject.size()];
                const double  SideCurrent = Side(Current), SideNext = Side(Next);
                if (SideCurrent >= 0.0) Kept.push_back(Current);
                if ((SideCurrent >= 0.0) != (SideNext >= 0.0)) Kept.push_back(Current + (Next - Current) * (SideCurrent / (SideCurrent - SideNext)));
            }
            Subject = std::move(Kept);
        }
        double Area = 0.0;                                                                   // [len²] - Twice the area
        for (size_t Index = 0; Index < Subject.size(); ++Index) Area += Cross(Subject[Index], Subject[(Index + 1) % Subject.size()]);
        return 0.5 * Area;
    }

    // 📝 Sphere–sphere lens: V = π(R + r − d)²(d² + 2dr − 3r² + 2dR + 6rR − 3R²) / (12d).
    [[nodiscard]] double LensVolume(double Major, double Minor, double Separation)
    {
        const double Gap = Major + Minor - Separation;                                       // [len] - Overlap depth
        return std::numbers::pi * Gap * Gap * (Separation * Separation + 2.0 * Separation * Minor - 3.0 * Minor * Minor + 2.0 * Separation * Major + 6.0 * Minor * Major - 3.0 * Major * Major) / (12.0 * Separation);
    }

    [[nodiscard]] std::array<BooleanExpectation, 4> OverlapExpectation(double First, double Second, double Overlap)
    {
        const auto Entry = [](double Volume) { return BooleanExpectation{ Volume, -1, -1 }; };
        return { { Entry(First + Second - Overlap), Entry(Overlap), Entry(First - Overlap), Entry(Second - Overlap) } };
    }

    void ValidateRandomFamily(const std::string& Family, int Count, const std::function<BooleanScene(ModelPartition&)>& Builder)
    {
        if (!Selected(Family)) return;
        // 📝 Per-family seed (FNV-1a of the name) so a filtered run reproduces the same pairs as the full run.
        std::uint64_t Seed = 1469598103934665603ULL;
        for (char Letter : Family) Seed = (Seed ^ static_cast<unsigned char>(Letter)) * 1099511628211ULL;
        Generator.seed(Seed);
        const auto Start = Clock::now();
        SceneRow Row;
        Row.Name = Family;
        long Cases = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            ModelPartition Partition;
            BooleanScene Scene = Builder(Partition);
            Scene.Name = Family + "#" + std::to_string(Index);
            EvaluateScene(Partition, Scene, 24, Row, false);
            ++Cases;
        }
        char Buffer[96];
        std::snprintf(Buffer, sizeof(Buffer), "%ld random pairs", Cases);
        Row.Statistics   = Buffer;
        Row.Milliseconds = Elapsed(Start);
        Rows.push_back(Row);
    }

    // B6 negative control: an open argument (block with its top face detached) must be refused, never assembled.
    void ValidateOpenArgument()
    {
        if (!Selected("OpenArgument")) return;
        const auto Start = Clock::now();
        SceneRow Row;
        Row.Name = "OpenArgument (negative control)";
        ModelPartition  Partition;
        const BodyToken First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
        const BodyToken Second = ConstructBlock(Partition, { 1.0, 1.0, 1.0 }, { 2.0, 2.0, 2.0 }).Value;
        const BodyInventory Inventory = Partition.CollectInventory(First);
        long Detached = 0;
        for (FaceToken Face : Inventory.Faces)
        {
            bool Top = true;                                                                 // [-] - Every loop corner at z = 2
            for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { Top = Top && Partition.Vertices.Resolve(Partition.FinStart(Fin)).Position.Z > 1.999; });
            }
            if (!Top) continue;
            auto& Faces = Partition.Shells.Resolve(Partition.Faces.Resolve(Face).Shell).Faces;
            Faces.erase(std::remove(Faces.begin(), Faces.end(), Face), Faces.end());
            ++Detached;
        }
        const GeneralFuseOutcome Fuse = RunGeneralFuse(Partition, First, Second);
        bool Refused = !Fuse.Refusal.empty();
        for (int Index = 0; Index < 4 && !Refused; ++Index) Refused = !AssembleBoolean(Partition, Fuse, static_cast<BooleanOperation>(Index)).Refusal.empty();
        ++Row.Checks;
        if (Detached != 1 || !Refused) { ++Row.Failures; Row.Notes.push_back(Detached != 1 ? "top face not found" : "an open argument was assembled without refusal"); }
        Row.Statistics   = Refused ? "refused: " + (Fuse.Refusal.empty() ? std::string("at assembly") : Fuse.Refusal) : "not refused";
        Row.Milliseconds = Elapsed(Start);
        Rows.push_back(Row);
    }

    void ValidateRandomFamilies()
    {
        ValidateRandomFamily("RandomBlocks", 40, [](ModelPartition& Partition)
        {
            Vector3 CornerA, ExtentA, CornerB, ExtentB;
            double  Overlap = 1.0;                                                           // [len³] - Box overlap
            for (;;)
            {
                CornerA = { Uniform(0.0, 0.5), Uniform(0.0, 0.5), Uniform(0.0, 0.5) };
                ExtentA = { Uniform(0.4, 1.2), Uniform(0.4, 1.2), Uniform(0.4, 1.2) };
                CornerB = { Uniform(0.0, 1.0), Uniform(0.0, 1.0), Uniform(0.0, 1.0) };
                ExtentB = { Uniform(0.3, 1.0), Uniform(0.3, 1.0), Uniform(0.3, 1.0) };
                Overlap = 1.0;
                bool Separated = false;
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    const double LowA  = Axis == 0 ? CornerA.X : Axis == 1 ? CornerA.Y : CornerA.Z;
                    const double HighA = LowA + (Axis == 0 ? ExtentA.X : Axis == 1 ? ExtentA.Y : ExtentA.Z);
                    const double LowB  = Axis == 0 ? CornerB.X : Axis == 1 ? CornerB.Y : CornerB.Z;
                    const double HighB = LowB + (Axis == 0 ? ExtentB.X : Axis == 1 ? ExtentB.Y : ExtentB.Z);
                    const double Shared = std::min(HighA, HighB) - std::max(LowA, LowB);     // [len] - Interval overlap
                    if (Shared < 0.05 || std::fabs(LowA - LowB) < 0.02 || std::fabs(HighA - HighB) < 0.02) Separated = true;
                    Overlap *= Shared;
                }
                if (!Separated) break;
            }
            BooleanScene Scene;
            Scene.First  = ConstructBlock(Partition, CornerA, ExtentA).Value;
            Scene.Second = ConstructBlock(Partition, CornerB, ExtentB).Value;
            Scene.FirstVolume  = ExtentA.X * ExtentA.Y * ExtentA.Z;
            Scene.SecondVolume = ExtentB.X * ExtentB.Y * ExtentB.Z;
            Scene.Expected = OverlapExpectation(Scene.FirstVolume, Scene.SecondVolume, Overlap);
            return Scene;
        });

        ValidateRandomFamily("RandomTurnedPrisms", 30, [](ModelPartition& Partition)
        {
            const auto Rectangle = [](Vector2 Centre, double HalfX, double HalfY, double Angle)
            {
                std::vector<Vector2> Corners;
                for (const Vector2 Local : { Vector2{ -HalfX, -HalfY }, Vector2{ HalfX, -HalfY }, Vector2{ HalfX, HalfY }, Vector2{ -HalfX, HalfY } })
                {
                    Corners.push_back({ Centre.X + Local.X * std::cos(Angle) - Local.Y * std::sin(Angle), Centre.Y + Local.X * std::sin(Angle) + Local.Y * std::cos(Angle) });
                }
                return Corners;
            };
            const std::vector<Vector2> OutlineA = Rectangle({ 0.0, 0.0 }, Uniform(0.5, 1.0), Uniform(0.5, 1.0), Uniform(0.0, 1.5));
            const std::vector<Vector2> OutlineB = Rectangle({ Uniform(-0.6, 0.6), Uniform(-0.6, 0.6) }, Uniform(0.4, 0.9), Uniform(0.4, 0.9), Uniform(0.0, 1.5));
            const double LowA = 0.0, HighA = Uniform(0.8, 1.5);                              // [len] - A height range
            const double LowB = Uniform(0.1, 0.6), HighB = LowB + Uniform(0.5, 1.5);         // [len] - B height range
            const auto Prism = [&](const std::vector<Vector2>& Outline, double Low, double High)
            {
                std::vector<Vector3> Profile;
                for (const Vector2& Corner : Outline) Profile.push_back({ Corner.X, Corner.Y, Low });
                return ConstructPrism(Partition, Profile, { 0.0, 0.0, High - Low }).Value.Body;
            };
            BooleanScene Scene;
            Scene.First  = Prism(OutlineA, LowA, HighA);
            Scene.Second = Prism(OutlineB, LowB, HighB);
            Scene.FirstVolume  = ConvexOverlapArea(OutlineA, OutlineA) * (HighA - LowA);
            Scene.SecondVolume = ConvexOverlapArea(OutlineB, OutlineB) * (HighB - LowB);
            const double Shared = std::max(0.0, std::min(HighA, HighB) - std::max(LowA, LowB));
            Scene.Expected = OverlapExpectation(Scene.FirstVolume, Scene.SecondVolume, ConvexOverlapArea(OutlineA, OutlineB) * Shared);
            return Scene;
        });

        ValidateRandomFamily("RandomSpheres", 30, [](ModelPartition& Partition)
        {
            const double Major = Uniform(0.6, 1.0), Minor = Uniform(0.4, 0.9);                 // [len] - Radii
            const double Separation = Uniform(std::fabs(Major - Minor) + 0.08, Major + Minor - 0.08);
            const Vector3 Centre = RandomDirection() * Separation;                            // [len] - Second centre
            BooleanScene Scene;
            Scene.First  = ConstructSphere(Partition, RandomFrame({ 0.0, 0.0, 0.0 }), Major).Value;
            Scene.Second = ConstructSphere(Partition, RandomFrame(Centre), Minor).Value;
            Scene.FirstVolume  = 4.0 / 3.0 * std::numbers::pi * Major * Major * Major;
            Scene.SecondVolume = 4.0 / 3.0 * std::numbers::pi * Minor * Minor * Minor;
            Scene.Expected = OverlapExpectation(Scene.FirstVolume, Scene.SecondVolume, LensVolume(Major, Minor, Separation));
            return Scene;
        });

        ValidateRandomFamily("RandomBlockSphere", 30, [](ModelPartition& Partition)
        {
            const double Radius = Uniform(0.3, 0.8);                                          // [len] - Sphere radius
            const Vector3 Centre = { Uniform(-0.3, 2.3), Uniform(-0.3, 1.8), Uniform(-0.3, 1.3) };
            BooleanScene Scene;
            Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }).Value;
            Scene.Second = ConstructSphere(Partition, RandomFrame(Centre), Radius).Value;
            Scene.FirstVolume  = 3.0;
            Scene.SecondVolume = 4.0 / 3.0 * std::numbers::pi * Radius * Radius * Radius;
            Scene.Expected = OverlapExpectation(Scene.FirstVolume, Scene.SecondVolume, std::nan(""));
            return Scene;
        });

        ValidateRandomFamily("RandomDrills", 30, [](ModelPartition& Partition)
        {
            const double Radius = Uniform(0.15, 0.6);                                         // [len] - Drill radius
            const double Turn   = Uniform(0.0, 6.2);                                          // [rad] - Seam azimuth
            const Vector3 Foot  = { Uniform(-0.2, 2.2), Uniform(-0.2, 1.7), Uniform(-0.8, 0.5) };
            const double Height = Uniform(0.7, 2.5);                                          // [len] - Drill length
            BooleanScene Scene;
            Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }).Value;
            Scene.Second = ConstructCylinder(Partition, ConstructFrame(Foot, { 0.0, 0.0, 1.0 }, { std::cos(Turn), std::sin(Turn), 0.0 }), Radius, Height).Value;
            Scene.FirstVolume  = 3.0;
            Scene.SecondVolume = std::numbers::pi * Radius * Radius * Height;
            Scene.Expected = OverlapExpectation(Scene.FirstVolume, Scene.SecondVolume, std::nan(""));
            return Scene;
        });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

// B2 cell: "—" when the row has no closed form (identities and the oracle still apply).
[[nodiscard]] static std::string VolumeText(const SceneRow& Row)
{
    if (Row.VolumeCompared == 0) return "— (no closed form)";
    char Buffer[32];
    std::snprintf(Buffer, sizeof(Buffer), "%.1e", Row.VolumeError);
    return Buffer;
}

int main(int ArgumentCount, char** Arguments)
{
    bool DetailEnabled = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--detail") DetailEnabled = true;
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlyPrefix = Arguments[++Index];
    }
    for (const std::string& Name : EnumerateBooleanScenes())
    {
        if (!Selected(Name)) continue;
        ValidateScene(Name);
        std::fprintf(stderr, "  %-20s %s\n", Name.c_str(), Rows.back().Failures == 0 ? "ok" : "FAILING");
    }
    ValidateRandomFamilies();
    ValidateOpenArgument();

    long FailingRows = 0;
    std::printf("# GeometryProcessing — BooleanValidation\n\n");
    std::printf("| | Scene | Checks | Failures | B2 max rel. ΔV | B3 identity residual | B4 oracle wrong / points | General Fuse | ms |\n|---|---|---|---|---|---|---|---|---|\n");
    for (const SceneRow& Row : Rows)
    {
        FailingRows += Row.Failures > 0 ? 1 : 0;
        std::printf("| %s | %s | %ld | %ld | %s | %.1e | %ld / %ld | %s | %.0f |\n", Row.Failures == 0 ? "🟢" : "🔴", Row.Name.c_str(), Row.Checks, Row.Failures,
                    VolumeText(Row).c_str(), Row.Identity, Row.OracleWrong, Row.Oracle, Row.Statistics.c_str(), Row.Milliseconds);
    }
    for (const SceneRow& Row : Rows)
    {
        for (const std::string& Message : Row.Notes) std::printf("\n- 🔴 %s: %s", Row.Name.c_str(), Message.c_str());
    }
    std::printf("\n**%s** — %ld failing scene(s); allowances: audit sound, |ΔV|/max(1, V) ≤ 1e-9, identities ≤ 1e-9·max(1, V_A + V_B), 0 oracle disagreements\n",
                FailingRows == 0 ? "PASS" : "FAIL", FailingRows);
    if (DetailEnabled)
    {
        std::printf("\n## Results per operation\n\n| Scene | Operation | Outcome | Faces | Shells | Solids | Volume | Closed form | ms |\n|---|---|---|---|---|---|---|---|---|\n");
        for (const DetailRow& Row : Details)
        {
            std::printf("| %s | %s | %s | %d | %d | %d | %.12f | %.12f | %.1f |\n", Row.Scene.c_str(), Row.Operation.c_str(), Row.Outcome.c_str(), Row.Faces, Row.Shells, Row.Solids,
                        Row.Volume, Row.Expected, Row.Milliseconds);
        }
    }
    return FailingRows == 0 ? 0 : 1;
}
