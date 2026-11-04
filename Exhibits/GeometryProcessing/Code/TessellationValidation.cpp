/*====================================================================================================================================
                                                  TESSELLATIONVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 4 (N) gates: watertight shared-edge CDT tessellation over specimens, Boolean results and random Boolean results

#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Boolean/BooleanCatalogue.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Inspection/SpecimenCatalogue.h"
#include "Engine/GeometryProcessing/Tessellation/ChartTriangulation.h"

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
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    std::string OnlyPrefix;                                                                  // [-] - --only filter
    [[nodiscard]] bool Selected(const std::string& Name) { return OnlyPrefix.empty() || Name.rfind(OnlyPrefix, 0) == 0; }

    // 📝 One row aggregates every body tessellated for a specimen, scene or random family.
    struct GateRow
    {
        std::string              Name;                                                       // [-]   - Row label
        int                      Bodies         = 0;                                         // [-]   - Bodies tessellated
        int                      Skipped        = 0;                                         // [-]   - Empty / refused Boolean results
        int64_t                  Triangles      = 0;                                         // [-]   - Σ triangles
        int64_t                  Unpaired       = 0;                                         // [-]   - N1 sides not paired
        int                      EulerMismatch  = 0;                                         // [-]   - N1 bodies with χ ≠ χ_B-rep
        int64_t                  Unused         = 0;                                         // [-]   - N1 unreferenced vertices
        double                   PeakRatio      = 0.0;                                       // [-]   - N2 max deviation / ε
        int64_t                  Inverted       = 0;                                         // [-]   - N3 inverted triangles
        int64_t                  Degenerate     = 0;                                         // [-]   - N3 zero-area triangles
        int64_t                  Folded         = 0;                                         // [-]   - N3 folded (tessellator's own test)
        double                   VolumeRatio    = 0.0;                                       // [-]   - N4 max |ΔV| / (ε·A)
        int64_t                  Delaunay       = 0;                                         // [-]   - N5 residual before legalisation
        int64_t                  EdgeSides      = 0;                                         // [-]   - N6 edge-sample sides
        int64_t                  Missing        = 0;                                         // [-]   - N6 sides not matched
        double                   MinimumAngle   = 180.0;                                     // [deg] - N7 smallest angle
        double                   Below20        = 0.0;                                       // [-]   - N7 Σ triangles below 20°
        double                   Below20Free    = 0.0;                                       // [-]   - N7 same, no boundary side
        double                   Measured       = 0.0;                                       // [-]   - N7 Σ non-degenerate triangles
        int                      Refusals       = 0;                                         // [-]   - Tessellator refusals
        int                      StarRepairs    = 0;                                         // [-]   - Singular stars repaired
        int                      FoldFlips      = 0;                                         // [-]   - Folded slivers flipped
        int                      ClaimedSplits  = 0;                                         // [-]   - Shared interior sides split
        int64_t                  CavityRepairs  = 0;                                         // [-]   - Bowyer–Watson cavity repairs
        double                   Milliseconds   = 0.0;                                       // [ms]  - Tessellation time only
        std::vector<std::string> Notes;                                                      // [-]   - Failure details
    };

    std::vector<GateRow> Rows;

    [[nodiscard]] bool RowPassed(const GateRow& Row)
    {
        return Row.Refusals == 0 && Row.Unpaired == 0 && Row.EulerMismatch == 0 && Row.Unused == 0 && Row.PeakRatio <= 1.0 && Row.Inverted == 0 &&
               Row.Degenerate == 0 && Row.Folded == 0 && Row.VolumeRatio <= 1.0 && Row.Delaunay == 0 && Row.Missing == 0 && Row.Bodies > 0;
    }

    // Tessellate one body, audit it, and fold the result into a row.
    TessellationOutcome Measure(GateRow& Row, const ModelPartition& Partition, BodyToken Body, const std::string& Label, double SagRatio = 1.0e-3)
    {
        TessellationSettings Settings;
        Settings.SagRatio = SagRatio;
        const TessellationOutcome     Outcome = TessellateBody(Partition, Body, Settings);
        const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
        Row.Bodies       += 1;
        Row.Milliseconds += Outcome.Milliseconds;
        if (!Outcome.Refusal.empty())
        {
            Row.Refusals += 1;
            Row.Notes.push_back(Label + ": refused — " + Outcome.Refusal);
            return Outcome;
        }
        Row.Triangles     += Report.Triangles;
        Row.Unpaired      += Report.UnpairedSides;
        Row.EulerMismatch += Report.EulerComplex != Report.EulerBrep ? 1 : 0;
        Row.Unused        += Report.UnusedVertices;
        Row.PeakRatio      = std::max(Row.PeakRatio, Report.PeakDeviation / Outcome.Sag);
        Row.Inverted      += Report.Inverted;
        Row.Degenerate    += Report.Degenerate;
        Row.VolumeRatio    = std::max(Row.VolumeRatio, std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea));
        Row.EdgeSides     += Report.EdgeSides;
        Row.Missing       += Report.MissingEdgeSides;
        Row.MinimumAngle   = std::min(Row.MinimumAngle, Report.MinimumAngle);
        const double Measured = static_cast<double>(Report.Triangles - Report.Degenerate);
        Row.Below20       += Report.ShareBelow20 * Measured;
        Row.Measured      += Measured;
        Row.CavityRepairs += Outcome.Triangulation.CavityRepairs;
        for (const PolygonComplexFace& Face : Outcome.Complex.Faces)
        {
            Row.Delaunay    += Face.DelaunayViolations;
            Row.Folded      += Face.FoldedResidual;
            Row.StarRepairs += Face.StarRepairs;
            Row.FoldFlips += Face.FoldFlips;
            Row.ClaimedSplits += Face.ClaimedSplits;
        }
        char Buffer[256];
        if (Report.UnpairedSides || Report.EulerComplex != Report.EulerBrep || Report.Inverted || Report.Degenerate || Report.MissingEdgeSides ||
            Report.PeakDeviation > Outcome.Sag || std::fabs(Report.ComplexVolume - Report.ExactVolume) > Outcome.Sag * Report.ComplexArea)
        {
            std::snprintf(Buffer, sizeof(Buffer), "%s: unpaired %lld, χ %d vs %d, inverted %lld, degenerate %lld, missing %lld, peak/ε %.3f, |ΔV|/(εA) %.3f", Label.c_str(),
                          static_cast<long long>(Report.UnpairedSides), Report.EulerComplex, Report.EulerBrep, static_cast<long long>(Report.Inverted),
                          static_cast<long long>(Report.Degenerate), static_cast<long long>(Report.MissingEdgeSides), Report.PeakDeviation / Outcome.Sag,
                          std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea));
            Row.Notes.push_back(Buffer);
        }
        return Outcome;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CORPUS
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateSpecimens()
    {
        for (const std::string& Name : EnumerateSpecimens())
        {
            if (!Selected(Name)) continue;
            ModelPartition        Partition;
            const SpecimenOutcome Specimen = ConstructSpecimen(Partition, Name, {});
            GateRow Row;
            Row.Name = Name;
            if (Specimen.Refusal.empty()) Measure(Row, Partition, Specimen.Body, Name);
            else Row.Notes.push_back("specimen refused: " + Specimen.Refusal);
            Rows.push_back(Row);
            std::fprintf(stderr, "  %-22s %s\n", Name.c_str(), RowPassed(Row) ? "ok" : "FAILING");
        }
    }

    constexpr BooleanOperation Operations[4] = { BooleanOperation::Union, BooleanOperation::Intersection, BooleanOperation::Difference,
                                                 BooleanOperation::ReversedDifference };

    // Every non-empty result of the four operations.
    void MeasureScene(GateRow& Row, ModelPartition& Partition, const BooleanScene& Scene)
    {
        for (BooleanOperation Operation : Operations)
        {
            const BooleanOutcome Result = ComputeBoolean(Partition, Scene.First, Scene.Second, Operation);
            if (!Result.Refusal.empty() || Result.Faces == 0)
            {
                Row.Skipped += 1;
                continue;
            }
            Measure(Row, Partition, Result.Body, Scene.Name + " " + BooleanOperationLabel(Operation));
        }
    }

    void ValidateBooleanScenes()
    {
        for (const std::string& Name : EnumerateBooleanScenes())
        {
            if (!Selected(Name)) continue;
            ModelPartition Partition;
            BooleanScene   Scene = ConstructBooleanScene(Partition, Name);
            Scene.Name = Name;
            GateRow Row;
            Row.Name = Name + " (4 ops)";
            MeasureScene(Row, Partition, Scene);
            Rows.push_back(Row);
            std::fprintf(stderr, "  %-22s %s\n", Name.c_str(), RowPassed(Row) ? "ok" : "FAILING");
        }
    }

    std::mt19937_64 Generator(20261020);

    [[nodiscard]] double Uniform(double Low, double High) { return std::uniform_real_distribution<double>(Low, High)(Generator); }

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

    void ValidateRandomFamily(const std::string& Family, int Count, const std::function<BooleanScene(ModelPartition&)>& Builder)
    {
        if (!Selected(Family)) return;
        std::uint64_t Seed = 1469598103934665603ULL;                                         // [-] - FNV-1a of the family name
        for (char Letter : Family) Seed = (Seed ^ static_cast<unsigned char>(Letter)) * 1099511628211ULL;
        Generator.seed(Seed);
        GateRow Row;
        Row.Name = Family + " (" + std::to_string(Count) + " pairs × 4 ops)";
        for (int Index = 0; Index < Count; ++Index)
        {
            ModelPartition Partition;
            BooleanScene   Scene = Builder(Partition);
            Scene.Name = Family + "#" + std::to_string(Index);
            MeasureScene(Row, Partition, Scene);
        }
        Rows.push_back(Row);
        std::fprintf(stderr, "  %-22s %s\n", Family.c_str(), RowPassed(Row) ? "ok" : "FAILING");
    }

    void ValidateRandomFamilies()
    {
        ValidateRandomFamily("RandomSpheres", 20, [](ModelPartition& Partition)
        {
            const double  Major = Uniform(0.6, 1.0), Minor = Uniform(0.4, 0.9);              // [len] - Radii
            const double  Separation = Uniform(std::fabs(Major - Minor) + 0.08, Major + Minor - 0.08);
            const Vector3 Centre = RandomDirection() * Separation;                           // [len] - Second centre
            BooleanScene Scene;
            Scene.First  = ConstructSphere(Partition, RandomFrame({ 0.0, 0.0, 0.0 }), Major).Value;
            Scene.Second = ConstructSphere(Partition, RandomFrame(Centre), Minor).Value;
            return Scene;
        });
        ValidateRandomFamily("RandomBlockSphere", 20, [](ModelPartition& Partition)
        {
            const double  Radius = Uniform(0.3, 0.8);                                        // [len] - Sphere radius
            const Vector3 Centre = { Uniform(-0.3, 2.3), Uniform(-0.3, 1.8), Uniform(-0.3, 1.3) };
            BooleanScene Scene;
            Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }).Value;
            Scene.Second = ConstructSphere(Partition, RandomFrame(Centre), Radius).Value;
            return Scene;
        });
        ValidateRandomFamily("RandomDrills", 20, [](ModelPartition& Partition)
        {
            const double  Radius = Uniform(0.15, 0.6);                                       // [len] - Drill radius
            const double  Turn   = Uniform(0.0, 6.2);                                        // [rad] - Seam azimuth
            const Vector3 Foot   = { Uniform(-0.2, 2.2), Uniform(-0.2, 1.7), Uniform(-0.8, 0.5) };
            const double  Height = Uniform(0.7, 2.5);                                        // [len] - Drill length
            BooleanScene Scene;
            Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }).Value;
            Scene.Second = ConstructCylinder(Partition, ConstructFrame(Foot, { 0.0, 0.0, 1.0 }, { std::cos(Turn), std::sin(Turn), 0.0 }), Radius, Height).Value;
            return Scene;
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CONVERGENCE, DETERMINISM, SCALE
    //--------------------------------------------------------------------------------------------------------------------

    struct ConvergenceRow
    {
        std::string         Name;                                                            // [-]   - Specimen
        std::vector<double> Sag;                                                             // [len] - ε per level
        std::vector<double> VolumeError;                                                     // [len³] - |V_mesh − V_exact|
        std::vector<double> PeakRatio;                                                       // [-]   - Peak deviation / ε
        std::vector<int>    Triangles;                                                       // [-]   - Triangles per level
        double              VolumeSlope   = 0.0;                                             // [-]   - d log|ΔV| / d log ε
        double              TriangleSlope = 0.0;                                             // [-]   - d log N / d log ε
        bool                Passed        = false;                                           // [-]   - Gate N8
    };
    std::vector<ConvergenceRow> Convergence;

    [[nodiscard]] double FitSlope(const std::vector<double>& X, const std::vector<double>& Y)
    {
        double MeanX = 0.0, MeanY = 0.0;
        for (size_t Index = 0; Index < X.size(); ++Index) { MeanX += X[Index]; MeanY += Y[Index]; }
        MeanX /= static_cast<double>(X.size());
        MeanY /= static_cast<double>(Y.size());
        double Covariance = 0.0, Variance = 0.0;
        for (size_t Index = 0; Index < X.size(); ++Index)
        {
            Covariance += (X[Index] - MeanX) * (Y[Index] - MeanY);
            Variance   += (X[Index] - MeanX) * (X[Index] - MeanX);
        }
        return Covariance / Variance;
    }

    // 📝 N8: a planar facet over a surface of curvature κ deviates by ~κh²/8, so at fixed shape ε ∝ h² and the triangle
    //    count N ∝ h⁻² ∝ ε⁻¹; the volume defect is ~ε·A, so |ΔV| ∝ ε. Both slopes should be ≈ ±1.
    void ValidateConvergence()
    {
        if (!Selected("Convergence")) return;
        for (const char* Name : { "Sphere", "Torus", "Cone", "Cylinder" })
        {
            ConvergenceRow Row;
            Row.Name = Name;
            std::vector<double> LogSag, LogVolume, LogTriangles;
            for (double Ratio : { 1.0e-2, 3.0e-3, 1.0e-3, 3.0e-4, 1.0e-4 })
            {
                ModelPartition        Partition;
                const SpecimenOutcome Specimen = ConstructSpecimen(Partition, Name, {});
                TessellationSettings  Settings;
                Settings.SagRatio = Ratio;
                const TessellationOutcome     Outcome = TessellateBody(Partition, Specimen.Body, Settings);
                const TessellationAuditReport Report  = AuditTessellation(Partition, Specimen.Body, Outcome);
                const double Error = std::fabs(Report.ComplexVolume - Report.ExactVolume);
                Row.Sag.push_back(Outcome.Sag);
                Row.VolumeError.push_back(Error);
                Row.PeakRatio.push_back(Report.PeakDeviation / Outcome.Sag);
                Row.Triangles.push_back(Report.Triangles);
                LogSag.push_back(std::log(Outcome.Sag));
                LogVolume.push_back(std::log(Error));
                LogTriangles.push_back(std::log(static_cast<double>(Report.Triangles)));
            }
            Row.VolumeSlope   = FitSlope(LogSag, LogVolume);
            Row.TriangleSlope = FitSlope(LogSag, LogTriangles);
            Row.Passed = Row.VolumeSlope > 0.85 && Row.VolumeSlope < 1.15 && Row.TriangleSlope > -1.15 && Row.TriangleSlope < -0.85 &&
                         *std::max_element(Row.PeakRatio.begin(), Row.PeakRatio.end()) <= 1.0;
            Convergence.push_back(Row);
            std::fprintf(stderr, "  Convergence %-10s %s\n", Name, Row.Passed ? "ok" : "FAILING");
        }
    }

    struct DeterminismRow
    {
        std::string Name;                                                                    // [-] - Body
        uint64_t    First  = 0;                                                              // [-] - Digest, run 1
        uint64_t    Second = 0;                                                              // [-] - Digest, run 2 (fresh partition)
        int         Triangles = 0;                                                           // [-] - Triangle count
    };
    std::vector<DeterminismRow> Determinism;

    // N9: identical inputs in fresh partitions give bitwise-identical complexes.
    void ValidateDeterminism()
    {
        if (!Selected("Determinism")) return;
        const auto Digest = [](const std::string& Scene, int Operation, int& Triangles)
        {
            ModelPartition Partition;
            BodyToken      Body;
            if (Operation < 0) Body = ConstructSpecimen(Partition, Scene, {}).Body;
            else
            {
                const BooleanScene Built = ConstructBooleanScene(Partition, Scene);
                Body = ComputeBoolean(Partition, Built.First, Built.Second, Operations[Operation]).Body;
            }
            const TessellationOutcome Outcome = TessellateBody(Partition, Body, {});
            Triangles = static_cast<int>(Outcome.Complex.Triangles.size());
            return DigestPolygonComplex(Outcome.Complex);
        };
        const std::pair<const char*, int> Cases[] = { { "Sphere", -1 }, { "Torus", -1 }, { "DrilledBlock", 2 }, { "SphereCraterTilted", 2 }, { "NapkinRing", 2 } };
        for (const auto& [Scene, Operation] : Cases)
        {
            DeterminismRow Row;
            Row.Name   = std::string(Scene) + (Operation < 0 ? "" : std::string(" ") + BooleanOperationLabel(Operations[Operation]));
            Row.First  = Digest(Scene, Operation, Row.Triangles);
            Row.Second = Digest(Scene, Operation, Row.Triangles);
            Determinism.push_back(Row);
        }
    }

    struct ScaleRow
    {
        double Scale     = 1.0;                                                              // [-] - Uniform scale
        int    Triangles = 0;                                                                // [-] - Triangle count
        double PeakRatio = 0.0;                                                              // [-] - Peak deviation / ε
        double Relative  = 0.0;                                                              // [-] - |ΔV| / V
    };
    std::vector<ScaleRow> Scales;

    // N10: ε relative to the diagonal makes the complex scale-free — triangle count within 1 % from 10⁻³ to 10³ (refinement
    //      decisions compare rounded quantities, so the counts differ by a few triangles, not bitwise).
    void ValidateScale()
    {
        if (!Selected("Scale")) return;
        for (double Scale : { 1.0e-3, 1.0, 1.0e3 })
        {
            ModelPartition Partition;
            const BodyToken Body = ConstructTorus(Partition, ConstructFrame({ 0.3 * Scale, -0.2 * Scale, 0.1 * Scale }, { 0.2, 0.3, 1.0 }, { 1.0, 0.0, 0.0 }),
                                                  2.0 * Scale, 0.5 * Scale).Value;
            const TessellationOutcome     Outcome = TessellateBody(Partition, Body, {});
            const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
            Scales.push_back({ Scale, Report.Triangles, Report.PeakDeviation / Outcome.Sag, std::fabs(Report.ComplexVolume - Report.ExactVolume) / Report.ExactVolume });
        }
    }

    struct RegressionRow
    {
        std::string Name;                                                                    // [-] - Case family
        int         Cases     = 0;                                                           // [-] - Polygons triangulated
        int         Failures  = 0;                                                           // [-] - Lost segments or wrong counts
        int         Points    = 0;                                                           // [-] - Polygon vertices (total)
    };
    std::vector<RegressionRow> Regressions;

    // A closed polygon must keep all n segments and give n − 2 interior triangles.
    [[nodiscard]] bool TriangulatePolygon(const std::vector<Vector2>& Polygon, Vector2 Low, Vector2 High)
    {
        ChartTriangulation Tiling(Low, High);
        std::vector<int>   Indices;
        for (const Vector2& Point : Polygon) Indices.push_back(Tiling.InsertPoint(Point));
        for (size_t Index = 0; Index < Indices.size(); ++Index)
        {
            if (Indices[Index] < 0 || !Tiling.ConstrainSegment(Indices[Index], Indices[(Index + 1) % Indices.size()])) return false;
        }
        Tiling.Legalize();
        Tiling.ClassifyInterior();
        size_t Interior = 0;
        for (const ChartTriangle& Triangle : Tiling.Triangles()) Interior += Triangle.Alive && Triangle.Interior ? 1 : 0;
        return Interior == Polygon.size() - 2;
    }

    // N11: CDT regressions found by the random families. ① RandomBlockSphere #8 face 13: a crossed side ending at an
    //      enclosing-triangle corner, whose open fan the side lookup must search both ways; collinear runs along v = 1.
    //      ② Random rectangles with collinear runs whose top chain dips to within δ of its own line (doubly queued crossings).
    void ValidateRegressions()
    {
        if (!Selected("Regressions")) return;
        const std::vector<Vector2> HullFan = {
        { 0.000000, 0.000000 }, { 0.500000, 0.000000 }, { 1.000000, 0.000000 }, { 1.500000, 0.000000 }, { 2.000000, 0.000000 },
        { 2.000000, 0.480746 }, { 1.950802, 0.443106 }, { 1.894933, 0.416353 }, { 1.834765, 0.401622 }, { 1.772855, 0.399541 },
        { 1.702401, 0.413037 }, { 1.637152, 0.442845 }, { 1.580827, 0.487268 }, { 1.536634, 0.543773 }, { 1.507091, 0.609143 },
        { 1.493882, 0.679651 }, { 1.497759, 0.751281 }, { 1.518502, 0.819952 }, { 1.554927, 0.881751 }, { 1.604961, 0.933156 },
        { 1.665752, 0.971240 }, { 1.733837, 0.993832 }, { 1.805336, 0.999644 }, { 1.876176, 0.988347 }, { 1.942320, 0.960583 },
        { 2.000000, 0.917934 }, { 2.000000, 1.000000 }, { 1.500000, 1.000000 }, { 1.000000, 1.000000 }, { 0.500000, 1.000000 },
        { 0.000000, 1.000000 }, { 0.000000, 0.500000 }
        };
        RegressionRow Captured{ "Hull-fan side lookup (RandomBlockSphere #8 face 13)", 1, TriangulatePolygon(HullFan, { -0.1, -0.1 }, { 2.1, 1.1 }) ? 0 : 1,
                                static_cast<int>(HullFan.size()) };
        Regressions.push_back(Captured);

        RegressionRow Random{ "Dipped rectangles with collinear runs (δ = 10⁻¹…10⁻⁶)", 0, 0, 0 };
        std::mt19937_64 Source(0x5eed1157ULL);
        std::uniform_real_distribution<double> Unit(0.0, 1.0);
        for (int Case = 0; Case < 200; ++Case)
        {
            // A rectangle (collinear runs on all four sides) whose x-monotone top chain dips to within δ of the line v = H
            // between runs of points lying exactly on it.
            const int    Run   = 2 + static_cast<int>(Unit(Source) * 6.0);                       // [-] - Points per straight side
            const double Width = 1.0 + Unit(Source) * 2.0, Height = 0.5 + Unit(Source);           // [-]
            const double Gap   = std::pow(10.0, -1.0 - 5.0 * Unit(Source));                      // [-] - Dip clearance δ
            const double Centre = Width * (0.3 + 0.4 * Unit(Source)), Radius = 0.25 * Width * Unit(Source) + 0.05;
            const int    Dip    = 4 + static_cast<int>(Unit(Source) * 12.0);                       // [-] - Dip points
            std::vector<Vector2> Top;
            for (int Index = 1; Index <= 3 * Run; ++Index)
            {
                const double X = Width * Index / (3 * Run + 1);
                if (std::fabs(X - Centre) >= Radius) Top.push_back({ X, Height });
            }
            for (int Index = 1; Index < Dip; ++Index)
            {
                const double Angle = std::numbers::pi * Index / Dip;
                const double X     = Centre + Radius * std::cos(Angle);
                if (X > 0.0 && X < Width) Top.push_back({ X, Height - Gap - 0.5 * Height * std::sin(Angle) * Unit(Source) });
            }
            std::sort(Top.begin(), Top.end(), [](Vector2 Left, Vector2 Right) { return Left.X > Right.X; });
            std::vector<Vector2> Simple;
            for (int Index = 0; Index < Run; ++Index) Simple.push_back({ Width * Index / Run, 0.0 });
            for (int Index = 0; Index < Run; ++Index) Simple.push_back({ Width, Height * Index / Run });
            Simple.push_back({ Width, Height });
            Simple.insert(Simple.end(), Top.begin(), Top.end());
            for (int Index = Run; Index > 0; --Index) Simple.push_back({ 0.0, Height * Index / Run });
            Random.Cases  += 1;
            Random.Points += static_cast<int>(Simple.size());
            Random.Failures += TriangulatePolygon(Simple, { -0.2, -0.2 }, { Width + 0.2, Height + 0.2 }) ? 0 : 1;
        }
        Regressions.push_back(Random);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlyPrefix = Arguments[++Index];
    }
    const Clock::time_point Start = Clock::now();
    ValidateSpecimens();
    ValidateBooleanScenes();
    ValidateRandomFamilies();
    ValidateConvergence();
    ValidateDeterminism();
    ValidateScale();
    ValidateRegressions();

    int Failures = 0;
    std::printf("# GeometryProcessing — TessellationValidation\n\n");
    std::printf("ε = 10⁻³ × body diagonal unless stated. Every quantity is recomputed by `AuditTessellation` from the PolygonComplex and the B-rep.\n\n");
    std::printf("| | Corpus row | Bodies | Triangles | N1 unpaired · χ · unused | N2 peak / ε | N3 inverted · degenerate · folded | N4 max \\|ΔV\\|/(ε·A) | N5 Delaunay residual | N6 missing / edge sides | N7 min angle · < 20° | Repairs star · flip · split | ms |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    int64_t Bodies = 0, Triangles = 0;
    for (const GateRow& Row : Rows)
    {
        const bool Passed = RowPassed(Row);
        Failures += Passed ? 0 : 1;
        Bodies    += Row.Bodies;
        Triangles += Row.Triangles;
        std::printf("| %s | %s | %d%s | %lld | %lld · %d · %lld | %.3f | %lld · %lld · %lld | %.3f | %lld | %lld / %lld | %.1f° · %.1f%% | %d · %d · %d | %.0f |\n", Passed ? "🟢" : "🔴",
                    Row.Name.c_str(), Row.Bodies, Row.Skipped ? (" (+" + std::to_string(Row.Skipped) + " empty)").c_str() : "", static_cast<long long>(Row.Triangles),
                    static_cast<long long>(Row.Unpaired), Row.EulerMismatch, static_cast<long long>(Row.Unused), Row.PeakRatio, static_cast<long long>(Row.Inverted),
                    static_cast<long long>(Row.Degenerate), static_cast<long long>(Row.Folded), Row.VolumeRatio, static_cast<long long>(Row.Delaunay),
                    static_cast<long long>(Row.Missing), static_cast<long long>(Row.EdgeSides), Row.MinimumAngle, Row.Measured > 0 ? 100.0 * Row.Below20 / Row.Measured : 0.0,
                    Row.StarRepairs, Row.FoldFlips, Row.ClaimedSplits, Row.Milliseconds);
    }
    for (const GateRow& Row : Rows)
    {
        for (const std::string& Note : Row.Notes) std::printf("\n- 🔴 %s", Note.c_str());
    }
    std::printf("\n\nCorpus: %lld bodies, %lld triangles.\n", static_cast<long long>(Bodies), static_cast<long long>(Triangles));

    if (!Convergence.empty())
    {
        std::printf("\n## N8 — Convergence (ε = 10⁻², 3·10⁻³, 10⁻³, 3·10⁻⁴, 10⁻⁴ × diagonal)\n\n");
        std::printf("| | Specimen | Triangles | \\|ΔV\\| | Peak / ε | Slope log\\|ΔV\\| / log ε | Slope log N / log ε |\n|---|---|---|---|---|---|---|\n");
        for (const ConvergenceRow& Row : Convergence)
        {
            Failures += Row.Passed ? 0 : 1;
            std::string Counts, Errors, Peaks;
            for (size_t Index = 0; Index < Row.Sag.size(); ++Index)
            {
                char Buffer[64];
                std::snprintf(Buffer, sizeof(Buffer), "%s%d", Index ? " → " : "", Row.Triangles[Index]);
                Counts += Buffer;
                std::snprintf(Buffer, sizeof(Buffer), "%s%.1e", Index ? " → " : "", Row.VolumeError[Index]);
                Errors += Buffer;
                std::snprintf(Buffer, sizeof(Buffer), "%s%.3f", Index ? " · " : "", Row.PeakRatio[Index]);
                Peaks += Buffer;
            }
            std::printf("| %s | %s | %s | %s | %s | %.3f | %.3f |\n", Row.Passed ? "🟢" : "🔴", Row.Name.c_str(), Counts.c_str(), Errors.c_str(), Peaks.c_str(), Row.VolumeSlope,
                        Row.TriangleSlope);
        }
        std::printf("\nGate: volume slope ∈ (0.85, 1.15), triangle slope ∈ (−1.15, −0.85), peak ≤ ε at every level.\n");
    }
    if (!Determinism.empty())
    {
        std::printf("\n## N9 — Determinism (fresh partition per run)\n\n| | Body | Triangles | Digest run 1 | Digest run 2 |\n|---|---|---|---|---|\n");
        for (const DeterminismRow& Row : Determinism)
        {
            const bool Passed = Row.First == Row.Second;
            Failures += Passed ? 0 : 1;
            std::printf("| %s | %s | %d | %016llx | %016llx |\n", Passed ? "🟢" : "🔴", Row.Name.c_str(), Row.Triangles, static_cast<unsigned long long>(Row.First),
                        static_cast<unsigned long long>(Row.Second));
        }
    }
    if (!Scales.empty())
    {
        bool Passed = true;
        for (const ScaleRow& Row : Scales)
        {
            Passed = Passed && std::abs(Row.Triangles - Scales.front().Triangles) <= Scales.front().Triangles / 100 && Row.PeakRatio <= 1.0;
        }
        Failures += Passed ? 0 : 1;
        std::printf("\n## N10 — Scale invariance (tilted torus R = 2s, r = 0.5s; triangle count within 1 %%)\n\n| | Scale s | Triangles | Peak / ε | \\|ΔV\\| / V |\n|---|---|---|---|---|\n");
        for (const ScaleRow& Row : Scales)
        {
            std::printf("| %s | %.0e | %d | %.3f | %.2e |\n", Passed ? "🟢" : "🔴", Row.Scale, Row.Triangles, Row.PeakRatio, Row.Relative);
        }
    }
    if (!Regressions.empty())
    {
        std::printf("\n## N11 — CDT regressions (every segment recovered, n − 2 interior triangles)\n\n| | Case | Polygons | Vertices | Failures |\n|---|---|---|---|---|\n");
        for (const RegressionRow& Row : Regressions)
        {
            Failures += Row.Failures == 0 ? 0 : 1;
            std::printf("| %s | %s | %d | %d | %d |\n", Row.Failures == 0 ? "🟢" : "🔴", Row.Name.c_str(), Row.Cases, Row.Points, Row.Failures);
        }
    }
    std::printf("\n**%s** — %d failing row(s); %.1f s\n", Failures == 0 ? "PASS" : "FAIL", Failures, Elapsed(Start) / 1000.0);
    std::printf("\nAllowances: N1 0 unpaired sides, χ(complex) = V − E + F − (L − F) of the B-rep, 0 unused vertices · N2 peak deviation ≤ ε (7 barycentric samples per "
                "triangle, closed-form projection) · N3 0 inverted, 0 zero-area, 0 folded · N4 |ΔV| ≤ ε·A · N5 0 · N6 every edge-sample side used once per fin, opposite "
                "directions · N7 reported only.\n");
    return Failures == 0 ? 0 : 1;
}
