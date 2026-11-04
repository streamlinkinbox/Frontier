/*====================================================================================================================================
                                                      SKETCHVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 7 (research § M): sketch corpus DOF / redundancy / conflict, closed-form positions, starts, scale, singularity

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Sketch/SketchCatalogue.h"
#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
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

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - S1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Pass condition (text)
        double      Worst    = 0.0;                                                          // [-] - Worst value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
    }

    GateTally DiagnosisGate   = { "S1", "DOF (structural = measured unless singular) · redundant count · conflict groups (with the expected labels) vs hand count", "all equal" };
    GateTally PositionGate    = { "S2", "Solved positions vs closed form (law of cosines, tangency, mirror, …)", "≤ 10⁻¹²·L" };
    GateTally StartGate       = { "S3", "Random starts ±0.1·L (200 per specimen): converged with every constraint met", "≥ 99 %" };
    GateTally MotionGate      = { "S4", "Minimum-norm motion: re-dimensioning a free rectangle splits the change evenly", "≤ 10⁻¹²·L" };
    GateTally FreedomGate     = { "S5", "Fully-constrained point flags (null space of J) vs hand analysis", "all equal" };
    GateTally ScaleGate       = { "S6", "Scale 10⁻³ … 10³: same rank / DOF, relative position error", "≤ 10⁻¹²" };
    GateTally SingularGate    = { "S7", "Positional singularity flagged exactly where expected (structural vs measured rank, or conditioning < 10⁻³ × generic)", "all equal" };
    GateTally ProfileGate     = { "S8", "Profile loops: area vs closed form · refusals where expected", "rel. ≤ 10⁻¹²" };
    GateTally DeterminismGate = { "S9", "Same sketch twice → bit-identical positions and diagnosis", "equal" };

    [[nodiscard]] std::string ConstraintLabel(const SketchModel& Sketch, int Index)
    {
        const SketchConstraint& Constraint = Sketch.Constraints[Index];
        return Constraint.Label.empty() ? std::string(SketchConstraintLabel(Constraint.Kind)) + "#" + std::to_string(Index) : Constraint.Label;
    }

    [[nodiscard]] std::string JoinLabels(const SketchModel& Sketch, const std::vector<int>& Indices)
    {
        std::string Text;
        for (int Index : Indices) Text += (Text.empty() ? "" : ", ") + ConstraintLabel(Sketch, Index);
        return Text.empty() ? "–" : Text;
    }

    [[nodiscard]] double MeasureSpan(const SketchModel& Sketch)
    {
        Vector2 Low{ 1e300, 1e300 }, High{ -1e300, -1e300 };
        for (const SketchPoint& Point : Sketch.Points)
        {
            Low  = { std::min(Low.X, Point.Position.X), std::min(Low.Y, Point.Position.Y) };
            High = { std::max(High.X, Point.Position.X), std::max(High.Y, Point.Position.Y) };
        }
        return std::max(1.0e-300, Distance(Low, High));
    }

    [[nodiscard]] double PositionError(const SketchModel& Sketch, const std::vector<std::pair<int, Vector2>>& Expected, double Factor)
    {
        double Worst = 0.0;
        for (const auto& [Point, Position] : Expected) Worst = std::max(Worst, Distance(Sketch.Points[Point].Position, Position * Factor));
        return Worst;
    }

    [[nodiscard]] uint64_t NextRandom(uint64_t& Seed)
    {
        Seed = Seed * 6364136223846793005ull + 1442695040888963407ull;
        return Seed >> 11;
    }

    [[nodiscard]] double UniformSymmetric(uint64_t& Seed) { return static_cast<double>(NextRandom(Seed)) / 9007199254740992.0 * 2.0 - 1.0; }

    struct SpecimenRow
    {
        std::string Name, Description, Redundant, Conflicts, Profile;                        // [-]    - Labels
        int         Unknowns = 0, Equations = 0, Rank = 0, StructuralRank = 0;               // [-]    - System size
        int         Dof = 0, ExpectedDof = 0, Iterations = 0;                                // [-]    - DOF and steps
        bool        Singular = false;                                                        // [-]    - Positional singularity
        double      Conditioning = 1.0;                                                      // [-]    - min |R_kk| / |R_00|
        double      Residual = 0.0, PositionError = -1.0, Area = 0.0, AreaError = -1.0;      // [len]  - Measurements
        double      Milliseconds = 0.0;                                                      // [ms]   - Solve time
        bool        PassedCondition = true;                                                  // [-]    - Every gate for this row
    };

    struct StartRow
    {
        std::string Name;                                                                    // [-]   - Specimen
        double      Amplitude = 0.0;                                                         // [-]   - Start spread / L
        int         Trials = 0, Converged = 0, DrawnBranch = 0;                              // [-]   - Counts
        double      MeanIterations = 0.0, MeanMilliseconds = 0.0;                            // [-]   - Averages
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const auto  Start = Clock::now();
    std::string OnlyPrefix;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlyPrefix = Arguments[++Index];
    }
    const std::vector<SketchSpecimen> Catalogue = BuildSketchCatalogue();
    std::vector<SpecimenRow> Rows;

    // ---- S1 S2 S5 S7 S8 S9 over the corpus.
    for (const SketchSpecimen& Specimen : Catalogue)
    {
        if (!OnlyPrefix.empty() && Specimen.Name.rfind(OnlyPrefix, 0) != 0) continue;
        SpecimenRow Row;
        Row.Name        = Specimen.Name;
        Row.Description = Specimen.Description;
        SketchModel Sketch = Specimen.Sketch;
        const SketchSolveOutcome Outcome = SolveSketch(Sketch);
        Row.Unknowns = Outcome.Unknowns; Row.Equations = Outcome.Equations; Row.Rank = Outcome.Rank; Row.StructuralRank = Outcome.StructuralRank;
        Row.Dof = Outcome.DegreesOfFreedom; Row.ExpectedDof = Specimen.ExpectedDegreesOfFreedom; Row.Iterations = Outcome.Iterations;
        Row.Singular = Outcome.SingularCondition; Row.Conditioning = Outcome.Conditioning; Row.Residual = Outcome.MaxResidual; Row.Milliseconds = Outcome.Milliseconds;
        Row.Redundant = JoinLabels(Sketch, Outcome.Redundant);
        for (const std::vector<int>& Group : Outcome.ConflictGroups) Row.Conflicts += (Row.Conflicts.empty() ? "{" : " {") + JoinLabels(Sketch, Group) + "}";
        if (Row.Conflicts.empty()) Row.Conflicts = "–";

        // S1: DOF, redundancy, conflicts (and convergence when none conflict).
        bool LabelsCondition = true;
        for (const std::string& Label : Specimen.ExpectedConflictLabels) LabelsCondition = LabelsCondition && Row.Conflicts.find(Label) != std::string::npos;
        const int  StructuralDof      = Outcome.Unknowns - Outcome.StructuralRank;
        const bool DofCondition       = StructuralDof == Specimen.ExpectedDegreesOfFreedom && (Outcome.SingularCondition || Outcome.DegreesOfFreedom == StructuralDof);
        const bool DiagnosisCondition = DofCondition && static_cast<int>(Outcome.Redundant.size()) == Specimen.ExpectedRedundant &&
                                        static_cast<int>(Outcome.ConflictGroups.size()) == Specimen.ExpectedConflictGroups && LabelsCondition &&
                                        (Specimen.ExpectedConflictGroups > 0 || Outcome.Converged);
        Tally(DiagnosisGate, DiagnosisCondition ? 0.0 : 1.0, DiagnosisCondition);
        Row.PassedCondition = Row.PassedCondition && DiagnosisCondition;

        // S2: closed-form positions.
        const double Span = MeasureSpan(Sketch);
        if (!Specimen.ExpectedPositions.empty())
        {
            Row.PositionError = PositionError(Sketch, Specimen.ExpectedPositions, 1.0);
            const bool PositionCondition = Row.PositionError <= 1.0e-12 * Span;
            Tally(PositionGate, Row.PositionError / Span, PositionCondition);
            Row.PassedCondition = Row.PassedCondition && PositionCondition;
        }

        // S5: fixed-point flags.
        if (!Specimen.ExpectedFixedPoints.empty())
        {
            bool FreedomCondition = true;
            for (size_t Point = 0; Point < Sketch.Points.size(); ++Point)
            {
                bool Expected = false;
                if (Specimen.ExpectedFixedPoints[0] == -1) Expected = true;
                else if (Specimen.ExpectedFixedPoints[0] != -2) Expected = std::find(Specimen.ExpectedFixedPoints.begin(), Specimen.ExpectedFixedPoints.end(), static_cast<int>(Point)) != Specimen.ExpectedFixedPoints.end();
                FreedomCondition = FreedomCondition && Outcome.PointFixed[Point] == Expected;
            }
            Tally(FreedomGate, FreedomCondition ? 0.0 : 1.0, FreedomCondition);
            Row.PassedCondition = Row.PassedCondition && FreedomCondition;
        }

        // S7: singularity flag.
        const bool SingularCondition = Outcome.SingularCondition == Specimen.ExpectedSingular;
        Tally(SingularGate, SingularCondition ? 0.0 : 1.0, SingularCondition);
        Row.PassedCondition = Row.PassedCondition && SingularCondition;

        // S8: profile.
        const SketchProfile Profile = ExtractSketchProfile(Sketch);
        Row.Profile = Profile.Refusal.empty() ? std::to_string(Profile.Loops.size()) + " loop(s)" : "refused: " + Profile.Refusal;
        bool ProfileCondition = Specimen.ExpectedProfile == Profile.Refusal.empty();
        if (Profile.Refusal.empty()) Row.Area = Profile.Area;
        if (Profile.Refusal.empty() && !std::isnan(Specimen.ExpectedArea))
        {
            Row.AreaError = std::fabs(Profile.Area - Specimen.ExpectedArea) / Specimen.ExpectedArea;
            ProfileCondition = ProfileCondition && Row.AreaError <= 1.0e-12;
        }
        if (Specimen.Name == "BowTie") ProfileCondition = ProfileCondition && Profile.Refusal.find("crosses itself") != std::string::npos;
        Tally(ProfileGate, Row.AreaError > 0.0 ? Row.AreaError : (ProfileCondition ? 0.0 : 1.0), ProfileCondition);
        Row.PassedCondition = Row.PassedCondition && ProfileCondition;

        // S9: determinism.
        SketchModel Again = Specimen.Sketch;
        const SketchSolveOutcome Repeat = SolveSketch(Again);
        bool SameCondition = Repeat.Rank == Outcome.Rank && Repeat.Redundant == Outcome.Redundant && Repeat.ConflictGroups == Outcome.ConflictGroups;
        for (size_t Point = 0; Point < Sketch.Points.size(); ++Point)
        {
            SameCondition = SameCondition && Again.Points[Point].Position.X == Sketch.Points[Point].Position.X && Again.Points[Point].Position.Y == Sketch.Points[Point].Position.Y;
        }
        Tally(DeterminismGate, SameCondition ? 0.0 : 1.0, SameCondition);
        Row.PassedCondition = Row.PassedCondition && SameCondition;
        Rows.push_back(Row);
    }

    // ---- S3 random starts.
    std::vector<StartRow> StartRows;
    const char* StartSpecimens[] = { "TriangleSides", "Slot", "PointOnCurves", "TangentCircles", "PlateWithHole" };
    for (const SketchSpecimen& Specimen : Catalogue)
    {
        if (std::find_if(std::begin(StartSpecimens), std::end(StartSpecimens), [&](const char* Name) { return Specimen.Name == Name; }) == std::end(StartSpecimens)) continue;
        if (!OnlyPrefix.empty() && OnlyPrefix != "Start" && Specimen.Name.rfind(OnlyPrefix, 0) != 0) continue;
        for (double Amplitude : { 0.1, 0.3 })
        {
            StartRow Row{ Specimen.Name, Amplitude };
            const double Span = MeasureSpan(Specimen.Sketch);
            uint64_t Seed = 0xA0761D6478BD642Full;
            for (int Trial = 0; Trial < 200; ++Trial)
            {
                SketchModel Sketch = Specimen.Sketch;
                for (SketchPoint& Point : Sketch.Points) Point.Position = Point.Position + Vector2{ UniformSymmetric(Seed), UniformSymmetric(Seed) } * (Amplitude * Span);
                for (SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == SketchCurveKind::Circle) Curve.Radius *= 1.0 + 0.5 * Amplitude * UniformSymmetric(Seed);
                const SketchSolveOutcome Outcome = SolveSketch(Sketch);
                ++Row.Trials;
                const bool Met = Outcome.Converged && Outcome.ConflictGroups.empty();
                Row.Converged      += Met ? 1 : 0;
                Row.DrawnBranch    += Met && PositionError(Sketch, Specimen.ExpectedPositions, 1.0) <= 1.0e-9 * Span ? 1 : 0;
                Row.MeanIterations += Outcome.Iterations;
                Row.MeanMilliseconds += Outcome.Milliseconds;
            }
            Row.MeanIterations   /= Row.Trials;
            Row.MeanMilliseconds /= Row.Trials;
            const double Rate = static_cast<double>(Row.Converged) / Row.Trials;
            if (Amplitude == 0.1) Tally(StartGate, 1.0 - Rate, Rate >= 0.99);
            StartRows.push_back(Row);
        }
    }

    // ---- S4 minimum-norm motion: a solved 3 × 2 rectangle re-dimensioned to width 4 → each side moves 0.5.
    double MotionError = -1.0;
    if (OnlyPrefix.empty() || OnlyPrefix == "Motion")
    {
        SketchModel Sketch;
        AppendSketchRectangle(Sketch, { 0.0, 0.0 }, { 3.0, 2.0 }, 1);
        AppendSketchConstraint(Sketch, SketchConstraintKind::HorizontalDistance, 0, 1, -1, 4.0, "Width");
        const SketchSolveOutcome Outcome = SolveSketch(Sketch);
        MotionError = PositionError(Sketch, { { 0, { -0.5, 0.0 } }, { 1, { 3.5, 0.0 } }, { 2, { 3.5, 2.0 } }, { 3, { -0.5, 2.0 } } }, 1.0);
        Tally(MotionGate, MotionError / 4.0, Outcome.Converged && MotionError <= 1.0e-12 * 4.0);
    }

    // ---- S6 scale.
    struct ScaleRow { double Factor; double Errors[3]; int Ranks[3]; };
    std::vector<ScaleRow> ScaleRows;
    const char* ScaleSpecimens[] = { "FixedRectangle", "TriangleSides", "Slot" };
    if (OnlyPrefix.empty() || OnlyPrefix == "Scale")
    {
        for (double Factor : { 1.0e-3, 1.0e-2, 1.0e-1, 1.0, 1.0e1, 1.0e2, 1.0e3 })
        {
            ScaleRow Row{ Factor, { 0.0, 0.0, 0.0 }, { 0, 0, 0 } };
            for (int Column = 0; Column < 3; ++Column)
            {
                const SketchSpecimen& Specimen = *std::find_if(Catalogue.begin(), Catalogue.end(), [&](const SketchSpecimen& Candidate) { return Candidate.Name == ScaleSpecimens[Column]; });
                SketchModel Reference = Specimen.Sketch;
                const SketchSolveOutcome ReferenceSolve = SolveSketch(Reference);
                SketchModel Scaled = ScaleSketch(Specimen.Sketch, Factor);
                const SketchSolveOutcome Outcome = SolveSketch(Scaled);
                const double Span = MeasureSpan(Scaled);
                Row.Errors[Column] = PositionError(Scaled, Specimen.ExpectedPositions, Factor) / Span;
                Row.Ranks[Column]  = Outcome.Rank;
                Tally(ScaleGate, Row.Errors[Column], Outcome.Converged && Outcome.Rank == ReferenceSolve.Rank && Outcome.DegreesOfFreedom == ReferenceSolve.DegreesOfFreedom && Row.Errors[Column] <= 1.0e-12);
            }
            ScaleRows.push_back(Row);
        }
    }

    // ---- Throughput: a 60-segment zig-zag chain (lengths + angles), DOF 0.
    double ChainMilliseconds = 0.0, ChainConditioning = 0.0;
    bool   ChainSingular = true;
    int    ChainUnknowns = 0, ChainDof = -1, ChainIterations = 0;
    bool   ChainConverged = false;
    if (OnlyPrefix.empty() || OnlyPrefix == "Chain")
    {
        SketchModel Sketch;
        constexpr int Segments = 60;
        for (int Index = 0; Index <= Segments; ++Index) AppendSketchPoint(Sketch, { 0.9 * Index, (Index % 2) * 0.6 + 0.05 * std::sin(Index) });
        for (int Index = 0; Index < Segments; ++Index) AppendSketchLine(Sketch, Index, Index + 1, Index + 1, true);
        AppendSketchConstraint(Sketch, SketchConstraintKind::Fixed, 0, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, SketchConstraintKind::Horizontal, 0, -1, -1, 0.0, "Level");
        for (int Index = 0; Index < Segments; ++Index)
        {
            AppendSketchConstraint(Sketch, SketchConstraintKind::Length, Index, -1, -1, 1.0, "L" + std::to_string(Index));
            if (Index > 0) AppendSketchConstraint(Sketch, SketchConstraintKind::Angle, Index - 1, Index, -1, (Index % 2 ? -1.0 : 1.0) * std::numbers::pi / 3.0, "A" + std::to_string(Index));
        }
        const SketchSolveOutcome Outcome = SolveSketch(Sketch);
        ChainMilliseconds = Outcome.Milliseconds; ChainUnknowns = Outcome.Unknowns; ChainDof = Outcome.DegreesOfFreedom;
        ChainIterations = Outcome.Iterations; ChainConverged = Outcome.Converged; ChainConditioning = Outcome.Conditioning; ChainSingular = Outcome.SingularCondition;
    }

    // ---- Report.
    const GateTally* Gates[] = { &DiagnosisGate, &PositionGate, &StartGate, &MotionGate, &FreedomGate, &ScaleGate, &SingularGate, &ProfileGate, &DeterminismGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !OnlyPrefix.empty());
    if (OnlyPrefix.empty()) PassedCondition = PassedCondition && ChainConverged && ChainDof == 0 && ChainSingular == false;
    int Passed = 0;
    for (const SpecimenRow& Row : Rows) Passed += Row.PassedCondition ? 1 : 0;
    std::printf("# Sketch validation (step 7, M)\n\n");
    if (!OnlyPrefix.empty()) std::printf("> 🔍 Filtered run: `--only %s` (gates with 0 checks were not exercised)\n\n", OnlyPrefix.c_str());
    std::printf("**%s** · corpus %d/%zu · chain %d unknowns DOF %d in %d steps %.1f ms (conditioning %.0e, not singular) · %.0f ms total\n\n", PassedCondition ? "🟢 PASS" : "🔴 FAIL", Passed, Rows.size(),
                ChainUnknowns, ChainDof, ChainIterations, ChainMilliseconds, ChainConditioning, Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Corpus\n\n| | Sketch | What it is | n | m | rank (structural) | DOF (hand) | Redundant | Conflict groups | Singular (conditioning) | Steps | max residual | position error | Profile | area rel. error | ms |\n"
                "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const SpecimenRow& Row : Rows)
    {
        char PositionText[32] = "–", AreaText[32] = "–";
        if (Row.PositionError >= 0.0) std::snprintf(PositionText, sizeof(PositionText), "%.1e", Row.PositionError);
        if (Row.AreaError >= 0.0) std::snprintf(AreaText, sizeof(AreaText), "%.1e", Row.AreaError);
        std::printf("| %s | %s | %s | %d | %d | %d (%d) | %d (%d) | %s | %s | %s (%.0e) | %d | %.1e | %s | %s | %s | %.2f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Description.c_str(),
                    Row.Unknowns, Row.Equations, Row.Rank, Row.StructuralRank, Row.Dof, Row.ExpectedDof, Row.Redundant.c_str(), Row.Conflicts.c_str(), Row.Singular ? "⚠️ yes" : "no", Row.Conditioning,
                    Row.Iterations, Row.Residual, PositionText, Row.Profile.c_str(), AreaText, Row.Milliseconds);
    }
    if (!StartRows.empty())
    {
        std::printf("\n## Random starts (S3)\n\n| | Sketch | spread / L | trials | converged | drawn branch | mean steps | mean ms |\n|---|---|---|---|---|---|---|---|\n");
        for (const StartRow& Row : StartRows)
        {
            const bool Gated = Row.Amplitude == 0.1;
            const double Rate = 100.0 * Row.Converged / Row.Trials;
            std::printf("| %s | %s | %.1f | %d | %.1f %% | %.1f %% | %.1f | %.2f |\n", Gated ? (Rate >= 99.0 ? "🟢" : "🔴") : "🔍", Row.Name.c_str(), Row.Amplitude, Row.Trials, Rate,
                        100.0 * Row.DrawnBranch / Row.Trials, Row.MeanIterations, Row.MeanMilliseconds);
        }
        std::printf("\n> 💡 0.1·L is gated; 0.3·L is informational (a start that far from the drawing may legitimately reach another branch).\n");
    }
    if (MotionError >= 0.0) std::printf("\n## Minimum-norm motion (S4)\n\nWidth 3 → 4 on a free rectangle: each vertical side moves 0.5, nothing else moves · error %.1e\n", MotionError);
    if (!ScaleRows.empty())
    {
        std::printf("\n## Scale (S6)\n\n| | s | %s rel. error (rank) | %s rel. error (rank) | %s rel. error (rank) |\n|---|---|---|---|---|\n", ScaleSpecimens[0], ScaleSpecimens[1], ScaleSpecimens[2]);
        for (const ScaleRow& Row : ScaleRows)
        {
            const double Worst = std::max({ Row.Errors[0], Row.Errors[1], Row.Errors[2] });
            std::printf("| %s | %.0e | %.1e (%d) | %.1e (%d) | %.1e (%d) |\n", Worst <= 1.0e-12 ? "🟢" : "🔴", Row.Factor, Row.Errors[0], Row.Ranks[0], Row.Errors[1], Row.Ranks[1], Row.Errors[2], Row.Ranks[2]);
        }
    }
    std::fflush(stdout);
    return PassedCondition ? 0 : 1;
}
