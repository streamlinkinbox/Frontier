/*====================================================================================================================================
                                                     BLENDVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 5 (I) gates: rolling-ball fillets and chamfers on the radial-edge topology against closed forms and oracles

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/BlendCatalogue.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"
#include "TessellationExport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
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
    constexpr double π = std::numbers::pi;                                                   // [rad] - π

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    // 📝 One gate: worst measured quantity against its limit.
    struct GateTally
    {
        const char* Code    = "";                                                            // [-] - B1 …
        const char* Subject = "";                                                            // [-] - What is measured
        const char* Limit   = "";                                                            // [-] - Pass condition (text)
        double      Worst   = 0.0;                                                           // [-] - Worst value
        long        Checks  = 0;                                                             // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
    }

    struct SpecimenRow
    {
        std::string Name, Family, Outcome;                                                   // [-]    - Labels / refusal text
        int         Edges = 0, Faces = 0, Genus = 0;                                         // [-]    - Selection and result
        double      Delta = 0.0, Expected = 0.0, Error = 0.0;                                // [len³] - Volume change
        std::string Carriers, Vertices;                                                      // [-]    - Statistics summaries
        int64_t     Triangles = 0;                                                           // [-]    - B5 tessellation
        double      Milliseconds = 0.0;                                                      // [ms]   - Blend wall time
        bool        PassedCondition = true;                                                  // [-]    - All gates on this row
    };

    [[nodiscard]] double SurfaceDistance(const SurfaceGeometry& Surface, Vector3 Position)
    {
        return Distance(Position, EvaluateSurface(Surface, ProjectOntoSurface(Surface, Position, {})).Position);
    }

    [[nodiscard]] const SurfaceGeometry& FaceSurface(const ModelPartition& Partition, FaceToken Face)
    {
        return Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
    }

    [[nodiscard]] Vector3 OutwardNormal(const ModelPartition& Partition, FaceToken Face, Vector3 Position)
    {
        const SurfaceGeometry& Surface = FaceSurface(Partition, Face);
        const Vector3 Normal = EvaluateSurfaceNormal(Surface, ProjectOntoSurface(Surface, Position, {}));
        return Partition.Faces.Resolve(Face).Sense == Orientation::Forward ? Normal : -Normal;
    }

    [[nodiscard]] double BodyScale(const ModelPartition& Partition, BodyToken Body)
    {
        Vector3 Low = { 1e300, 1e300, 1e300 }, High = -Low;
        for (VertexToken Vertex : Partition.CollectInventory(Body).Vertices)
        {
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
            High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
        }
        return std::max(1.0, Length(High - Low));
    }

    // 📝 Faces of the source body incident to a vertex (the three supports of a ball corner).
    [[nodiscard]] std::vector<FaceToken> FacesAround(const ModelPartition& Partition, BodyToken Body, VertexToken Vertex)
    {
        std::vector<FaceToken> Faces;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    if (Partition.FinStart(Fin) == Vertex && std::find(Faces.begin(), Faces.end(), Face) == Faces.end()) Faces.push_back(Face);
                });
            }
        }
        return Faces;
    }

    GateTally VolumeGate      = { "B1", "Volume change vs closed form", "\\|ΔV − ΔV*\\| ≤ 10⁻⁹·max(1, V)" };
    GateTally TopologyGate    = { "B2", "Audit sound · genus kept · face count · source untouched", "all hold" };
    GateTally ContactGate     = { "B3", "Fillet G1 across contact / corner edges · chamfer setback", "≤ 10⁻¹⁰ rad · ≤ 10⁻⁹·L" };
    GateTally BallGate        = { "B4", "Rolling-ball law: spine ↔ supports = r, blend face ↔ spine = r, corner centre ↔ 3 supports = r", "≤ 10⁻⁹·L" };
    GateTally TessellationGate = { "B5", "Tessellation N1 · N3 · N4 · N6 on every blended body", "0 · 0 · ≤ 1 · 0" };
    GateTally MembershipGate  = { "B6", "Generalized winding number vs analytic clearance oracle", "0 wrong" };
    GateTally BooleanGate     = { "B7", "Booleans downstream of blends (volume, audit)", "rel. ≤ 10⁻⁷, sound" };
    GateTally RefusalGate     = { "B8", "Refusals: atomic, prefixed, source intact", "all refused" };
    GateTally DeterminismGate = { "B9", "Same specimen twice → identical tessellation digest", "equal" };
    std::string ExportDirectory;                                                             // [-] - --export: tessellation JSON per built specimen
    std::string OnlyPrefix;                                                                  // [-] - --only filter (specimen prefix; "Scale" selects B10)
    [[nodiscard]] bool Selected(const std::string& Name) { return OnlyPrefix.empty() || Name.rfind(OnlyPrefix, 0) == 0; }

    GateTally ScaleGate       = { "B10", "Scale 10⁻³ … 10³: relative ΔV error", "≤ 10⁻⁹" };

    // ⚠️ Measures B3 / B4 on a successful blend; returns false on any violation.
    bool MeasureBlendLaws(const ModelPartition& Partition, BodyToken Source, const BlendOutcome& Outcome, const BlendSettings& Settings, const TessellationOutcome& Tessellation, double Scale)
    {
        bool PassedCondition = true;
        std::map<uint32_t, const EdgeBlendSheet*> SheetOf;
        for (const EdgeBlendSheet& Sheet : Outcome.Sheets) SheetOf[Sheet.Edge.Slot] = &Sheet;
        std::map<uint32_t, const BlendFaceHistory*> HistoryOf;
        for (const BlendFaceHistory& History : Outcome.FaceHistories) HistoryOf[History.Result.Slot] = &History;
        const double Limit = 1.0e-9 * Scale;                                                 // [len]

        // ---- B4 ①: every spine sample is r from both supports (exact inversions, independent of the kernel's residual).
        for (const EdgeBlendSheet& Sheet : Outcome.Sheets)
        {
            const EdgeProfile& Edge = Partition.Edges.Resolve(Sheet.Edge);
            for (int Sample = 0; Sample <= 32; ++Sample)
            {
                const Vector3 Probe  = EvaluateCurve(Partition.Curves.Resolve(Edge.Curve), Edge.IntervalLow + (Edge.IntervalHigh - Edge.IntervalLow) * Sample / 32.0).Position;
                const double  Foot   = ProjectOntoCurve(Sheet.Spine, Probe, 0.0);
                const Vector3 Centre = EvaluateCurve(Sheet.Spine, Foot).Position;
                for (int Side = 0; Side < 2; ++Side)
                {
                    const double Residual = std::fabs(SurfaceDistance(FaceSurface(Partition, Sheet.Faces[Side]), Centre) - MeasureSheetRadius(Sheet, Foot));
                    Tally(BallGate, Residual / Scale, Residual <= Limit);
                    PassedCondition = PassedCondition && Residual <= Limit;
                }
            }
        }

        // ---- B4 ②: tessellation vertices of fillet faces are r from their spine / corner centre; corner centre r from its supports.
        const PolygonComplex& Complex = Tessellation.Complex;
        for (size_t Index = 0; Index < Complex.Faces.size(); ++Index)
        {
            const auto Found = HistoryOf.find(Complex.Faces[Index].Face.Slot);
            if (Found == HistoryOf.end() || Settings.Profile != BlendProfile::Fillet) continue;
            const BlendFaceHistory& History = *Found->second;
            const PolygonComplexFace& Span  = Complex.Faces[Index];
            if (History.Origin == BlendFaceOrigin::Edge)
            {
                const EdgeBlendSheet& Sheet = *SheetOf.at(History.Edge.Slot);
                for (int Triangle = Span.FirstTriangle; Triangle < Span.FirstTriangle + Span.TriangleCount; ++Triangle)
                {
                    for (int Corner : Complex.Triangles[Triangle])
                    {
                        const Vector3 Position = Complex.Positions[Corner];
                        const double  Residual = std::fabs(MeasureEnvelopeResidual(Sheet, Position));   // variable: distance to the cone of balls
                        Tally(BallGate, Residual / Scale, Residual <= Limit);
                        PassedCondition = PassedCondition && Residual <= Limit;
                    }
                }
            }
            else if (History.Origin == BlendFaceOrigin::Vertex)
            {
                const auto* Sphere = std::get_if<SphereSurface>(&FaceSurface(Partition, History.Result));
                if (Sphere == nullptr) continue;
                for (FaceToken Support : FacesAround(Partition, Source, History.Vertex))
                {
                    const double Residual = std::fabs(SurfaceDistance(FaceSurface(Partition, Support), Sphere->Frame.Origin) - Settings.Size);
                    Tally(BallGate, Residual / Scale, Residual <= Limit);
                    PassedCondition = PassedCondition && Residual <= Limit;
                }
            }
        }

        // ---- B3: walk the result's edges; classify by the provenance of the two faces.
        for (EdgeToken Edge : Partition.CollectInventory(Outcome.Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            const FinToken     First   = Profile.RadialFin;
            const FinToken     Second  = Partition.RadialMate(First);
            const FaceToken    Faces[2] = { Partition.Loops.Resolve(Partition.Fins.Resolve(First).Loop).Face, Partition.Loops.Resolve(Partition.Fins.Resolve(Second).Loop).Face };
            const BlendFaceHistory* Histories[2] = { HistoryOf.at(Faces[0].Slot), HistoryOf.at(Faces[1].Slot) };
            for (int Side = 0; Side < 2; ++Side)
            {
                const BlendFaceHistory& Blend = *Histories[Side];
                const BlendFaceHistory& Other = *Histories[1 - Side];
                if (Blend.Origin != BlendFaceOrigin::Edge) continue;
                const EdgeBlendSheet& Sheet = *SheetOf.at(Blend.Edge.Slot);
                const bool Contact = Other.Origin == BlendFaceOrigin::Modified && (Other.Face == Sheet.Faces[0] || Other.Face == Sheet.Faces[1]);
                const bool Corner  = Other.Origin == BlendFaceOrigin::Vertex && Settings.Profile == BlendProfile::Fillet;
                if (!Contact && !Corner) continue;
                const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
                const CurveGeometry& Source = Partition.Curves.Resolve(Partition.Edges.Resolve(Sheet.Edge).Curve);
                for (int Sample = 1; Sample < 10; ++Sample)
                {
                    const Vector3 Position = EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / 10.0).Position;
                    if (Settings.Profile == BlendProfile::Fillet)
                    {
                        // ⚠️ atan2(|a × b|, a · b), not acos(a · b): acos near 1 resolves only √(2ε) ≈ 2.1·10⁻⁸ rad.
                        const Vector3 First = OutwardNormal(Partition, Faces[0], Position), Second = OutwardNormal(Partition, Faces[1], Position);
                        const double  Angle = std::atan2(Length(Cross(First, Second)), Dot(First, Second));
                        Tally(ContactGate, Angle, Angle <= 1.0e-10);
                        PassedCondition = PassedCondition && Angle <= 1.0e-10;
                    }
                    else
                    {
                        // 📝 Equal setback holds while both supports are the edge's own faces. After a smooth overflow the
                        //    chamfer is still the chord of the ball, so each contact lies on the ball: |P − C(τ)| = r.
                        //    The ball itself moves when one side rolls over, so the setback on the kept face then differs from d.
                        const FaceToken Own[2] = { Partition.Loops.Resolve(Partition.Fins.Resolve(Sheet.Fins[0]).Loop).Face, Partition.Loops.Resolve(Partition.Fins.Resolve(Sheet.Fins[1]).Loop).Face };
                        const bool Original = Sheet.Faces[0] == Own[0] && Sheet.Faces[1] == Own[1];
                        const double Setback  = Original ? Distance(Position, EvaluateCurve(Source, ProjectOntoCurve(Source, Position, Partition.Edges.Resolve(Sheet.Edge).IntervalLow)).Position)
                                                         : Distance(Position, EvaluateCurve(Sheet.Spine, ProjectOntoCurve(Sheet.Spine, Position, 0.0)).Position);
                        // Variable chamfer: the setback interpolates linearly from Size (start vertex) to EndSize (end vertex).
                        double Expected = Original ? Settings.Size : Sheet.Radius;                     // [len]
                        if (Original && Settings.EndSize)
                        {
                            const EdgeProfile& Range = Partition.Edges.Resolve(Sheet.Edge);
                            const Vector3 Start = Partition.Vertices.Resolve(Range.Start).Position, Finish = Partition.Vertices.Resolve(Range.End).Position;
                            const double  Fraction = Dot(Position - Start, Finish - Start) / Dot(Finish - Start, Finish - Start);
                            Expected = Settings.Size + (*Settings.EndSize - Settings.Size) * Fraction;
                        }
                        double Residual = std::fabs(Setback - Expected);
                        if (Sheet.LawCondition())
                        {
                            // 📝 Ball law (curved edge): the contact is the foot of C(τ) at r(τ) = d·tan(θ(τ)/2), τ read
                            //    off its own rail; a straight setback to the edge is not d on a curved support.
                            const int    Rail  = Other.Face == Sheet.Faces[0] ? 0 : 1;
                            const double τ     = ProjectOntoCurve(Sheet.Contacts[Rail], Position, 0.0);
                            const Vector3 Ball = EvaluateCurve(Sheet.Spine, τ).Position;
                            Residual = std::fabs(Distance(Position, Ball) - MeasureSheetRadius(Sheet, τ)) + Distance(Position, EvaluateCurve(Sheet.Contacts[Rail], τ).Position);
                        }
                        Tally(ContactGate, Residual / Scale, Residual <= Limit);
                        PassedCondition = PassedCondition && Residual <= Limit;
                    }
                }
                break;
            }
        }
        return PassedCondition;
    }

    bool MeasureTessellation(const ModelPartition& Partition, BodyToken Body, int64_t& Triangles)
    {
        const TessellationOutcome     Outcome = TessellateBody(Partition, Body, {});
        const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
        Triangles = static_cast<int64_t>(Outcome.Complex.Triangles.size());
        const double VolumeRatio = std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea);
        const bool   PassedCondition = Outcome.Refusal.empty() && Report.UnpairedSides == 0 && Report.EulerComplex == Report.EulerBrep && Report.Inverted == 0 && VolumeRatio <= 1.0 && Report.MissingEdgeSides == 0;
        Tally(TessellationGate, VolumeRatio, PassedCondition);
        return PassedCondition;
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
        if (Argument == "--export" && Index + 1 < ArgumentCount) ExportDirectory = Arguments[++Index];
    }
    const Clock::time_point Start = Clock::now();
    std::vector<SpecimenRow> Rows;
    std::mt19937_64 Generator(0x5EB1E7D);

    // ---- B1 … B6, B8 over the catalogue.
    for (const std::string& Name : EnumerateBlendSpecimens())
    {
        if (!Selected(Name)) continue;
        ModelPartition      Partition;
        const BlendSpecimen Specimen = ConstructBlendSpecimen(Partition, Name);
        SpecimenRow         Row{ Name, Specimen.Family };
        Row.Edges = static_cast<int>(Specimen.Edges.size());
        const double         Before   = IntegrateBodyVolume(Partition, Specimen.Body);
        const AuditReport    Original = AuditBody(Partition, Specimen.Body, AuditScope::Complete);
        const BlendOutcome   Outcome  = BlendEdges(Partition, Specimen.Body, Specimen.Edges, Specimen.Settings);
        Row.Milliseconds = Outcome.Statistics.Milliseconds;
        const double         Untouched = std::fabs(IntegrateBodyVolume(Partition, Specimen.Body) - Before);
        if (Specimen.RefusalExpected)
        {
            const bool Refused = !Outcome.Refusal.empty() && Outcome.Refusal.rfind("Blend: ", 0) == 0 && Untouched == 0.0 && AuditBody(Partition, Specimen.Body, AuditScope::Complete).SoundCondition();
            Tally(RefusalGate, Refused ? 0.0 : 1.0, Refused);
            Row.Outcome = Outcome.Refusal.empty() ? "🔴 not refused" : Outcome.Refusal;
            Row.PassedCondition = Refused;
            Rows.push_back(Row);
            continue;
        }
        if (!Outcome.Refusal.empty())
        {
            Row.Outcome = Outcome.Refusal;
            Row.PassedCondition = false;
            Tally(VolumeGate, 1.0, false);
            Rows.push_back(Row);
            continue;
        }
        const double Scale = BodyScale(Partition, Specimen.Body);
        Row.Delta    = IntegrateBodyVolume(Partition, Outcome.Body) - Before;
        Row.Expected = Specimen.ExpectedDelta;
        Row.Error    = std::fabs(Row.Delta - Row.Expected);
        Row.Faces    = static_cast<int>(Outcome.FaceHistories.size());
        Row.Genus    = Outcome.Audit.Genus;
        const bool VolumeCondition = Row.Error <= 1.0e-9 * std::max(1.0, Before);
        Tally(VolumeGate, Row.Error / std::max(1.0, Before), VolumeCondition);
        const bool TopologyCondition = Outcome.Audit.SoundCondition() && Outcome.Audit.Genus == Original.Genus && (Specimen.ExpectedFaces < 0 || Row.Faces == Specimen.ExpectedFaces) && Untouched == 0.0;
        Tally(TopologyGate, TopologyCondition ? 0.0 : 1.0, TopologyCondition);
        const BlendStatistics& Statistics = Outcome.Statistics;
        char Text[160];
        std::snprintf(Text, sizeof(Text), "%dP %dC %dK %dT %dR %dS", Statistics.CarrierPlanes, Statistics.CarrierCylinders, Statistics.CarrierCones, Statistics.CarrierTori, Statistics.CarrierProcedural, Statistics.CornerSpheres);
        Row.Carriers = Text;
        std::snprintf(Text, sizeof(Text), "%d · %d · %d · %d", Statistics.Vertices[0], Statistics.Vertices[1], Statistics.Vertices[2], Statistics.Vertices[3]);
        Row.Vertices = Text;
        const bool TessellationCondition = MeasureTessellation(Partition, Outcome.Body, Row.Triangles);
        const TessellationOutcome Tessellation = TessellateBody(Partition, Outcome.Body, {});
        if (!ExportDirectory.empty()) ExportTessellation(Partition, Tessellation, ExportDirectory, Specimen.Name);
        const bool LawCondition = MeasureBlendLaws(Partition, Specimen.Body, Outcome, Specimen.Settings, Tessellation, Scale);
        bool MembershipCondition = true;
        if (Specimen.Clearance)
        {
            const BodyContainment Containment = PrepareBodyContainment(Partition, Outcome.Body);
            WindingSettings Coarse;
            Coarse.Precision = 1.0e-6;
            Vector3 Low = { 1e300, 1e300, 1e300 }, High = -Low;
            for (const Vector3& Position : Tessellation.Complex.Positions)
            {
                Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
                High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
            }
            std::uniform_real_distribution<double> Unit(-0.1, 1.1);
            int Wrong = 0;
            for (int Sample = 0; Sample < 600; ++Sample)
            {
                const Vector3 Point = { Low.X + (High.X - Low.X) * Unit(Generator), Low.Y + (High.Y - Low.Y) * Unit(Generator), Low.Z + (High.Z - Low.Z) * Unit(Generator) };
                const double Clearance = Specimen.Clearance(Point);
                if (std::fabs(Clearance) < 1.0e-6) continue;
                const PointMembership Membership = ClassifySolidPoint(Containment, Point, Coarse).Membership;
                const bool Agreement = (Clearance > 0.0) == (Membership == PointMembership::Inside);
                Tally(MembershipGate, Agreement ? 0.0 : 1.0, Agreement);
                Wrong += Agreement ? 0 : 1;
            }
            MembershipCondition = Wrong == 0;
        }
        Row.Outcome = "blended";
        Row.PassedCondition = VolumeCondition && TopologyCondition && TessellationCondition && LawCondition && MembershipCondition;
        Rows.push_back(Row);
    }

    // ---- B7: Booleans downstream of blends.
    struct BooleanRow { std::string Name; double Volume = 0.0, Expected = 0.0; bool PassedCondition = false; };
    std::vector<BooleanRow> BooleanRows;
    const auto Downstream = [&](const char* Name, const char* Specimen, auto&& Tool, BooleanOperation Operation, auto&& Expected)
    {
        if (!Selected(Specimen)) return;
        ModelPartition     Partition;
        const BlendSpecimen Built   = ConstructBlendSpecimen(Partition, Specimen);
        const BlendOutcome  Blended = BlendEdges(Partition, Built.Body, Built.Edges, Built.Settings);
        BooleanRow Row{ Name };
        if (Blended.Refusal.empty())
        {
            const double Blend = IntegrateBodyVolume(Partition, Blended.Body);
            const BooleanOutcome Result = ComputeBoolean(Partition, Blended.Body, Tool(Partition), Operation);
            Row.Expected = Expected(Blend);
            if (Result.Body.PresentCondition())
            {
                Row.Volume = IntegrateBodyVolume(Partition, Result.Body);
                const double Relative = std::fabs(Row.Volume - Row.Expected) / Row.Expected;
                Row.PassedCondition = Relative <= 1.0e-7 && AuditBody(Partition, Result.Body, AuditScope::Complete).SoundCondition();
                Tally(BooleanGate, Relative, Row.PassedCondition);
            }
            else Tally(BooleanGate, 1.0, false);
        }
        else Tally(BooleanGate, 1.0, false);
        BooleanRows.push_back(Row);
    };
    const OrthonormalFrame Upright = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const auto At = [&](Vector3 Origin) { OrthonormalFrame Frame = Upright; Frame.Origin = Origin; return Frame; };
    Downstream("RoundedBlock − drill (flat-to-flat)", "RoundedBlock", [&](ModelPartition& P) { return ConstructCylinder(P, At({ 1.0, 0.75, -1.0 }), 0.3, 3.0).Value; },
               BooleanOperation::Difference, [](double Blend) { return Blend - π * 0.09 * 1.0; });
    Downstream("RoundedBlock ∩ half-space x ≤ 1 (cuts 4 cylinders, 0 spheres)", "RoundedBlock", [](ModelPartition& P) { return ConstructBlock(P, { -1.0, -1.0, -1.0 }, { 2.0, 3.5, 3.0 }).Value; },
               BooleanOperation::Intersection, [](double Blend) { return 0.5 * Blend; });
    Downstream("CylinderRimFillet − coaxial bore r 0.4", "CylinderRimFillet", [&](ModelPartition& P) { return ConstructCylinder(P, At({ 0.0, 0.0, -1.0 }), 0.4, 4.0).Value; },
               BooleanOperation::Difference, [](double Blend) { return Blend - π * 0.16 * 2.0; });
    Downstream("BoxAllChamfer ∪ block on the top flat", "BoxAllChamfer", [](ModelPartition& P) { return ConstructBlock(P, { 1.5, 1.0, 1.5 }, { 1.0, 1.0, 1.0 }).Value; },
               BooleanOperation::Union, [](double Blend) { return Blend + 0.5; });
    Downstream("CliffPlateFillet − drill r 0.3 (through the kept flat)", "CliffPlateFillet", [&](ModelPartition& P) { return ConstructCylinder(P, At({ 2.0, 1.2, -1.0 }), 0.3, 3.0).Value; },
               BooleanOperation::Difference, [](double Blend) { return Blend - π * 0.09 * 0.2; });
    Downstream("CliffDiscFillet − coaxial bore r 0.4", "CliffDiscFillet", [&](ModelPartition& P) { return ConstructCylinder(P, At({ 0.0, 0.0, -1.0 }), 0.4, 3.0).Value; },
               BooleanOperation::Difference, [](double Blend) { return Blend - π * 0.16 * 0.2; });

    // ---- B9: determinism on BevelLab and the overflow family.
    for (const std::string& Name : EnumerateBlendSpecimens())
    {
        ModelPartition Probe;
        const std::string Family = Selected(Name) ? ConstructBlendSpecimen(Probe, Name).Family : std::string();
        if (Family != "BevelLab" && Family != "Overflow") continue;
        uint64_t Digests[2] = {};
        for (uint64_t& Digest : Digests)
        {
            ModelPartition      Partition;
            const BlendSpecimen Specimen = ConstructBlendSpecimen(Partition, Name);
            const BlendOutcome  Outcome  = BlendEdges(Partition, Specimen.Body, Specimen.Edges, Specimen.Settings);
            Digest = Outcome.Refusal.empty() ? DigestPolygonComplex(TessellateBody(Partition, Outcome.Body, {}).Complex) : 0;
        }
        Tally(DeterminismGate, Digests[0] == Digests[1] ? 0.0 : 1.0, Digests[0] == Digests[1] && Digests[0] != 0);
    }

    // ---- B10: scale invariance (rounded box and rim fillet scaled by s, sizes scaled by s).
    struct ScaleRow { double Scale = 1.0; double Box = 0.0, Rim = 0.0, Cliff = 0.0; };
    std::vector<ScaleRow> ScaleRows;
    for (double Factor : { 1.0e-3, 1.0e-1, 1.0e1, 1.0e3 })
    {
        if (!Selected("Scale")) break;
        ScaleRow Row{ Factor };
        {
            ModelPartition  Partition;
            const BodyToken Body = ConstructBlock(Partition, { Factor, -Factor, 0.0 }, { 4.0 * Factor, 3.0 * Factor, 2.0 * Factor }).Value;
            const BlendOutcome Outcome = BlendEdges(Partition, Body, Partition.CollectInventory(Body).Edges, { BlendProfile::Fillet, 0.3 * Factor });
            const double R = 0.3, Expected = -((4.0 - π) * R * R * (9.0 - 6.0 * R) + (8.0 - 4.0 / 3.0 * π) * R * R * R) * Factor * Factor * Factor;
            Row.Box = Outcome.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Outcome.Body) - IntegrateBodyVolume(Partition, Body) - Expected) / std::fabs(Expected) : 1.0;
        }
        {
            ModelPartition  Partition;
            const BodyToken Body = ConstructCylinder(Partition, { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, Factor, 2.0 * Factor).Value;
            std::vector<EdgeToken> Rim;
            for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
            {
                const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                if (!std::holds_alternative<StraightLine>(Partition.Curves.Resolve(Profile.Curve)) && Partition.Vertices.Resolve(Profile.Start).Position.Z > Factor) Rim.push_back(Edge);
            }
            const BlendOutcome Outcome  = BlendEdges(Partition, Body, Rim, { BlendProfile::Fillet, 0.3 * Factor });
            const double       Expected = MeasureRimBlendVolume({ Factor, 2.0 * Factor }, { -1.0, 0.0 }, { 0.0, -1.0 }, 0.3 * Factor, BlendProfile::Fillet, true);
            Row.Rim = Outcome.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Outcome.Body) - IntegrateBodyVolume(Partition, Body) - Expected) / std::fabs(Expected) : 1.0;
        }
        {
            // Cliff overflow: plate t = 0.2 s, front top edge r = 0.3 s; removed section r·t − ∫_{r−t}^{r} √(r² − w²) dw per unit length.
            ModelPartition  Partition;
            const BodyToken Body = ConstructBlock(Partition, {}, { 4.0 * Factor, 2.0 * Factor, 0.2 * Factor }).Value;
            std::vector<EdgeToken> Front;
            for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
            {
                const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                const Vector3 Start = Partition.Vertices.Resolve(Profile.Start).Position, End = Partition.Vertices.Resolve(Profile.End).Position;
                if (std::fabs(Start.Y) <= 1.0e-12 * Factor && std::fabs(End.Y) <= 1.0e-12 * Factor && Start.Z > 0.1 * Factor && End.Z > 0.1 * Factor) Front.push_back(Edge);
            }
            const BlendOutcome Outcome   = BlendEdges(Partition, Body, Front, { BlendProfile::Fillet, 0.3 * Factor });
            const auto         Primitive = [](double Level) { return 0.5 * (Level * std::sqrt(0.09 - Level * Level) + 0.09 * std::asin(Level / 0.3)); };
            const double       Expected  = -4.0 * (0.3 * 0.2 - (Primitive(0.3) - Primitive(0.1))) * Factor * Factor * Factor;
            Row.Cliff = Outcome.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Outcome.Body) - IntegrateBodyVolume(Partition, Body) - Expected) / std::fabs(Expected) : 1.0;
        }
        const double Worst = std::max({ Row.Box, Row.Rim, Row.Cliff });
        Tally(ScaleGate, Worst, Worst <= 1.0e-9);
        ScaleRows.push_back(Row);
    }

    // ---- Report.
    const GateTally* Gates[] = { &VolumeGate, &TopologyGate, &ContactGate, &BallGate, &TessellationGate, &MembershipGate, &BooleanGate, &RefusalGate, &DeterminismGate, &ScaleGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !OnlyPrefix.empty());   // ⚠️ Full run: every gate must be exercised
    for (const SpecimenRow& Row : Rows) PassedCondition = PassedCondition && Row.PassedCondition;
    int Harness = 0, HarnessPassed = 0, Lab = 0, LabPassed = 0;
    for (const SpecimenRow& Row : Rows)
    {
        if (Row.Family == "Harness") { ++Harness; HarnessPassed += Row.PassedCondition; }
        if (Row.Family == "BevelLab") { ++Lab; LabPassed += Row.PassedCondition; }
    }

    std::printf("# Blend validation (step 5, I)\n\n");
    if (!OnlyPrefix.empty()) std::printf("> 🔍 Filtered run: `--only %s` (gates with 0 checks were not exercised)\n\n", OnlyPrefix.c_str());
    std::printf("**%s** · rolling-ball harness %d/%d · BevelLab %d/%d · %zu specimens · %.0f ms\n\n", PassedCondition ? "🟢 PASS" : "🔴 FAIL", HarnessPassed, Harness, LabPassed, Lab, Rows.size(), Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Specimens\n\nCarriers: P plane · C cylinder · K cone · T torus · R procedural rolling ball (arc or chord) · S corner sphere. Vertices: cap · mitre · ball corner · continuation.\n\n");
    std::printf("| | Specimen | Family | Edges | Faces | Genus | ΔV | ΔV* (closed form) | \\|error\\| | Carriers | Vertices | Triangles | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const SpecimenRow& Row : Rows)
    {
        if (Row.Outcome != "blended")
        {
            std::printf("| %s | %s | %s | %d | – | – | refused | – | – | – | – | – | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Family.c_str(), Row.Edges, Row.Milliseconds);
            continue;
        }
        std::printf("| %s | %s | %s | %d | %d | %d | %.12f | %.12f | %.1e | %s | %s | %lld | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Family.c_str(), Row.Edges, Row.Faces, Row.Genus,
                    Row.Delta, Row.Expected, Row.Error, Row.Carriers.c_str(), Row.Vertices.c_str(), static_cast<long long>(Row.Triangles), Row.Milliseconds);
    }
    std::printf("\n## Refusals\n\n| | Specimen | Refusal |\n|---|---|---|\n");
    for (const SpecimenRow& Row : Rows)
    {
        if (Row.Outcome != "blended") std::printf("| %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Outcome.c_str());
    }
    std::printf("\n## Booleans downstream (B7)\n\n| | Operation | V | V* | rel. error |\n|---|---|---|---|---|\n");
    for (const BooleanRow& Row : BooleanRows)
    {
        std::printf("| %s | %s | %.12f | %.12f | %.1e |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Volume, Row.Expected, Row.Expected != 0.0 ? std::fabs(Row.Volume - Row.Expected) / Row.Expected : 0.0);
    }
    std::printf("\n## Scale (B10)\n\n| | s | Rounded box rel. error | Rim fillet rel. error | Cliff plate rel. error |\n|---|---|---|---|---|\n");
    for (const ScaleRow& Row : ScaleRows)
    {
        std::printf("| %s | %.0e | %.1e | %.1e | %.1e |\n", std::max({ Row.Box, Row.Rim, Row.Cliff }) <= 1.0e-9 ? "🟢" : "🔴", Row.Scale, Row.Box, Row.Rim, Row.Cliff);
    }
    return PassedCondition ? 0 : 1;
}
